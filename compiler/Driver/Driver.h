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

struct DriverOptions {
  std::string command; // "build", "run", "check"
  std::string input;
  std::vector<std::string> inputs;
  std::string output;
  std::string emitC; // if non-empty, write the lowered C here
  Mode mode = Mode::Safe;
  OptMode opt = OptMode::Debug;
  bool verbose = false;
  bool warningsAsErrors = false;
  bool keepTemp = false;
  bool logging = false;
  std::string logLevel = "warn";
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

/// Resolve the directory containing the C+ runtime headers.
std::string findCPlusIncludeDir();

/// Resolve the C+ runtime static library, or an empty string if unavailable.
std::string findCPlusRuntimeLib();

/// Directory that holds tests/, demos/, benchmarks/.
std::string findSourceRoot();

} // namespace cplus

#endif // CPLUS_DRIVER_H
