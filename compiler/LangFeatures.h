//===- LangFeatures.h - C+ language surface definition -----------*- C++ -*-===//
//
// Definition of the C+ language extensions relative to C17.
//
// C+ is C plus a deliberately small set of additions:
//
//   1. `resource struct T { ... };`  - a struct with a destructor, owned by
//      scope, destroyed deterministically on scope exit.
//
//   2. `class T { ... };`             - a lightweight struct-based class with
//      methods that lower to ordinary functions.
//
//   3. `managed T *p = cplus_new(T);` - managed heap allocation, freed
//      automatically at scope exit, no manual `free`.
//
//   4. `owned T h = ...;` / `move(h)` / `borrow(h)` - explicit ownership
//      transfer and borrowing for resource handles.
//
// The *lexer* forms are kept unambiguous so that plain C never changes
// meaning.  Each keyword is spelled with a `cplus_`/`class_`/`resource_`
// prefix or uses a context that plain C cannot produce.
//
//===----------------------------------------------------------------------===//

#ifndef CPLUS_LANGFEATURES_H
#define CPLUS_LANGFEATURES_H

#include <string>

namespace cplus {

/// C+ language mode.
enum class Mode {
  /// Safe C+ (default).  Memory-unsafe constructs are diagnosed, and
  /// supported safe constructs are instrumented with runtime checks.
  Safe,
  /// Legacy C compatibility mode.  Ordinary C behaviour is preserved as far
  /// as the underlying Clang supports it.  Code compiled this way is NOT
  /// considered safe.
  LegacyC,
};

/// The C+ optimisation / safety configuration.
enum class OptMode {
  Debug,  ///< fast compile, -O0, safety checks on, full diagnostics
  Release ///< -O2, safety checks still ON (never disabled)
};

/// Names of the C+ keywords, kept in one place so the lexer, the parser
/// hooks and the tests cannot drift apart.
namespace kw {
inline constexpr const char *kResource = "resource";
inline constexpr const char *kClass = "class";
inline constexpr const char *kOwned = "owned";
inline constexpr const char *kManaged = "managed";
inline constexpr const char *kMove = "move";
inline constexpr const char *kBorrow = "borrow";
inline constexpr const char *kNew = "cplus_new";
inline constexpr const char *kSelf = "self";
inline constexpr const char *kDealloc = "cplus_dealloc";
} // namespace kw

/// Human-readable name of the language, used in diagnostics and --version.
inline constexpr const char *kLanguageName = "C+";

/// Version string of the language implementation.
inline constexpr const char *kLanguageVersion = "0.1.0";

/// The name of the attribute used to mark an external function as trusted
/// (i.e. allowed to be called from safe C+ code without extra checks).
inline constexpr const char *kTrustedAttr = "cplus_trusted";

/// The name of the attribute used to opt a function out of RAII cleanup,
/// e.g. for functions that longjmp out of the caller's frame.
inline constexpr const char *kNoCleanupAttr = "cplus_no_cleanup";

} // namespace cplus

#endif // CPLUS_LANGFEATURES_H
