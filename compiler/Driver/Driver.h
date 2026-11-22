//===- Driver.h - C+ compiler driver -----------------------------*- C++ -*-===//
//
// The C+ driver orchestrates the compilation pipeline:
//
//   1. Read the .c source.
//   2. Lower C+ constructs (resource/class/managed/owned/RAII) to plain C.
//   3. Hand the lowered C to an existing C compiler + linker.
//
// We deliberately reuse the host C compiler for parsing type-checking, code
// generation, optimisation and linking instead of reimplementing them.
//
//===----------------------------------------------------------------------===//

#ifndef CPLUS_DRIVER_H
#define CPLUS_DRIVER_H

#include "Diagnostics.h"
#include "LangFeatures.h"

namespace llvm { class raw_ostream; }
#include <string>
#include <vector>

namespace cplus {

/// Runtime sanitizers selected for the generated binary. These are added to
/// BOTH the compile and the final link command lines. Values are a bitmask so
/// compatible combinations can be expressed.
enum class Sanitizer : unsigned {
  None = 0,
  Address = 1u << 0,   ///< AddressSanitizer
  Undefined = 1u << 1, ///< UndefinedBehaviorSanitizer
  Memory = 1u << 2,    ///< MemorySanitizer (Linux x86-64 only)
  Thread = 1u << 3,    ///< ThreadSanitizer
  Leak = 1u << 4,      ///< LeakSanitizer
  Cfi = 1u << 5,       ///< Control Flow Integrity (requires LTO)
  Integer = 1u << 6,   ///< unsigned overflow / suspicious conversions
};

inline Sanitizer operator|(Sanitizer a, Sanitizer b) {
  return static_cast<Sanitizer>(static_cast<unsigned>(a) |
                                static_cast<unsigned>(b));
}
inline Sanitizer operator&(Sanitizer a, Sanitizer b) {
  return static_cast<Sanitizer>(static_cast<unsigned>(a) &
                                static_cast<unsigned>(b));
}
inline bool wants(Sanitizer set, Sanitizer bit) {
  return (set & bit) != Sanitizer::None;
}
inline Sanitizer bothAsanUbsan() {
  return Sanitizer::Address | Sanitizer::Undefined;
}

struct DriverOptions {
  std::string command; // "build", "run", "check"
  std::string input;
  std::vector<std::string> inputs;
  std::string output;
  std::string emitC; // if non-empty, write the lowered C here
  Mode mode = Mode::Safe;
  OptMode opt = OptMode::Debug;
  // Safe mode defaults to UBSan; --sanitize=... overrides, --sanitize=none
  // disables all runtime sanitizers.
  Sanitizer sanitizer = Sanitizer::Undefined;
  bool sanitizerExplicit = false;
  // DEFAULT (false): report proven errors, warn on unverified operations.
  // STRICT (true): reject unverified operations rather than warn.
  bool strict = false;
  bool verbose = false;
  bool warningsAsErrors = false;
  bool keepTemp = false;
  bool logging = false;
  std::string logLevel = "warn";
  bool disableWarnings = false;
  bool compileOnly = false;
  bool preprocessOnly = false;
  bool assemblyOnly = false;
  std::vector<std::string> extraArgs;
};

/// Render collected C+ diagnostics in a stable, grep-friendly form.
void printDiagnostics(const DiagnosticEngine &diags, llvm::raw_ostream &os,
                      const std::string &inputName);

/// Run the requested driver command.  Returns a process exit code.
int runDriver(const DriverOptions &opts, llvm::raw_ostream &os,
              llvm::raw_ostream &errs);

/// Resolve the host C compiler used as the C+ backend.
std::string findClangBinary();

/// Print a short, build-free diagnostic listing every sanitizer name accepted
/// by `--sanitize=`, the disable aliases, the Safe-mode default and the backend
/// resolved by findClangBinary(). Used by `--print-sanitizers`.
void printSanitizerInfo(llvm::raw_ostream &os);

/// Resolve the directory containing the C+ runtime headers.
std::string findCPlusIncludeDir();

/// Resolve the C+ runtime static library, or an empty string if unavailable.
std::string findCPlusRuntimeLib();

/// Directory that holds tests/, demos/, benchmarks/.
std::string findSourceRoot();

} // namespace cplus

#endif // CPLUS_DRIVER_H
