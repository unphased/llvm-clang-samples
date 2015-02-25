
#include <string>

#include "clang/AST/AST.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Refactoring.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Rewrite/Core/Rewriter.h"
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
    const SourceManager* sm = Result.SourceManager;
    SourceLocation loc;
    if (record_decl && (loc = record_decl->getLocation(), sm->isInMainFile(loc))) {
      // auto parent = record_decl->getParent();
      // auto className = parent->getQualifiedNameAsString();
      std::string str("/* this is the CXXRecordDecl ");
      str += record_decl->getQualifiedNameAsString();
      // str += " in class/struct/union ";
      // str += className;
      str += " */";
      Replacement Rep(*(sm), record_decl->getLocStart(), 0, str);
      Replace->insert(Rep);
    }
    auto field_decl = Result.Nodes.getNodeAs<clang::FieldDecl>("fielddecl");
    if (field_decl) {
      loc = field_decl->getLocation();
      auto name = field_decl->getQualifiedNameAsString();
      llvm::outs() <<
        " Main file: " << sm->isInMainFile(loc) <<
        " Written in main file: " << sm->isWrittenInMainFile(loc) <<
        " Filename: " << sm->getFilename(loc) <<
        " Line: " << sm->getPresumedLineNumber(loc) <<
        " Col: " << sm->getPresumedColumnNumber(loc) <<
        ": " << name;
      // Presumed locations are always for expansion points.
      std::pair<FileID, unsigned> locInfo = sm->getDecomposedExpansionLoc(loc);
 
      bool invalid = false;
      const clang::SrcMgr::SLocEntry &Entry = sm->getSLocEntry(locInfo.first, &invalid);
      if (invalid || !Entry.isFile()) {
        llvm::outs() << " invalid ";
      }
      const SrcMgr::FileInfo &FI = Entry.getFile();
      llvm::outs() << "\n";
    }
    if (field_decl && sm->isInMainFile(field_decl->getLocation())) {
      std::string str("/* this is the FieldDecl ");
      str += field_decl->getQualifiedNameAsString();
      str += " */";
      Replacement Rep(*(sm), field_decl->getLocStart(), 0, str);
      Replace->insert(Rep);
    }
  }
private:
  Replacements *Replace;
};

static DeclarationMatcher recorddeclmatcher = 
  // recordDecl(hasDescendant(fieldDecl())).bind("recorddecl");
  recordDecl().bind("recorddecl");

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

  // use Rewriter to get visibility into the collapsed Replacements
  IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts = new DiagnosticOptions();
  DiagnosticsEngine Diagnostics(
      IntrusiveRefCntPtr<DiagnosticIDs>(new DiagnosticIDs()), &*DiagOpts,
      new TextDiagnosticPrinter(llvm::errs(), &*DiagOpts), true);
  SourceManager Sources(Diagnostics, Tool.getFiles());

  // Apply all replacements to a rewriter.
  Rewriter Rewrite(Sources, LangOptions());
  Tool.applyAllReplacements(Rewrite);

  // Query the rewriter for all the files it has rewritten, dumping their new
  // contents to stdout.
  for (Rewriter::buffer_iterator I = Rewrite.buffer_begin(),
                                 E = Rewrite.buffer_end();
       I != E; ++I) {
    const FileEntry *Entry = Sources.getFileEntryForID(I->first);
    llvm::outs() << "Rewrite buffer for file: " << Entry->getName() << "\n";
    llvm::outs().changeColor(raw_ostream::YELLOW);
    I->second.write(llvm::outs());
    llvm::outs().resetColor() << "\n";
  }

  return 0;
}
