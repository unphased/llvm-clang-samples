
#include <string>

#include "clang/AST/AST.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Refactoring.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/raw_ostream.h"

#include "exfil.h"

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::driver;
using namespace clang::tooling;

static llvm::cl::OptionCategory ToolingSampleCategory("Matcher Sample");

class DeclHandler : public MatchFinder::MatchCallback {
public:
  DeclHandler(Replacements *Replace) : Replace(Replace) {}

  virtual void run(const MatchFinder::MatchResult &Result) {
    auto record_decl = Result.Nodes.getNodeAs<clang::CXXRecordDecl>("recorddecl");
    if (record_decl) {
      auto name = record_decl->getQualifiedNameAsString();
      llvm::outs() << "Found a decl: " << name << "\n";
    }
    if (record_decl && Result.SourceManager->isInMainFile(record_decl->getLocation())) {
      // auto parent = record_decl->getParent();
      // auto className = parent->getQualifiedNameAsString();
      std::string str("/* this is the CXXRecordDecl ");
      str += record_decl->getQualifiedNameAsString();
      // str += " in class/struct/union ";
      // str += className;
      str += " */";
      Replacement Rep(*(Result.SourceManager), record_decl->getLocStart(), 0, str);
      Replace->insert(Rep);
    }
    auto field_decl = Result.Nodes.getNodeAs<clang::FieldDecl>("fielddecl");
    if (field_decl && Result.SourceManager->isInMainFile(field_decl->getLocation())) {
      std::string str("/* this is the FieldDecl ");
      str += field_decl->getQualifiedNameAsString();
      str += " */";
      Replacement Rep(*(Result.SourceManager), field_decl->getLocStart(), 0, str);
      Replace->insert(Rep);
    }
  }
private:
  Replacements *Replace;
};

static DeclarationMatcher recorddeclmatcher = 
  recordDecl(hasDescendant(fieldDecl())).bind("recorddecl");

static DeclarationMatcher fielddeclmatcher = 
  fieldDecl().bind("fielddecl");

int main(int argc, const char **argv) {
  CommonOptionsParser op(argc, argv, ToolingSampleCategory);
  RefactoringTool Tool(op.getCompilations(), op.getSourcePathList());

  // Set up AST matcher callbacks.
  DeclHandler HandlerOfDecls(&Tool.getReplacements());

  MatchFinder Finder;
  Finder.addMatcher(recorddeclmatcher, &HandlerOfDecls);
  Finder.addMatcher(fielddeclmatcher, &HandlerOfDecls);

  // Run the tool and collect a list of replacements. We could call runAndSave,
  // which would destructively overwrite the files with their new contents.
  // However, for demonstration purposes it's interesting to show the
  // replacements.
  if (int Result = Tool.run(newFrontendActionFactory(&Finder).get())) {
    return Result;
  }

  llvm::outs() << "Replacements collected by the tool:\n";
  for (auto &r : Tool.getReplacements()) {
    llvm::outs() << r.toString() << "\n";
  }

  return 0;
}
