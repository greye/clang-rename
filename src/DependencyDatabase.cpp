#include "DependencyDatabase.h"
#include "DependencyDatabasePlugin.h"

#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/CompilationDatabasePluginRegistry.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/Support/Path.h>
#include <system_error>

using namespace clang;
using namespace clang::tooling; 

namespace {

/// \brief A parser for escaped strings of command line arguments.
///
/// Assumes \-escaping for quoted arguments (see the documentation of
/// unescapeCommandLine(...)).
class CommandLineArgumentParser {
 public:
  CommandLineArgumentParser(StringRef CommandLine)
      : Input(CommandLine), Position(Input.begin()-1) {}

  std::vector<std::string> parse() {
    bool HasMoreInput = true;
    while (HasMoreInput && nextNonWhitespace()) {
      std::string Argument;
      HasMoreInput = parseStringInto(Argument);
      CommandLine.push_back(Argument);
    }
    return CommandLine;
  }

 private:
  // All private methods return true if there is more input available.

  bool parseStringInto(std::string &String) {
    do {
      if (*Position == '"') {
        if (!parseDoubleQuotedStringInto(String)) return false;
      } else if (*Position == '\'') {
        if (!parseSingleQuotedStringInto(String)) return false;
      } else {
        if (!parseFreeStringInto(String)) return false;
      }
    } while (*Position != ' ');
    return true;
  }

  bool parseDoubleQuotedStringInto(std::string &String) {
    if (!next()) return false;
    while (*Position != '"') {
      if (!skipEscapeCharacter()) return false;
      String.push_back(*Position);
      if (!next()) return false;
    }
    return next();
  }

  bool parseSingleQuotedStringInto(std::string &String) {
    if (!next()) return false;
    while (*Position != '\'') {
      String.push_back(*Position);
      if (!next()) return false;
    }
    return next();
  }

  bool parseFreeStringInto(std::string &String) {
    do {
      if (!skipEscapeCharacter()) return false;
      String.push_back(*Position);
      if (!next()) return false;
    } while (*Position != ' ' && *Position != '"' && *Position != '\'');
    return true;
  }

  bool skipEscapeCharacter() {
    if (*Position == '\\') {
      return next();
    }
    return true;
  }

  bool nextNonWhitespace() {
    do {
      if (!next()) return false;
    } while (*Position == ' ');
    return true;
  }

  bool next() {
    ++Position;
    return Position != Input.end();
  }

  const StringRef Input;
  StringRef::iterator Position;
  std::vector<std::string> CommandLine;
};

std::vector<std::string> unescapeCommandLine(
    StringRef EscapedCommandLine) {
  CommandLineArgumentParser parser(EscapedCommandLine);
  return parser.parse();
}

class DependencyDatabasePlugin : public CompilationDatabasePlugin {
  CompilationDatabase *
  loadFromDirectory(llvm::StringRef Directory,
                    std::string &ErrorMessage) override {
    llvm::SmallString<1024> Path(Directory);
    llvm::sys::path::append(Path, "compile_filedeps.json");
    std::unique_ptr<CompilationDatabase> Database(
        DependencyDatabase::loadFromFile(Path, ErrorMessage));
    if (!Database)
      return nullptr;
    return Database.release();
  }
};
} // end namespace

void
clang::rename::registerDependencyDatabasePlugin() {
  static CompilationDatabasePluginRegistry::Add<DependencyDatabasePlugin>
  RegisterDatabasePluginAction(
      "json-dependency-database",
      "Reads JSON formatted compilation databases with file dependencies");
}

namespace llvm {
  template<>
  struct DenseMapInfo<StringRef> {
    // This assumes that "" will never be a valid key.
    static inline StringRef getEmptyKey() { return StringRef(""); }
    static inline StringRef getTombstoneKey() { return StringRef(); }
    static unsigned getHashValue(StringRef Val) { return HashString(Val); }
    static bool isEqual(StringRef LHS, StringRef RHS) { return LHS == RHS; }
  };
}

// Register the DependencyDatabasePlugin with the
// CompilationDatabasePluginRegistry using this statically initialized variable.
DependencyDatabase *
DependencyDatabase::loadFromFile(StringRef FilePath,
                                 std::string &ErrorMessage) {
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> DatabaseBuffer =
      llvm::MemoryBuffer::getFile(FilePath);
  if (std::error_code Result = DatabaseBuffer.getError()) {
    ErrorMessage = "Error while opening JSON database: " + Result.message();
    return nullptr;
  }
  std::unique_ptr<DependencyDatabase> Database(
      new DependencyDatabase(DatabaseBuffer->release()));
  if (!Database->parse(ErrorMessage))
    return nullptr;
  return Database.release();
}

std::vector<CompileCommand>
DependencyDatabase::getCompileCommands(StringRef FilePath) const {
  SmallString<128> NativeFilePath;
  llvm::sys::path::native(FilePath, NativeFilePath);

  std::string Error;
  llvm::raw_string_ostream ES(Error);
  StringRef Match = MatchTrie.findEquivalent(NativeFilePath.str(), ES);
  if (Match.empty()) {
    return std::vector<CompileCommand>();
  }

  auto CmdsRefIt = IndexByFile.find(Match);
  if (CmdsRefIt != IndexByFile.end()) {
    std::vector<CompileCommand> Commands;
    getCommands(CmdsRefIt->getValue(), Commands);
    return Commands;
  }

  auto RDepsIt = ReverseDeps.find(Match);
  if (RDepsIt != ReverseDeps.end()) {
    std::vector<CompileCommand> Commands;
    auto &RDeps = RDepsIt->getValue();
    for (auto it = RDeps.begin(), end = RDeps.end(); it != end; ++it) {
      getCommands(IndexByFile.find(*it)->getValue(), Commands);
    }
    return Commands;
  }
  return std::vector<CompileCommand>();
}

std::vector<std::string>
DependencyDatabase::getAllFiles() const {
  std::vector<std::string> Result;

  llvm::StringMap< std::vector<CompileCommandRef> >::const_iterator
    CommandsRefI = IndexByFile.begin();
  const llvm::StringMap< std::vector<CompileCommandRef> >::const_iterator
    CommandsRefEnd = IndexByFile.end();
  for (; CommandsRefI != CommandsRefEnd; ++CommandsRefI) {
    Result.push_back(CommandsRefI->first().str());
  }

  return Result;
}

std::vector<CompileCommand>
DependencyDatabase::getAllCompileCommands() const {
  std::vector<CompileCommand> Commands;
  for (llvm::StringMap< std::vector<CompileCommandRef> >::const_iterator
        CommandsRefI = IndexByFile.begin(), CommandsRefEnd = IndexByFile.end();
      CommandsRefI != CommandsRefEnd; ++CommandsRefI) {
    getCommands(CommandsRefI->getValue(), Commands);
  }
  return Commands;
}

void DependencyDatabase::getCommands(
                                  ArrayRef<CompileCommandRef> CommandsRef,
                                  std::vector<CompileCommand> &Commands) const {
  for (int I = 0, E = CommandsRef.size(); I != E; ++I) {
    SmallString<8> DirectoryStorage;
    SmallString<1024> CommandStorage;
    Commands.push_back(CompileCommand(
      // FIXME: Escape correctly:
      CommandsRef[I].first->getValue(DirectoryStorage),
      unescapeCommandLine(CommandsRef[I].second->getValue(CommandStorage))));
  }
}

static SmallString<128>
getNativePath(llvm::yaml::ScalarNode *File, llvm::yaml::ScalarNode *Directory) {
  SmallString<16> FileStorage;
  StringRef FileName = File->getValue(FileStorage);
  SmallString<128> NativeFilePath;
  if (llvm::sys::path::is_relative(FileName)) {
    SmallString<8> DirectoryStorage;
    SmallString<128> AbsolutePath(Directory->getValue(DirectoryStorage));
    llvm::sys::path::append(AbsolutePath, FileName);
    llvm::sys::path::native(AbsolutePath.str(), NativeFilePath);
  } else {
    llvm::sys::path::native(FileName, NativeFilePath);
  }
  return NativeFilePath;
}

bool DependencyDatabase::parse(std::string &ErrorMessage) {
  llvm::yaml::document_iterator I = YAMLStream.begin();
  if (I == YAMLStream.end()) {
    ErrorMessage = "Error while parsing YAML.";
    return false;
  }
  llvm::yaml::Node *Root = I->getRoot();
  if (!Root) {
    ErrorMessage = "Error while parsing YAML.";
    return false;
  }
  llvm::yaml::SequenceNode *Array = dyn_cast<llvm::yaml::SequenceNode>(Root);
  if (!Array) {
    ErrorMessage = "Expected array.";
    return false;
  }
  for (llvm::yaml::SequenceNode::iterator AI = Array->begin(),
                                          AE = Array->end();
       AI != AE; ++AI) {
    llvm::yaml::MappingNode *Object = dyn_cast<llvm::yaml::MappingNode>(&*AI);
    if (!Object) {
      ErrorMessage = "Expected object.";
      return false;
    }
    llvm::yaml::ScalarNode *Directory = nullptr;
    llvm::yaml::ScalarNode *Command = nullptr;
    llvm::yaml::ScalarNode *File = nullptr;
    llvm::SmallVector<llvm::yaml::ScalarNode *, 8> Deps;
    for (llvm::yaml::MappingNode::iterator KVI = Object->begin(),
                                           KVE = Object->end();
         KVI != KVE; ++KVI) {
      llvm::yaml::Node *Value = (*KVI).getValue();
      if (!Value) {
        ErrorMessage = "Expected value.";
        return false;
      }
      llvm::yaml::ScalarNode *KeyString =
          dyn_cast<llvm::yaml::ScalarNode>((*KVI).getKey());
      if (!KeyString) {
        ErrorMessage = "Expected strings as key.";
        return false;
      }

      SmallString<8> KeyStorage;
      llvm::yaml::ScalarNode *ValueString = nullptr;

      if (auto SeqNode = dyn_cast<llvm::yaml::SequenceNode>(Value)) {
        if (KeyString->getValue(KeyStorage) == "deps") {
          for (auto DepsIt = SeqNode->begin(), DepsE = SeqNode->end();
               DepsIt != DepsE;
               ++DepsIt) {
            auto *Node = dyn_cast<llvm::yaml::ScalarNode>(&*DepsIt);
            if (!Node) {
              ErrorMessage = "Expecting string values in dependency sequence.";
              return false;
            }
            Deps.push_back(Node);
          }
        } else {
          ErrorMessage = ("Unknown key: \"" +
                          KeyString->getRawValue() + "\"").str();
          return false;
        }
      } else if ((ValueString = dyn_cast<llvm::yaml::ScalarNode>(Value))) {
        if (KeyString->getValue(KeyStorage) == "directory") {
          Directory = ValueString;
        } else if (KeyString->getValue(KeyStorage) == "command") {
          Command = ValueString;
        } else if (KeyString->getValue(KeyStorage) == "file") {
          File = ValueString;
        } else {
          ErrorMessage = ("Unknown key: \"" +
                          KeyString->getRawValue() + "\"").str();
          return false;
        }
      } else {
        ErrorMessage = "Expected string or sequence value.";
        return false;
      }
    }
    if (!File) {
      ErrorMessage = "Missing key: \"file\".";
      return false;
    }
    if (!Command) {
      ErrorMessage = "Missing key: \"command\".";
      return false;
    }
    if (!Directory) {
      ErrorMessage = "Missing key: \"directory\".";
      return false;
    }
    SmallString<128> NativeFilePath = getNativePath(File, Directory);
    MatchTrie.insert(NativeFilePath.str());

    auto &tuEntry= IndexByFile.GetOrCreateValue(NativeFilePath);
    tuEntry.getValue().push_back(CompileCommandRef(Directory, Command));

    for (auto it = Deps.begin(), end = Deps.end(); it != end; ++it) {
      SmallString<128> DepPath = getNativePath(*it, Directory);;

      auto &DepEntry = ReverseDeps.GetOrCreateValue(DepPath);
      DepEntry.getValue().push_back(tuEntry.getKey());

      Dependencies[tuEntry.getKey()].push_back(DepEntry.getKey());

      MatchTrie.insert(DepPath.str());
    }
  }
  return true;
}
