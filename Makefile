SRCDIR=$(dir $(abspath $(lastword $(MAKEFILE_LIST))))
SOURCE=$(abspath $(shell find $(SRCDIR) -maxdepth 1 -name \*.cpp))

BUILDDIR=$(shell pwd)
OBJECT=$(addsuffix .o,$(basename $(addprefix $(BUILDDIR)/,$(notdir $(SOURCE)))))

CXX=g++
CXXFLAGS=$(shell llvm-config-3.5 --cxxflags)
SYSINC=-isystem $(shell llvm-config-3.5 --includedir)

LLVM_VERSION=$(shell llvm-config-3.5 --version)
LLVM_LIBS=clangTooling \
          clangFrontend \
          clangSerialization \
          clangParse \
          clangSema \
          clangAnalysis \
          clangEdit \
          clangRewrite \
          clangDriver \
          clangAST \
          clangASTMatchers \
          clangLex \
          clangBasic \
          clangIndex \
          LLVM-$(LLVM_VERSION)
LDFLAGS=$(shell llvm-config-3.5 --ldflags) $(addprefix -l,$(LLVM_LIBS))

$(BUILDDIR)/clang-rename: $(SRCDIR)/tool/ClangRename.cpp $(OBJECT)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

ifneq (clean, $(MAKECMDGOALS))
-include deps.mk
endif

$(BUILDDIR)/%.o: $(SRCDIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECT) clang-rename

deps.mk: $(SOURCE)
	$(CXX) $(SYSINC) $(CXXFLAGS) -MM -MT $(OBJECT) $^ > $@
