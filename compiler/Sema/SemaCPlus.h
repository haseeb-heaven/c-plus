//===- SemaCPlus.h - C+ parsing / semantic extension of Clang ---*- C++ -*-===//
//
// The C+ frontend.  Clang's C parser is reused verbatim for ordinary C; the
// C+ specific syntax is recognised by a small set of source-level rewrites
// performed *before* Clang sees the buffer:
//
//   * `resource struct Foo { ... };`  -> `struct Foo { ... };` marked with an
//     annotate attribute.  Implemented by rewriting the token stream into
//     `__attribute__((annotate("cplus_resource"))) struct Foo { ... };`.
//
//   * `class Foo { ... };`             -> `struct Foo { ... };` marked with
//     `annotate("cplus_class")`.  Method bodies are ordinary C function
//     bodies in which `self` refers to the implicit first parameter.
//
//   * `managed T *p = ...;`            -> an ordinary pointer whose pointee
//     type is registered as managed; the declaration is annotated so the
//     RAII pass can insert a scope cleanup.
//
//   * `owned T h = ...;`               -> an ordinary value whose type is
//     registered as owning.
//
// The rewriting is done on the *token* level using Clang's own lexer, so
// comments, string literals, line continuations and the preprocessor all
// behave exactly as they do in C.
//
//===----------------------------------------------------------------------===//

#ifndef CPLUS_SEMACPLUS_H
#define CPLUS_SEMACPLUS_H

#include "Diagnostics.h"
#include "LangFeatures.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace clang {
class ASTContext;
class CompilerInstance;
class Preprocessor;
} // namespace clang

namespace cplus {

/// The result of preprocessing + C+ source rewriting: the modified source
/// text that is handed to Clang.
struct RewrittenSource {
  std::string text;
  /// True if the rewrite changed anything.
  bool changed = false;
  /// Names of `resource struct` types declared in this translation unit.
  std::vector<std::string> resourceTypes;
  /// Names of `class` types declared in this translation unit.
  std::vector<std::string> classTypes;
  /// Extents of fixed-size arrays declared in this translation unit, used by
  /// `arr.len` and by the static bounds check.
  std::map<std::string, size_t> arrayLengths;
};

/// Rewrite C+ specific syntax into equivalent C syntax annotated for the
/// later passes.  The rewrite is purely lexical/syntactic and preserves line
/// numbering so diagnostics stay accurate.
/// Insert C+ runtime bounds checks around dynamic indices into fixed-size
/// arrays. Constant indices are left to the static bounds checker.
std::string instrumentDynamicArrayBounds(
    const std::string &source,
    const std::map<std::string, size_t> &arrayLengths);

RewrittenSource rewriteCPlusSyntax(const std::string &source,
                                   DiagnosticEngine &diags,
                                   const std::string &filename);

/// Options controlling how the C+ driver runs Clang.
struct FrontendOptions {
  Mode mode = Mode::Safe;
  OptMode opt = OptMode::Debug;
  std::vector<std::string> includeDirs;
  std::vector<std::string> defines;
  std::vector<std::string> undefs;
  std::vector<std::string> extraArgs;
  std::string sysroot;
  std::string stdVersion = "c17";
  bool warningsAsErrors = false;
  bool verbose = false;
};

/// Builds the Clang invocation for the given source and options.
std::vector<std::string> buildClangArgs(const FrontendOptions &opts,
                                         const std::string &file);

} // namespace cplus

#endif // CPLUS_SEMACPLUS_H
