//===- Main.cpp - C+ compiler driver -----------------------------*- C++ -*-===//
//
// The `c+` command line driver.
//
//   c+ --version
//   c+ build  main.cp           [--release|--debug] [--legacy-c]
//   c+ run    main.c            [--release|--debug] [--legacy-c]
//   c+ check  main.c            [--release|--debug] [--legacy-c]
//   c+ test   [pattern]
//   c+ bench  [pattern]
//
//===----------------------------------------------------------------------===//

#include "Driver/Driver.h"
#include "LangFeatures.h"

#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"

#include <string>
#include <vector>

using namespace cplus;
using namespace llvm;

namespace {

struct Options {
  std::string command;
  std::string input;
  std::vector<std::string> inputs;
  std::string output;
  std::string emitC;
  Mode mode = Mode::Safe;
  OptMode opt = OptMode::Debug;
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

void printVersion(raw_ostream &os) {
  os << kLanguageName << " " << kLanguageVersion << "\n";
  os << "Language: C+ (C with memory safety, deterministic RAII, lightweight "
        "classes)\n";
  os << "Backend: host Clang/LLVM\n";
}

void printUsage(raw_ostream &os) {
  os << "usage: c+ <command> [options] <file>\n\n";
  os << "extensions: .cp/.hp recommended for modern C+; .c/.h remain supported\n\n";
  os << "commands:\n";
  os << "  build <file.c>   lower C+ and compile it to a native executable\n";
  os << "  run   <file.c>   build, then run the executable\n";
  os << "  check <file.c>   lower and type-check without writing an executable\n";
  os << "  test  [pattern]  run the automated test suite\n";
  os << "  bench [pattern]  run the benchmark suite\n";
  os << "  demos            build and run the demo programs\n";
  os << "  --version        print version information\n";
  os << "  --help           print this message\n\n";
  os << "options:\n";
  os << "  --release        -O2 (runtime safety checks stay on)\n";
  os << "  --debug          -O0 plus debug info (default)\n";
  os << "  --legacy-c       legacy C compatibility mode (NOT memory safe)\n";
  os << "  -o <file>        output path\n";
  os << "  --emit-c <file>  write the lowered C source for inspection\n";
  os << "  --keep-temp      keep the generated C file\n";
  os << "  -Werror          turn C+ warnings into errors\n";
  os << "  --verbose        show the backend command line\n";
  os << "  --log            enable compiler/runtime logging (off by default)\n";
  os << "  --log-level LVL  trace|debug|info|warn|error|fatal (default warn)\n";
  os << "  -w               disable warnings (gcc compatible)\n";
  os << "  -c / -S / -E     object / assembly / preprocess (gcc compatible)\n";
  os << "  -O2 -g -std=...  passed through to the C backend\n";
  os << "  c+ file.c -o f   gcc-style invocation (no subcommand needed)\n";
  os << "  --               pass the remaining arguments to the C compiler\n";
}

bool runScript(const std::string &relative, const std::string &pattern,
               bool bench) {
  std::string root = findSourceRoot();
  if (root.empty()) {
    errs() << "error: cannot locate the C+ source tree (set CPLUS_ROOT)\n";
    return false;
  }
  std::string script = root + "/" + relative;
  if (!llvm::sys::fs::exists(script)) {
    errs() << "error: " << script << " not found\n";
    return false;
  }
  std::vector<std::string> args;
  // Prefer the host shell; the suite is deliberately plain POSIX sh.
  std::string shell = "/bin/sh";
  std::vector<StringRef> argv{shell, script};
  if (!pattern.empty())
    argv.push_back(pattern);
  int rc = llvm::sys::ExecuteAndWait(shell, argv);
  (void)bench;
  return rc == 0;
}

} // namespace

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);

  Options opts;
  std::vector<std::string> positional;
  bool passThrough = false;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (passThrough) {
      opts.extraArgs.emplace_back(a);
      continue;
    }
    if (a == "--version" || a == "-V") {
      printVersion(outs());
      return 0;
    }
    if (a == "--help" || a == "-h") {
      printUsage(outs());
      return 0;
    }
    if (a == "--") {
      passThrough = true;
      continue;
    }
    if (a == "--release") {
      opts.opt = OptMode::Release;
      continue;
    }
    if (a == "--debug") {
      opts.opt = OptMode::Debug;
      continue;
    }
    if (a == "--legacy-c" || a == "--legacy") {
      opts.mode = Mode::LegacyC;
      continue;
    }
    if (a == "-Werror") {
      opts.warningsAsErrors = true;
      continue;
    }
    if (a == "--verbose" || a == "-v") {
      opts.verbose = true;
      continue;
    }
    // gcc/g++ compatible short options
    if (a == "-w") {
      opts.disableWarnings = true;
      continue;
    }
    if (a == "-c") {
      opts.compileOnly = true;
      continue;
    }
    if (a == "-E") {
      opts.preprocessOnly = true;
      continue;
    }
    if (a == "-S") {
      opts.assemblyOnly = true;
      continue;
    }
    if (a == "--log") {
      opts.logging = true;
      continue;
    }
    if (a == "--log-level") {
      if (i + 1 < argc)
        opts.logLevel = argv[++i];
      continue;
    }
    if (a == "--keep-temp") {
      opts.keepTemp = true;
      continue;
    }
    if (a == "-o") {
      if (i + 1 < argc)
        opts.output = argv[++i];
      continue;
    }
    // gcc-style two-token options: -I dir, -D NAME, -U NAME, -L dir, -l lib.
    if ((a == "-I" || a == "-D" || a == "-U" || a == "-L" || a == "-l") &&
        i + 1 < argc) {
      opts.extraArgs.emplace_back(a);
      opts.extraArgs.emplace_back(argv[++i]);
      continue;
    }
    if (a == "--emit-c") {
      if (i + 1 < argc)
        opts.emitC = argv[++i];
      continue;
    }
    // Pass through common gcc/g++ options unchanged.
    if (a.rfind("-I", 0) == 0 || a.rfind("-D", 0) == 0 ||
        a.rfind("-U", 0) == 0 || a.rfind("-l", 0) == 0 ||
        a.rfind("-L", 0) == 0 || a.rfind("-O", 0) == 0 ||
        a.rfind("-std=", 0) == 0 || a.rfind("-W", 0) == 0 ||
        a.rfind("-f", 0) == 0 || a == "-pthread" || a == "-shared" ||
        a == "-pedantic" || a == "-ansi" || a == "-pipe") {
      opts.extraArgs.emplace_back(a);
      continue;
    }
    if (a.size() > 1 && a[0] == '-' && a != "-") {
      opts.extraArgs.emplace_back(a);
      continue;
    }
    positional.emplace_back(a);
  }

  if (positional.empty()) {
    printUsage(errs());
    return 1;
  }

  auto isKnownCommand = [](const std::string &s) {
    return s == "build" || s == "run" || s == "check" || s == "test" ||
           s == "bench" || s == "demos";
  };
  auto looksLikeSource = [](const std::string &s) {
    static const char *const exts[] = {".c",  ".cp",  ".cpp", ".cc",
                                       ".cxx", ".C",  ".h",   ".hp"};
    for (const char *ext : exts)
      if (s.size() >= std::string(ext).size() &&
          s.compare(s.size() - std::string(ext).size(),
                    std::string(ext).size(), ext) == 0)
        return true;
    return s.find('/') != std::string::npos;
  };
  if (!isKnownCommand(positional[0]) && looksLikeSource(positional[0]))
    positional.insert(positional.begin(), "build");

  opts.command = positional[0];
  for (size_t i = 1; i < positional.size(); ++i)
    opts.inputs.push_back(positional[i]);
  if (!opts.inputs.empty())
    opts.input = opts.inputs[0];

  if (opts.command == "test" || opts.command == "bench" ||
      opts.command == "demos") {
    const char *script = opts.command == "test"    ? "tests/run_tests.sh"
                         : opts.command == "bench" ? "benchmarks/run_bench.sh"
                                                   : "demos/run_demos.sh";
    return runScript(script, opts.input, opts.command == "bench") ? 0 : 1;
  }

  if (opts.command != "build" && opts.command != "run" &&
      opts.command != "check") {
    errs() << "error: unknown command '" << opts.command << "'\n";
    printUsage(errs());
    return 1;
  }

  if (opts.input.empty()) {
    errs() << "error: " << opts.command << " requires an input file\n";
    return 1;
  }

  if (opts.output.empty())
    opts.output.clear(); // driver picks a default

  DriverOptions d;
  d.command = opts.command;
  d.input = opts.input;
  d.inputs = opts.inputs;
  d.output = opts.output;
  d.emitC = opts.emitC;
  d.mode = opts.mode;
  d.opt = opts.opt;
  d.verbose = opts.verbose;
  d.warningsAsErrors = opts.warningsAsErrors;
  d.keepTemp = opts.keepTemp;
  d.extraArgs = opts.extraArgs;
  d.logging = opts.logging;
  d.logLevel = opts.logLevel;
  d.disableWarnings = opts.disableWarnings;
  d.compileOnly = opts.compileOnly;
  d.preprocessOnly = opts.preprocessOnly;
  d.assemblyOnly = opts.assemblyOnly;
  return runDriver(d, outs(), errs());
}
