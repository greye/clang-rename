#ifndef CLANG_RENAME_DEPENDENCYDB_H
#define CLANG_RENAME_DEPENDENCYDB_H

#include <clang/Basic/LLVM.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/FileMatchTrie.h>
#include <llvm/ADT/StringMap.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/YAMLParser.h>
#include <memory>
#include <string>
#include <vector>

class DependencyDatabase : public clang::tooling::CompilationDatabase {
public:
  /// \brief Returns all compile comamnds in which the specified file was
  /// compiled.
  ///
  /// FIXME: Currently FilePath must be an absolute path inside the
  /// source directory which does not have symlinks resolved.
  std::vector<CompileCommand>
  getCompileCommands(StringRef FilePath) const override;

  /// \brief Returns the list of all files available in the compilation database.
  ///
  /// These are the 'file' entries of the JSON objects.
  std::vector<std::string> getAllFiles() const override;

  /// \brief Returns all compile commands for all the files in the compilation
  /// database.
  std::vector<CompileCommand> getAllCompileCommands() const override;

private:
  /// \brief Constructs a JSON compilation database on a memory buffer.
  DependencyDatabase(llvm::MemoryBuffer *Database)
    : Database(Database), YAMLStream(Database->getBuffer(), SM) {}

  /// \brief Parses the database file and creates the index.
  ///
  /// Returns whether parsing succeeded. Sets ErrorMessage if parsing
  /// failed.
  bool parse(std::string &ErrorMessage);

  // Tuple (directory, commandline) where 'commandline' pointing to the
  // corresponding nodes in the YAML stream.
  typedef std::pair<llvm::yaml::ScalarNode*,
                    llvm::yaml::ScalarNode*> CompileCommandRef;

  /// \brief Converts the given array of CompileCommandRefs to CompileCommands.
  void getCommands(ArrayRef<CompileCommandRef> CommandsRef,
                   std::vector<CompileCommand> &Commands) const;

private:
  // Maps file paths to the compile command lines for that file.
  llvm::StringMap< std::vector<CompileCommandRef> > IndexByFile;
  llvm::StringMap< std::vector<llvm::StringRef> > ReverseDeps;

  llvm::DenseMap< llvm::StringRef, std::vector<llvm::StringRef> > Dependencies;

  FileMatchTrie MatchTrie;

  std::unique_ptr<llvm::MemoryBuffer> Database;
  llvm::yaml::Stream YAMLStream;

  llvm::SourceMgr SM;
};

#endif
