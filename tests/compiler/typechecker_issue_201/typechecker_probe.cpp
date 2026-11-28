//===- typechecker_probe.cpp - issue #201 TypeChecker unit probe --*- C++ -*-===//
//
// Runs cplus::TypeChecker::run() on one fixture and prints the resulting
// registry entries and diagnostics in a stable, line-oriented format:
//
//   record name=<name> resource=<0|1> ctor=<name>_new drop=<name>_drop
//   diag level=<n> message=<text>
//   errors=<n>
//
// The C+ rewriter attaches annotation attributes to the lowered RecordDecl.
// Clang's own placement of source-level `__attribute__((annotate(...)))` on a
// definition varies by frontend version, so each fixture instead carries
// deterministic directives of the form:
//
//   // ANNOTATE <RecordName> <payload> [payload ...]
//
// The probe creates the corresponding AnnotateAttr objects in that exact order
// on the matching top-level record definition, then calls TypeChecker::run().
// Any directive that cannot be attached is a hard harness error (never a
// silent pass).
//
//===----------------------------------------------------------------------===//

#include "Diagnostics.h"
#include "Sema/TypeChecker.h"

#include <clang/AST/ASTContext.h>
#include <clang/AST/Attr.h>
#include <clang/AST/Decl.h>
#include <clang/Basic/AttrKinds.h>
#include <clang/Basic/AttributeCommonInfo.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Frontend/ASTUnit.h>
#include <clang/Frontend/PCHContainerOperations.h>
#include <clang/Tooling/Tooling.h>

#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: typechecker_probe <fixture.cpp>\n";
    return 2;
  }
  std::ifstream input(argv[1]);
  if (!input) {
    std::cerr << "cannot open fixture: " << argv[1] << "\n";
    return 2;
  }
  const std::string source((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());

  // Collect the deterministic annotation directives from the fixture.
  std::vector<std::pair<std::string, std::vector<std::string>>> directives;
  std::istringstream lines(source);
  std::string line;
  while (std::getline(lines, line)) {
    const std::string mark = "// ANNOTATE ";
    auto pos = line.find(mark);
    if (pos == std::string::npos)
      continue;
    std::istringstream tokens(line.substr(pos + mark.size()));
    std::string name;
    if (!(tokens >> name))
      continue;
    std::vector<std::string> payloads;
    std::string payload;
    while (tokens >> payload)
      payloads.push_back(payload);
    if (!payloads.empty())
      directives.emplace_back(name, std::move(payloads));
  }

  const std::vector<std::string> args = {"-std=c++17", "-fsyntax-only",
                                         "-Wno-everything"};
  clang::IgnoringDiagConsumer ignore;
  std::unique_ptr<clang::ASTUnit> ast = clang::tooling::buildASTFromCodeWithArgs(
      source, args, "issue201_fixture.cpp", "cplus-typechecker-201",
      std::make_shared<clang::PCHContainerOperations>(),
      clang::tooling::getClangStripDependencyFileAdjuster(), {}, &ignore);
  if (!ast || ast->getDiagnostics().hasErrorOccurred()) {
    std::cerr << "parse_error\n";
    return 3;
  }

  clang::ASTContext &ctx = ast->getASTContext();
  for (const auto &directive : directives) {
    clang::RecordDecl *target = nullptr;
    for (clang::Decl *d : ctx.getTranslationUnitDecl()->decls()) {
      auto *rd = clang::dyn_cast<clang::RecordDecl>(d);
      if (rd && rd->isCompleteDefinition() &&
          rd->getNameAsString() == directive.first) {
        target = rd;
        break;
      }
    }
    if (!target) {
      std::cerr << "missing_record " << directive.first << "\n";
      return 4;
    }
    for (const auto &payload : directive.second) {
      const clang::AttributeCommonInfo info(
          clang::SourceRange(), clang::AttributeCommonInfo::AT_Annotate,
          clang::AttributeCommonInfo::Form::GNU());
      target->addAttr(clang::AnnotateAttr::Create(ctx, payload, info));
    }
  }

  cplus::DiagnosticEngine diags;
  cplus::TypeChecker checker(diags, cplus::Mode::Safe, cplus::OptMode::Debug);
  checker.run(ctx);

  for (const auto &info : checker.registry().all()) {
    std::cout << "record name=" << info.name
              << " resource=" << (info.isResource ? 1 : 0)
              << " ctor=" << info.ctorSymbol
              << " drop=" << info.dropSymbol << "\n";
  }
  for (const auto &d : diags.diags()) {
    std::cout << "diag level=" << static_cast<int>(d.level)
              << " message=" << d.message << "\n";
  }
  std::cout << "errors=" << diags.errorCount() << "\n";
  return 0;
}
