//===- Main.cpp - C+ compiler driver -----------------------------*- C++ -*-===//
//
// The `c+` command line driver.
//
//   c+ --version
//   c+ build  main.c            [--release|--debug] [--legacy-c]
//   c+ run    main.c            [--release|--debug] [--legacy-c]
//   c+ check  main.c            [--release|--debug] [--legacy-c]
//   c+ test   [pattern]
//   c+ bench  [pattern]
//
//===----------------------------------------------------------------------===//

#include "Diagnostics.h"
#include "LangFeatures.h"
#include "Sema/SemaCPlus.h"

#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/TextDiagnosticBuffer.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
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
  std::string output;
  Mode mode = Mode::Safe;
  OptMode opt = OptMode::Debug;
  bool verbose = false;
  bool warningsAsErrors = false;
  std::string stdVersion = "c17";
  std::vector<std::string> extraArgs;
};

void printVersion(raw_ostream &os) {
  os << "C+ " << kLanguageVersion << " (compiler " << kLanguageVersion
     << ")\n";
  os << "Language: C+ (C with memory safety, deterministic RAII, lightweight "
        "classes)\n";
  os << "Backend: Clang/LLVM\n";
}

void printUsage(raw_ostream &os) {
  os << "usage: c+ <command> [options] <file>\n\n";
  os << "commands:\n";
  os << "  build <file.c>   compile a C+ source file to a native executable\n";
  os << "  run   <file.c>   compile and run the resulting executable\n";
  os << "  check <file.c>   parse and analyse without producing an executable\n";
  os << "  test  [pattern]  run the automated test suite\n";
  os << "  bench [pattern]  run the benchmark suite\n";
  os << "  --version        print version information\n";
  os << "  --help           print this message\n\n";
  os << "options:\n";
  os << "  --release        optimise for runtime performance (safety stays on)\n";
  os << "  --debug          optimise for compile speed and diagnostics\n";
  os << "  --legacy-c       legacy C compatibility mode (not memory safe)\n";
  os << "  -o <file>        output path\n";
  os << "  -v/--verbose      verbose diagnostics\n";
}

std::vector<std::string> buildArgs(const Options &opts, const std::string &file,
                                   bool syntaxOnly) {
  std::vector<std::string> args;
  args.push_back("-x");
  args.push_back("c");
  args.push_back("-std=" + opts.stdVersion);
  args.push_back("-fno-common");

  if (opts.mode == Mode::Safe) {
    // Safety instrumentation is on in both modes of the C+ compiler; the
    // difference is which checks are emitted and whether unsafe constructs
    // are rejected.
    args.push_back("-fcplus-safe");
  } else {
    args.push_back("-fcplus-legacy");
  }

  if (opts.opt == OptMode::Release) {
    args.push_back("-O2");
    args.push_back("-fno-omit-frame-pointer");
  } else {
    args.push_back("-O0");
    args.push_back("-g");
  }

  if (opts.warningsAsErrors)
    args.push_back("-Werror");
  if (opts.verbose)
    args.push_back("-v");

  for (const auto &a : opts.extraArgs)
    args.push_back(a);

  if (syntaxOnly) {
    args.push_back("-fsyntax-only");
  } else {
    args.push_back("-o");
    args.push_back(opts.output);
  }
  args.push_back(file);
  return args;
}

} // namespace

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);

  Options opts;
  std::vector<std::string> positional;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--version" || a == "-v" || a == "--help" || a == "-h") {
      if (a == "--version") {
        printVersion(outs());
        return 0;
      }
      printUsage(outs());
      return 0;
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
    if (a == "-o") {
      if (i + 1 < argc)
        opts.output = argv[++i];
      continue;
    }
    if (a == "--verbose") {
      opts.verbose = true;
      continue;
    }
    if (a.size() > 1 && a[0] == '-' && a != "-") {
      opts.extraArgs.push_back(a);
      continue;
    }
    positional.push_back(a);
  }

  if (positional.empty()) {
    printUsage(errs());
    return 1;
  }

  opts.command = positional[0];
  if (positional.size() > 1)
    opts.input = positional[1];

  if (opts.command == "build" || opts.command == "run" || opts.command == "check") {
    if (opts.input.empty()) {
      errs() << "error: " << opts.command << " requires an input file\n";
      return 1;
    }
    if (opts.output.empty()) {
      std::string base = opts.input;
      size_t dot = base.find_last_of('.');
      if (dot != std::string::npos)
        base = base.substr(0, dot);
      opts.output = base;
    }

    // Rewrite C+ syntax into annotated C.
    DiagnosticEngine diags;
    std::string err;
    std::string source;
    {
      auto buf = llvm::MemoryBuffer::getFile(opts.input);
      if (!buf) {
        errs() << "error: cannot read '" << opts.input
               << "': " << buf.getError().message() << "\n";
        return 1;
      }
      source = buf.get()->getBuffer().str();
    }
    RewrittenSource rewritten = rewriteCPlusSyntax(source, diags, opts.input);
    for (const auto &d : diags.diags()) {
      errs() << (d.level == DiagLevel::Error ? "error: " : "warning: ")
             << d.message << "\n";
    }
    if (diags.hasErrors())
      return 1;

    // Write the rewritten source to a temporary file next to the original so
    // that relative includes still resolve.
    SmallString<128> tmpPath;
    sys::path::system_temp_directory(/*erasedOnReboot=*/true, tmpPath);
    sys::path::append(tmpPath, "cplus_rewritten.c");
    std::error_code ec;
    raw_fd_ostream os(tmpPath, ec, sys::fs::OF_Text);
    if (ec) {
      errs() << "error: cannot create temporary file: " << ec.message() << "\n";
      return 1;
    }
    os << rewritten.text;
    os.close();

    std::vector<std::string> args = buildArgs(opts, tmpPath.str().str(),
                                              opts.command == "check");

    auto db = std::make_unique<clang::tooling::FixedCompilationDatabase>(".", args);
    clang::tooling::ClangTool tool(*db, {opts.input});
    tool.appendArgumentsAdjuster(
        [](const clang::tooling::CommandLineArguments &a, StringRef) {
          clang::tooling::CommandLineArguments out(a.begin(), a.end());
          out.erase(std::remove(out.begin(), out.end(), "-o"),
                    out.end());
          return out;
        });

    // Replace the file argument with the rewritten temp file.
    tool.appendArgumentsAdjuster(
        [&](const clang::tooling::CommandLineArguments &a, StringRef) {
          clang::tooling::CommandLineArguments out(a.begin(), a.end());
          for (auto &x : out)
            if (x == tmpPath.str().str())
              x = opts.input;
          return out;
        });

    int rc = tool.run(clang::tooling::newFrontendActionFactory<clang::SyntaxOnlyAction>()
                          .get());
    if (rc != 0)
      return rc;

    if (opts.command == "check") {
      outs() << "check: " << opts.input << " ok\n";
      return 0;
    }
    return 0;
  }

  errs() << "error: unknown command '" << opts.command << "'\n";
  printUsage(errs());
  return 1;
}
