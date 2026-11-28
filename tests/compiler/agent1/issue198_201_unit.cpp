// Unit regression for Agent 1 issues #198 and #201.
//
// libcEffect() summaries and TypeChecker's record classification are library
// functions with no dedicated CLI surface, so this test calls them directly
// and asserts the exact argument indices / registry entries. Built by
// regression_issues198_201.sh against the freshly built cplus_core archive.
#include "Analysis/OwnershipAnalysis.h"
#include "Sema/TypeChecker.h"

#include "clang/AST/ASTContext.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Frontend/ASTUnit.h"
#include "clang/Frontend/PCHContainerOperations.h"
#include "clang/Tooling/Tooling.h"

#include <iostream>
#include <string>

using namespace cplus;

static int failures = 0;

static void check(bool ok, const std::string &what) {
  if (ok)
    return;
  ++failures;
  std::cerr << "FAIL: " << what << "\n";
}

int main() {
  // #198: exact destination-capacity argument indices.
  check(libcEffect("memset").sizeArg == 2, "memset sizeArg must be 2");
  check(libcEffect("memset_s").sizeArg == 1, "memset_s sizeArg must be 1");
  check(libcEffect("memcpy").sizeArg == 2, "memcpy sizeArg must be 2");
  check(libcEffect("strncpy").sizeArg == 2,
        "strncpy sizeArg must be 2, not the src pointer at 1");
  check(libcEffect("strncat").sizeArg == 2, "strncat sizeArg must be 2");
  check(libcEffect("strcpy_s").sizeArg == 1, "strcpy_s sizeArg must be 1");
  check(libcEffect("strncpy_s").sizeArg == 1, "strncpy_s sizeArg must be 1");
  check(libcEffect("strcat_s").sizeArg == 1, "strcat_s sizeArg must be 1");
  check(libcEffect("strncat_s").sizeArg == 1, "strncat_s sizeArg must be 1");
  check(libcEffect("strncpy").destArg == 0, "strncpy destArg must be 0");
  check(libcEffect("strncpy").checkedSize, "strncpy must be size-checked");
  check(libcEffect("fopen").returnsOwned, "fopen returns owned FILE*");
  check(!libcEffect("fopen_s").returnsOwned,
        "fopen_s returns errno_t, not an owned pointer");
  check(libcEffect("fopen_s").destArg == 0,
        "fopen_s delivers the FILE* through argument 0");
  check(libcEffect("fclose_s").frees, "fclose_s must free its argument");
  check(libcEffect("fread_s").sizeArg == 1, "fread_s sizeArg must be 1");
  check(libcEffect("fwrite_s").sizeArg == 1, "fwrite_s sizeArg must be 1");
  check(hasDeclaredEffect("memset_s") && hasDeclaredEffect("strncat") &&
            hasDeclaredEffect("strncat_s") &&
            hasDeclaredEffect("fclose_s") && hasDeclaredEffect("fread_s") &&
            hasDeclaredEffect("fwrite_s"),
        "SafeCRT/libc names must have builtin effect summaries");
  check(hasVerifiedEffectDeclaration("strncat_s"),
        "SafeCRT names must be authoritative for STRICT");
  // #199: trusted-helper names are closed, not a wildcard prefix.
  check(!hasVerifiedEffectDeclaration("cplus_unverified_sink"),
        "user cplus_ extern must not be trusted by prefix");
  check(!hasDeclaredEffect("cplus_core_helper"),
        "user cplus_ helper must not inherit a compiler effect summary");
  check(hasVerifiedEffectDeclaration("__cplus_bounds_check") &&
            hasVerifiedEffectDeclaration("cplus_scan_value_at"),
        "genuine compiler/runtime helpers must stay trusted");

  // #201: only the exact cplus_class / cplus_resource annotations count.
  const char *source = R"cpp(
    struct __attribute__((annotate("packed_wire"))) Packet { int header; };
    union  __attribute__((annotate("ffi_export"))) Wire { int i; float f; };
    struct __attribute__((annotate("cplus_class"))) Plain { int v; };
    struct __attribute__((annotate("cplus_resource"))) Res { ~Res() {} };
    struct __attribute__((annotate("reflect")))
           __attribute__((annotate("cplus_resource"))) Res2 { ~Res2() {} };
  )cpp";
  clang::IgnoringDiagConsumer ignore;
  std::unique_ptr<clang::ASTUnit> ast = clang::tooling::buildASTFromCodeWithArgs(
      source, {"-std=c++20", "-fsyntax-only"}, "issue201.cpp", "issue201-test",
      std::make_shared<clang::PCHContainerOperations>(),
      clang::tooling::getClangStripDependencyFileAdjuster(), {}, &ignore);
  check(ast != nullptr && !ast->getDiagnostics().hasErrorOccurred(),
        "annotation fixture must parse");
  if (ast) {
    DiagnosticEngine diags;
    TypeChecker tc(diags, Mode::Safe, OptMode::Debug);
    tc.run(ast->getASTContext());
    const TypeRegistry &reg = tc.registry();
    check(reg.findByName("Packet") == nullptr,
          "annotate(\"packed_wire\") must not create a C+ class");
    check(reg.findByName("Wire") == nullptr,
          "annotate(\"ffi_export\") must not create a C+ class");
    check(reg.findByName("Plain") != nullptr &&
              !reg.findByName("Plain")->isResource,
          "cplus_class must register a non-resource class");
    const ClassInfo *res = reg.findByName("Res");
    check(res != nullptr && res->isResource,
          "cplus_resource must register a resource");
    const ClassInfo *res2 = reg.findByName("Res2");
    check(res2 != nullptr && res2->isResource,
          "cplus_resource must be found even after an unrelated annotation");
    check(!diags.hasErrors(), "valid resources must not emit diagnostics");
  }

  if (failures) {
    std::cerr << failures << " check(s) failed\n";
    return 1;
  }
  std::cout << "issues #198/#199/#201 unit regression: PASS\n";
  return 0;
}
