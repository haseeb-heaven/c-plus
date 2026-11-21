//===- Driver.cpp - C+ compile pipeline -------------------------*- C++ -*-===//

#include "Driver/Driver.h"

#include "Sema/CPlusRewriter.h"
#include "Sema/RaiiLowering.h"
#include "Sema/StaticBoundsCheck.h"
#include "Sema/AstSafetyCheck.h"

#include "cplus/clog.h"
#undef TRACE
#undef DEBUG
#undef INFO
#undef WARN
#undef ERROR
#undef FATAL

#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <system_error>
#include <unistd.h>

namespace cplus {

namespace {

static bool g_logEnabled = false;
static int g_logLevel = 3; // CLOG_WARN

static int logLevelFromString(const std::string &name) {
  if (name == "trace") return 0;
  if (name == "debug") return 1;
  if (name == "info") return 2;
  if (name == "warn" || name == "warning") return 3;
  if (name == "error") return 4;
  if (name == "fatal") return 5;
  return 3;
}

#define DRIVER_LOG(level, ...)                                                 \
  do {                                                                         \
    if (g_logEnabled && (level) >= g_logLevel)                                  \
      clog_log((clog_level_t)(level), __FILE__, __LINE__, __func__,             \
               __VA_ARGS__);                                                   \
  } while (0)

std::string envOr(const char *name) {
  const char *v = std::getenv(name);
  return v ? std::string(v) : std::string();
}

bool fileExists(const std::string &path) {
  return !path.empty() && llvm::sys::fs::exists(path) &&
         !llvm::sys::fs::is_directory(path);
}

std::string joinPath(const std::string &a, const std::string &b) {
  if (a.empty())
    return b;
  return a + "/" + b;
}

/// Locate a file relative to the running executable (installed layout) or
/// the source tree (development layout).
std::string findByLayout(const std::string &relative) {
  std::string exe = llvm::sys::fs::getMainExecutable("c+", nullptr);
  if (!exe.empty()) {
    std::string dir = llvm::sys::path::parent_path(exe).str();
    for (const char *up : {"", "../", "../../"}) {
      std::string candidate = joinPath(dir, up) + relative;
      if (llvm::sys::fs::exists(candidate))
        return candidate;
    }
  }
  std::string root = findSourceRoot();
  if (!root.empty()) {
    std::string candidate = joinPath(root, relative);
    if (llvm::sys::fs::exists(candidate))
      return candidate;
  }
  return std::string();
}

int runProgram(const std::string &program,
               const std::vector<std::string> &args) {
  std::vector<llvm::StringRef> argv;
  argv.reserve(args.size() + 1);
  argv.push_back(program);
  for (const auto &a : args)
    argv.push_back(a);

  std::string err;
  int rc = llvm::sys::ExecuteAndWait(program, argv, std::nullopt, {}, 0, 0,
                                     &err);
  if (rc < 0) {
    llvm::errs() << "error: failed to execute '" << program << "'";
    if (!err.empty())
      llvm::errs() << ": " << err;
    llvm::errs() << "\n";
    return 127;
  }
  return rc;
}

struct TempFile {
  std::string path;
  bool keep = false;
  TempFile() = default;
  TempFile(const TempFile &) = delete;
  TempFile &operator=(const TempFile &) = delete;
  TempFile(TempFile &&other) noexcept : path(std::move(other.path)), keep(other.keep) {
    other.path.clear();
  }
  TempFile &operator=(TempFile &&other) noexcept {
    if (this != &other) {
      path = std::move(other.path);
      keep = other.keep;
      other.path.clear();
    }
    return *this;
  }
  ~TempFile() {
    if (!keep && !path.empty())
      llvm::sys::fs::remove(path);
  }
};

/// A lowered compilation unit plus the temp file that holds it.
struct LoweredUnit {
  std::string text;
  std::string tempPath;
  bool ok = false;
};

bool writeFile(const std::string &path, const std::string &text,
               std::string *err) {
  std::error_code ec;
  llvm::raw_fd_ostream os(path, ec, llvm::sys::fs::OF_Text);
  if (ec) {
    *err = ec.message();
    return false;
  }
  os << text;
  os.close();
  return true;
}

std::string readFile(const std::string &path, std::string *err) {
  auto buf = llvm::MemoryBuffer::getFile(path);
  if (!buf) {
    *err = buf.getError().message();
    return std::string();
  }
  return std::string((*buf)->getBuffer());
}

std::string defaultOutputName(const std::string &input) {
  llvm::StringRef path(input);
  llvm::StringRef stem = llvm::sys::path::filename(path);
  size_t dot = stem.rfind('.');
  if (dot != llvm::StringRef::npos && dot != 0)
    stem = stem.substr(0, dot);
  std::string result = stem.str();
  if (result.empty())
    result = "a.out";
  return result;
}

std::vector<std::string> cplusIncludeArgs() {
  std::vector<std::string> args;
  std::string inc = findCPlusIncludeDir();
  if (!inc.empty()) {
    // Add the include directory only.  Auto-including cplus.h would inject a
    // `print` macro into unrelated C code and collide with user functions.
    args.push_back("-I" + inc);
  }
  return args;
}

std::string runtimeLibArg() {
  std::string lib = findCPlusRuntimeLib();
  if (lib.empty())
    return std::string();
  return lib;
}

} // namespace

std::string findSourceRoot() {
  std::string env = envOr("CPLUS_ROOT");
  if (!env.empty() && llvm::sys::fs::exists(joinPath(env, "CMakeLists.txt")))
    return env;
#ifdef CPLUS_SOURCE_DIR
  if (llvm::sys::fs::exists(joinPath(CPLUS_SOURCE_DIR, "CMakeLists.txt")))
    return CPLUS_SOURCE_DIR;
#endif
  std::string exe = llvm::sys::fs::getMainExecutable("c+", nullptr);
  if (!exe.empty()) {
    std::string dir = llvm::sys::path::parent_path(exe).str();
    for (const char *up : {"", "../", "../../"}) {
      std::string candidate = joinPath(dir, up);
      if (llvm::sys::fs::exists(joinPath(candidate, "tests/run_tests.sh")))
        return candidate;
    }
  }
  return std::string();
}

std::string findClangBinary() {
  std::string env = envOr("CPLUS_CLANG");
  if (!env.empty() && fileExists(env))
    return env;
  if (auto found = llvm::sys::findProgramByName("clang")) {
    // Prefer the host system compiler: it is the one whose sanitizer runtime
    // and SDK configuration are known to match the host.
    std::string p = *found;
    if (p.find("/opt/homebrew/Cellar/llvm/") == std::string::npos)
      return p;
  }
  for (const char *candidate : {"/usr/bin/clang", "/usr/local/bin/clang"}) {
    if (fileExists(candidate))
      return candidate;
  }
#ifdef CPLUS_CLANG_BIN
  if (fileExists(CPLUS_CLANG_BIN))
    return CPLUS_CLANG_BIN;
#endif
  return "clang";
}

std::string findCPlusIncludeDir() {
  std::string env = envOr("CPLUS_INCLUDE");
  if (!env.empty() && llvm::sys::fs::exists(joinPath(env, "cplus/cplus.h")))
    return env;
  std::string found = findByLayout("runtime/include");
  if (!found.empty() && llvm::sys::fs::exists(joinPath(found, "cplus/cplus.h")))
    return found;
  return std::string();
}

std::string findCPlusRuntimeLib() {
  std::string env = envOr("CPLUS_RUNTIME_LIB");
  if (fileExists(env))
    return env;
#ifdef CPLUS_RUNTIME_LIB
  if (fileExists(CPLUS_RUNTIME_LIB))
    return CPLUS_RUNTIME_LIB;
#endif
  for (const char *relative :
       {"build/libcplus_runtime.a", "lib/libcplus_runtime.a",
        "libcplus_runtime.a"}) {
    std::string found = findByLayout(relative);
    if (!found.empty())
      return found;
  }
  return std::string();
}

void printDiagnostics(const DiagnosticEngine &diags, llvm::raw_ostream &os,
                      const std::string &inputName) {
  for (const auto &d : diags.diags()) {
    const char *level = "error";
    switch (d.level) {
    case DiagLevel::Note:
      level = "note";
      break;
    case DiagLevel::Warning:
      level = "warning";
      break;
    case DiagLevel::Error:
      level = "error";
      break;
    case DiagLevel::Fatal:
      level = "fatal error";
      break;
    }
    const std::string &file = d.file.empty() ? inputName : d.file;
    if (!file.empty()) {
      os << file;
      if (d.line)
        os << ":" << d.line;
      if (d.column)
        os << ":" << d.column;
      os << ": ";
    }
    DRIVER_LOG(CLOG_WARN, "diagnostic %s %s:%u: %s", level, file.c_str(),
               d.line, d.message.c_str());
    os << level << ": " << d.message << "\n";
    if (!d.help.empty())
      os << "help: " << d.help << "\n";
    for (const auto &n : d.notes) {
      os << "note: " << n.message << "\n";
    }
  }
}

/// Lower one translation unit (source file or header) to plain C.
std::string lowerUnit(const std::string &text, const std::string &name,
                      DiagnosticEngine &diags, Mode mode, bool *ok,
                      RewrittenSource *outRewritten = nullptr,
                      bool headerMode = false, bool strict = false) {
  RewrittenSource rewritten = rewriteCPlusSource(text, diags, name, headerMode);
  if (mode == Mode::Safe)
    checkStaticBounds(text, rewritten.arrayLengths, diags, name);
  LoweredSource lowered =
      lowerRaii(rewritten.text, rewritten.resourceTypes, diags, name, mode,
                strict);
  if (outRewritten)
    *outRewritten = rewritten;
  *ok = !diags.hasErrors();
  return lowered.text;
}

std::string resolveInclude(const std::string &name,
                           const std::string &currentPath) {
  if (name.empty() || llvm::sys::path::is_absolute(name))
    return fileExists(name) ? name : std::string();

  std::vector<std::string> dirs;
  llvm::StringRef parent = llvm::sys::path::parent_path(currentPath);
  if (!parent.empty())
    dirs.push_back(parent.str());
  dirs.push_back(".");
  std::string envInc = envOr("CPLUS_INCLUDE");
  if (!envInc.empty())
    dirs.push_back(envInc);
  std::string autoInc = findCPlusIncludeDir();
  if (!autoInc.empty())
    dirs.push_back(autoInc);
  std::string root = findSourceRoot();
  if (!root.empty()) {
    dirs.push_back(root);
    dirs.push_back(root + "/runtime/include");
  }

  for (const auto &dir : dirs) {
    std::string candidate = joinPath(dir, name);
    if (fileExists(candidate))
      return candidate;
  }
  return std::string();
}

/// Replace `#include "x.hp"` with an absolute path to a lowered `.h` copy.
/// This lets modern C+ headers use the same syntax as `.cp` sources while
/// ordinary `.h` headers keep working unchanged.
std::string preprocessModernHeaders(
    const std::string &source, const std::string &currentPath,
    DiagnosticEngine &diags, Mode mode, std::vector<TempFile> &guards,
    std::map<std::string, std::string> &processed, unsigned &counter) {
  std::string result;
  std::istringstream in(source);
  std::string line;
  bool first = true;
  while (std::getline(in, line)) {
    if (!first)
      result += "\n";
    first = false;

    size_t hash = line.find("#include");
    if (hash != std::string::npos) {
      size_t q1 = line.find('"', hash);
      size_t q2 = q1 == std::string::npos ? std::string::npos
                                          : line.find('"', q1 + 1);
      if (q1 != std::string::npos && q2 != std::string::npos) {
        std::string inc = line.substr(q1 + 1, q2 - q1 - 1);
        if (llvm::StringRef(inc).ends_with(".hp")) {
          std::string path = resolveInclude(inc, currentPath);
          if (path.empty()) {
            Diag d;
            d.level = DiagLevel::Error;
            d.message = "cannot find C+ header '" + inc + "'";
            d.file = currentPath;
            diags.report(std::move(d));
          } else {
            auto known = processed.find(path);
            std::string tempPath =
                known == processed.end() ? std::string() : known->second;
            if (tempPath.empty()) {
              std::string err;
              std::string header = readFile(path, &err);
              if (!err.empty()) {
                Diag d;
                d.level = DiagLevel::Error;
                d.message = "cannot read '" + path + "': " + err;
                diags.report(std::move(d));
              } else {
                header = preprocessModernHeaders(header, path, diags, mode,
                                                 guards, processed, counter);
                bool ok = false;
                RewrittenSource headerInfo;
                std::string lowered = lowerUnit(header, path, diags, mode, &ok,
                                                &headerInfo, true);
                for (const auto &cls : headerInfo.classTypes)
                  registerExternalClass(
                      cls, lowered.find(cls + "_ctor(") != std::string::npos);
                for (const auto &res : headerInfo.resourceTypes)
                  registerExternalResource(res);
                llvm::SmallString<256> tmp;
                llvm::sys::path::system_temp_directory(true, tmp);
                llvm::sys::path::append(
                    tmp, "cplus-hp-" + std::to_string(::getpid()) + "-" +
                             std::to_string(counter++) + ".h");
                tempPath = tmp.str().str();
                if (!writeFile(tempPath, lowered, &err)) {
                  Diag d;
                  d.level = DiagLevel::Error;
                  d.message = "cannot write lowered header: " + err;
                  diags.report(std::move(d));
                  tempPath.clear();
                } else {
                  guards.emplace_back();
                  guards.back().path = tempPath;
                  guards.back().keep = false;
                  processed[path] = tempPath;
                }
              }
            }
            if (!tempPath.empty())
              line.replace(q1, q2 - q1 + 1, "\"" + tempPath + "\"");
          }
        }
      }
    }
    result += line;
  }
  return result;
}

static std::vector<std::string> backendArgs(const DriverOptions &opts) {
  std::vector<std::string> args;
  args.push_back(opts.mode == Mode::LegacyC ? "-std=gnu89" : "-std=gnu17");
  args.push_back("-fno-common");
  args.push_back("-Wno-unused-parameter");
  args.push_back("-DCPLUS=1");
  if (opts.opt == OptMode::Release)
    args.push_back("-O2");
  else {
    args.push_back("-O0");
    args.push_back("-g");
  }
  if (opts.mode == Mode::Safe) {
    args.push_back("-pthread");
    if (!opts.disableWarnings) {
      args.push_back("-Werror=array-bounds");
      args.push_back("-Werror=return-stack-address");
      args.push_back("-Werror=uninitialized");
      args.push_back("-Wwrite-strings");
      args.push_back("-Werror=sizeof-array-argument");
      args.push_back("-Werror=sizeof-pointer-div");
      args.push_back("-Werror=format-security");
      args.push_back("-Werror=unsequenced");
      args.push_back("-Werror=shift-count-overflow");
      args.push_back("-Werror=shift-negative-value");
      args.push_back("-Werror=varargs");
    } else {
      args.push_back("-w");
    }
    args.push_back("-fsanitize=undefined,bounds,null,alignment");
    args.push_back("-fno-sanitize-recover=all");
    args.push_back("-fno-omit-frame-pointer");
    args.push_back("-DCPLUS_SAFE=1");
  } else {
    args.push_back("-DCPLUS_LEGACY_C=1");
  }
  if (opts.warningsAsErrors)
    args.push_back("-Werror");
  if (opts.verbose)
    args.push_back("-v");
  for (const auto &inc : cplusIncludeArgs())
    args.push_back(inc);
  if (opts.logging) {
    args.push_back("-DCPLUS_LOG_RUNTIME=1");
    args.push_back("-DCPLUS_LOG_LEVEL=" +
                   std::to_string(logLevelFromString(opts.logLevel)));
  }
  return args;
}

int runDriver(const DriverOptions &opts, llvm::raw_ostream &os,
              llvm::raw_ostream &errs) {
  g_logEnabled = opts.logging;
  g_logLevel = logLevelFromString(opts.logLevel);
  DRIVER_LOG(CLOG_INFO, "c+ driver start: command=%s files=%zu",
             opts.command.c_str(), opts.inputs.size());
  std::vector<std::string> inputs = opts.inputs.empty()
                                        ? std::vector<std::string>{opts.input}
                                        : opts.inputs;
  if (inputs.empty()) {
    errs << "error: no input files\n";
    return 1;
  }

  const std::string clang = findClangBinary();
  std::string output = opts.output;
  if (output.empty() && !opts.preprocessOnly) {
    if (opts.compileOnly)
      output = defaultOutputName(inputs[0]) + ".o";
    else if (opts.assemblyOnly)
      output = defaultOutputName(inputs[0]) + ".s";
    else
      output = defaultOutputName(inputs[0]);
  }
  std::vector<std::string> base = backendArgs(opts);
  std::vector<std::string> objects;
  std::vector<TempFile> guards;
  std::string pidSuffix = std::to_string(::getpid());

  for (size_t fileIndex = 0; fileIndex < inputs.size(); ++fileIndex) {
    const std::string &input = inputs[fileIndex];
    clearExternalClasses();
    DiagnosticEngine diags;
    std::string err;
    std::string source = readFile(input, &err);
    if (!err.empty()) {
      errs << "error: cannot read '" << input << "': " << err << "\n";
      return 1;
    }

    std::vector<TempFile> headerGuards;
    std::map<std::string, std::string> processedHeaders;
    unsigned headerCounter = 0;
    source = preprocessModernHeaders(source, input, diags, opts.mode,
                                     headerGuards, processedHeaders,
                                     headerCounter);

    DRIVER_LOG(CLOG_DEBUG, "lowering %s", input.c_str());
    bool ok = false;
    std::string loweredText = lowerUnit(source, input, diags, opts.mode, &ok,
                                        nullptr, false, opts.strict);
    DRIVER_LOG(CLOG_DEBUG, "lowered %s (%zu bytes)", input.c_str(),
               loweredText.size());
    if (opts.mode == Mode::Safe)
      checkAstSafety(loweredText, input, base, diags, opts.strict);
    printDiagnostics(diags, errs, input);
    if (diags.hasErrors())
      return 1;
    if (opts.warningsAsErrors && diags.warningCount() > 0)
      return 1;

    if (fileIndex == 0 && !opts.emitC.empty()) {
      if (!writeFile(opts.emitC, loweredText, &err)) {
        errs << "error: cannot write '" << opts.emitC << "': " << err << "\n";
        return 1;
      }
      if (opts.verbose)
        os << "lowered C written to " << opts.emitC << "\n";
    }

    llvm::SmallString<256> tempPath;
    llvm::StringRef inputDir = llvm::sys::path::parent_path(input);
    if (inputDir.empty())
      inputDir = ".";
    tempPath = std::string(inputDir);
    llvm::sys::path::append(tempPath, ".cplus-tmp-" + pidSuffix + "-" +
                                          std::to_string(fileIndex) + ".c");
    if (!writeFile(tempPath.str().str(), loweredText, &err)) {
      llvm::SmallString<256> sysTemp;
      llvm::sys::path::system_temp_directory(true, sysTemp);
      llvm::sys::path::append(sysTemp, "cplus-" + pidSuffix + "-" +
                                           std::to_string(fileIndex) + ".c");
      tempPath = sysTemp;
      if (!writeFile(tempPath.str().str(), loweredText, &err)) {
        errs << "error: cannot write temporary file: " << err << "\n";
        return 1;
      }
    }
    TempFile guard;
    guard.path = tempPath.str().str();
    guard.keep = opts.keepTemp || !envOr("CPLUS_KEEP_TEMP").empty();
    if (guard.keep)
      os << "note: lowered C kept at " << guard.path << "\n";
    guards.push_back(std::move(guard));

    std::vector<std::string> args = base;
    args.push_back("-I" + std::string(inputDir));
    for (const auto &a : opts.extraArgs)
      args.push_back(a);
    if (opts.command == "check") {
      args.push_back("-fsyntax-only");
    } else if (opts.preprocessOnly) {
      args.push_back("-E");
      if (!output.empty()) {
        args.push_back("-o");
        args.push_back(output);
      }
    } else if (opts.assemblyOnly) {
      args.push_back("-S");
      args.push_back("-o");
      args.push_back(output);
    } else if (opts.compileOnly) {
      args.push_back("-c");
      args.push_back("-o");
      args.push_back(output);
    } else {
      llvm::SmallString<256> objPath;
      llvm::sys::path::system_temp_directory(true, objPath);
      llvm::sys::path::append(objPath, "cplus-" + pidSuffix + "-" +
                                           std::to_string(fileIndex) + ".o");
      args.push_back("-c");
      args.push_back("-o");
      args.push_back(objPath.str().str());
      objects.push_back(objPath.str().str());
    }
    args.push_back(tempPath.str().str());

    if (opts.verbose) {
      os << "backend: " << clang << "\n";
      for (const auto &a : args)
        os << "  " << a << "\n";
    }
    DRIVER_LOG(CLOG_INFO, "compile %s", input.c_str());
    int rc = runProgram(clang, args);
    if (rc != 0) {
      DRIVER_LOG(CLOG_ERROR, "compile failed: %s rc=%d", input.c_str(), rc);
      return rc;
    }
    if (opts.command == "check")
      os << "check: " << input << " ok\n";
  }

  if (opts.command == "check" || opts.compileOnly || opts.preprocessOnly ||
      opts.assemblyOnly)
    return 0;

  std::vector<std::string> linkArgs = base;
  for (const auto &obj : objects)
    linkArgs.push_back(obj);
  std::string runtime = runtimeLibArg();
  if (!runtime.empty())
    linkArgs.push_back(runtime);
  for (const auto &a : opts.extraArgs)
    linkArgs.push_back(a);
  linkArgs.push_back("-o");
  linkArgs.push_back(output);

  DRIVER_LOG(CLOG_INFO, "link %s", output.c_str());
  int rc = runProgram(clang, linkArgs);
  if (rc != 0) {
    DRIVER_LOG(CLOG_ERROR, "link failed rc=%d", rc);
    return rc;
  }

  if (opts.command == "run") {
    std::string exe = output;
    if (exe.find('/') == std::string::npos)
      exe = "./" + exe;
    return runProgram(exe, {});
  }
  if (opts.verbose)
    os << "built " << output << "\n";
  return 0;
}

} // namespace cplus
