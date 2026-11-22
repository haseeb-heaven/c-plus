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

#include <cctype>
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

/// Read a specific source line for diagnostic context (Clang-style snippet).
static std::string sourceLineAt(const std::string &file, unsigned line) {
  if (file.empty() || !line)
    return std::string();
  std::ifstream in(file);
  std::string s;
  unsigned n = 1;
  while (std::getline(in, s) && n < line)
    ++n;
  return n == line ? s : std::string();
}

void printDiagnostics(const DiagnosticEngine &diags, llvm::raw_ostream &os,
                      const std::string &inputName) {
  const bool color = os.is_displayed();
  auto paint = [&](llvm::raw_ostream::Colors c, bool bold = false) {
    if (color)
      os.changeColor(c, bold);
  };
  auto unpaint = [&]() {
    if (color)
      os.resetColor();
  };
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
      paint(llvm::raw_ostream::WHITE, true);
      os << file;
      unpaint();
      if (d.line)
        os << ":" << d.line;
      if (d.column)
        os << ":" << d.column;
      os << ": ";
    }
    DRIVER_LOG(CLOG_WARN, "diagnostic %s %s:%u: %s", level, file.c_str(),
               d.line, d.message.c_str());
    llvm::raw_ostream::Colors levelColor = llvm::raw_ostream::WHITE;
    if (d.level == DiagLevel::Error || d.level == DiagLevel::Fatal)
      levelColor = llvm::raw_ostream::RED;
    else if (d.level == DiagLevel::Warning)
      levelColor = llvm::raw_ostream::YELLOW;
    else if (d.level == DiagLevel::Note)
      levelColor = llvm::raw_ostream::CYAN;
    paint(levelColor, true);
    os << level;
    unpaint();
    os << ": ";
    // Highlight the [CPLUS-...] category tag.
    size_t tag = d.message.find("[CPLUS-");
    if (tag != std::string::npos) {
      size_t close = d.message.find(']', tag);
      os << d.message.substr(0, tag);
      paint(levelColor, true);
      os << d.message.substr(tag, close == std::string::npos
                                      ? std::string::npos
                                      : close - tag + 1);
      unpaint();
      os << d.message.substr(close == std::string::npos
                                 ? d.message.size() : close + 1);
    } else {
      os << d.message;
    }
    os << "\n";
    // Clang-style source snippet with a caret so the location is visible
    // without opening the file.
    if (d.line) {
      std::string src = sourceLineAt(file, d.line);
      if (!src.empty()) {
        os << "  " << src << "\n";
        if (d.column && d.column <= src.size() + 1) {
          os << "  ";
          for (unsigned k = 1; k < d.column; ++k)
            os << ' ';
          paint(levelColor, true);
          os << "^";
          size_t start = d.column - 1;
          size_t end = start;
          while (end < src.size() &&
                 (std::isalnum(static_cast<unsigned char>(src[end])) ||
                  src[end] == '_' || src[end] == '.'))
            ++end;
          for (size_t k = start + 1; k < end; ++k)
            os << "~";
          unpaint();
          os << "\n";
        }
      }
    }
    if (!d.help.empty()) {
      paint(llvm::raw_ostream::GREEN);
      os << "help: ";
      unpaint();
      os << d.help << "\n";
    }
    for (const auto &n : d.notes) {
      paint(llvm::raw_ostream::CYAN);
      os << "note: ";
      unpaint();
      os << n.message << "\n";
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

/// Locate the runtime source so it can be instrumented for sanitizer builds.
static std::string runtimeSourcePath() {
  std::string root = findSourceRoot();
  if (root.empty())
    return std::string();
  std::string candidate = joinPath(root, "runtime/cplus_runtime.c");
  return fileExists(candidate) ? candidate : std::string();
}

static Sanitizer effectiveSanitizer(const DriverOptions &opts) {
  if (!opts.sanitizerExplicit && opts.mode == Mode::LegacyC)
    return Sanitizer::None;
  return opts.sanitizer;
}

/// Return a human-readable conflict when two sanitizers cannot coexist.
static std::string sanitizerConflict(Sanitizer s) {
  if (wants(s, Sanitizer::Address) && wants(s, Sanitizer::Thread))
    return "address and thread sanitizers cannot be combined";
  if (wants(s, Sanitizer::Address) && wants(s, Sanitizer::Memory))
    return "address and memory sanitizers cannot be combined";
  if (wants(s, Sanitizer::Thread) && wants(s, Sanitizer::Memory))
    return "thread and memory sanitizers cannot be combined";
  if (wants(s, Sanitizer::Leak) &&
      (wants(s, Sanitizer::Address) || wants(s, Sanitizer::Thread) ||
       wants(s, Sanitizer::Memory)))
    return "leak sanitizer must be selected alone or with undefined/cfi/integer";
  if (wants(s, Sanitizer::Cfi) &&
      (wants(s, Sanitizer::Memory) || wants(s, Sanitizer::Thread)))
    return "cfi cannot be combined with memory or thread sanitizers";
  return std::string();
}

/// Flags for one sanitizer bit. CFI needs LTO and non-trapping mode so a
/// violation produces a diagnostic instead of a silent SIGTRAP.
static void addSanitizerFlags(std::vector<std::string> &args, Sanitizer san) {
  if (wants(san, Sanitizer::Address))
    args.push_back("-fsanitize=address");
  if (wants(san, Sanitizer::Undefined))
    args.push_back("-fsanitize=undefined,bounds,null,alignment");
  if (wants(san, Sanitizer::Memory))
    args.push_back("-fsanitize=memory");
  if (wants(san, Sanitizer::Thread)) {
    args.push_back("-fsanitize=thread");
    args.push_back("-pthread");
  }
  if (wants(san, Sanitizer::Leak))
    args.push_back("-fsanitize=leak");
  if (wants(san, Sanitizer::Cfi)) {
    args.push_back("-fsanitize=cfi");
    args.push_back("-flto");
    args.push_back("-fvisibility=hidden");
    args.push_back("-fno-sanitize-trap=cfi");
  }
  if (wants(san, Sanitizer::Integer))
    args.push_back("-fsanitize=unsigned-integer-overflow,"
                   "implicit-integer-truncation,"
                   "implicit-integer-arithmetic-value-change");
  if (san != Sanitizer::None) {
    args.push_back("-fno-sanitize-recover=all");
    args.push_back("-fno-omit-frame-pointer");
    args.push_back("-g"); // debug info for actionable stack traces
  }
}

static const char *sanitizerBitName(Sanitizer bit) {
  switch (bit) {
  case Sanitizer::Address: return "address";
  case Sanitizer::Undefined: return "undefined";
  case Sanitizer::Memory: return "memory";
  case Sanitizer::Thread: return "thread";
  case Sanitizer::Leak: return "leak";
  case Sanitizer::Cfi: return "cfi";
  case Sanitizer::Integer: return "integer";
  default: return "none";
  }
}

/// Probe source and expected diagnostic for a sanitizer bit.
struct SanProbe {
  const char *source;
  const char *needle;
};

static SanProbe probeFor(Sanitizer bit) {
  switch (bit) {
  case Sanitizer::Address:
    return {"#include <stdlib.h>\nint main(void){int*p=malloc(4);if(!p)return 1;"
            "volatile int i=4;p[i]=1;free(p);return 0;}\n",
            "ERROR: AddressSanitizer:"};
  case Sanitizer::Undefined:
    return {"int main(void){int x=2147483647;volatile int y=1;"
            "return (x+y)==0?0:1;}\n", "runtime error:"};
  case Sanitizer::Memory:
    return {"int main(void){int x; return x;}\n", "use-of-uninitialized-value"};
  case Sanitizer::Thread:
    return {"#include <pthread.h>\nstatic int x;static void*t(void*a){(void)a;"
            "x++;return 0;}\nint main(void){pthread_t a,b;"
            "pthread_create(&a,0,t,0);pthread_create(&b,0,t,0);"
            "pthread_join(a,0);pthread_join(b,0);return 0;}\n",
            "WARNING: ThreadSanitizer:"};
  case Sanitizer::Leak:
    return {"#include <stdlib.h>\nint main(void){void*p=malloc(32);(void)p;"
            "return 0;}\n", "ERROR: LeakSanitizer:"};
  case Sanitizer::Cfi:
    return {"typedef void(*F)(void);static int w(int x){return x;}"
            "int main(void){F f=(F)&w;f();return 0;}\n",
            "control flow integrity check"};
  case Sanitizer::Integer:
    return {"#include <stdint.h>\nint main(void){uint32_t x=4294967295u;"
            "volatile uint32_t o=1;return (int)(x+o);}\n",
            "unsigned integer overflow"};
  default:
    return {"", ""};
  }
}

/// Probe the selected backend for a sanitizer. Compile support AND a real
/// runtime diagnostic must both work; a successful compile alone is not
/// evidence of support. Results are cached per process.
static bool sanitizerSupported(Sanitizer bit, const std::string &clang,
                               const std::vector<std::string> &extra) {
  static std::map<std::string, bool> cache;
  std::string key = clang + "|" + std::to_string(static_cast<unsigned>(bit));
  for (const auto &a : extra)
    key += "|" + a;
  auto it = cache.find(key);
  if (it != cache.end())
    return it->second;
  SanProbe probe = probeFor(bit);
  if (!probe.source || !*probe.source)
    return cache[key] = false;
  llvm::SmallString<256> srcPath, exePath;
  if (llvm::sys::fs::createTemporaryFile("cplus_san", ".c", srcPath))
    return cache[key] = false;
  if (llvm::sys::fs::createTemporaryFile("cplus_san", ".out", exePath))
    return cache[key] = false;
  {
    std::ofstream os(srcPath.c_str());
    os << probe.source;
  }
  std::vector<std::string> flags;
  addSanitizerFlags(flags, bit);
  std::string cmd = "'" + clang + "' -std=gnu17 -g";
  for (const auto &a : extra)
    cmd += " '" + a + "'";
  for (const auto &f : flags)
    if (f.rfind("-fsanitize=", 0) == 0 || f == "-fno-sanitize-trap=cfi" ||
        f == "-flto" || f == "-fvisibility=hidden")
      cmd += " '" + f + "'";
  std::string compileLog = std::string(srcPath.str()) + ".compile.log";
  cmd += " -o '" + std::string(exePath.str()) + "' '" + std::string(srcPath.str()) +
         "' > '" + compileLog + "' 2>&1";
  int rc = system(cmd.c_str());
  if (rc != 0) {
    llvm::sys::fs::remove(srcPath);
    llvm::sys::fs::remove(compileLog);
    return cache[key] = false;
  }
  std::string runLog = std::string(exePath.str()) + ".log";
  std::string runCmd = "exec 2>/dev/null; "
                       "ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 '" +
                       std::string(exePath.str()) + "' > /dev/null 2> '" +
                       runLog + "'";
  (void)system(runCmd.c_str());
  std::ifstream log(runLog);
  std::stringstream buf;
  buf << log.rdbuf();
  std::string output = buf.str();
  llvm::sys::fs::remove(srcPath);
  llvm::sys::fs::remove(exePath);
  llvm::sys::fs::remove(compileLog);
  llvm::sys::fs::remove(runLog);
  return cache[key] = (output.find(probe.needle) != std::string::npos &&
                       output.find("CHECK failed") == std::string::npos);
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
    args.push_back("-DCPLUS_SAFE=1");
  } else {
    args.push_back("-DCPLUS_LEGACY_C=1");
  }
  // Runtime sanitizers. Flags are part of `base`, which is used for both
  // per-file compilation and the final link, so the sanitizer runtime is
  // linked into the executable as well.
  Sanitizer san = effectiveSanitizer(opts);
  addSanitizerFlags(args, san);
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
  // Validate the sanitizer selection before doing any work. Never silently
  // ignore an option or let an unsupported combination fail later at link.
  Sanitizer san = effectiveSanitizer(opts);
  if (!sanitizerConflict(san).empty()) {
    errs << "error: unsupported sanitizer combination: "
         << sanitizerConflict(san) << "\n";
    return 1;
  }
  if (opts.sanitizerExplicit) {
    static const Sanitizer kBits[] = {
        Sanitizer::Address, Sanitizer::Undefined, Sanitizer::Memory,
        Sanitizer::Thread,  Sanitizer::Leak,      Sanitizer::Cfi,
        Sanitizer::Integer};
    for (Sanitizer bit : kBits) {
      if (!wants(san, bit))
        continue;
      if (!sanitizerSupported(bit, clang, opts.extraArgs)) {
        errs << "error: sanitizer '" << sanitizerBitName(bit)
             << "' is not supported by the selected backend ("
             << clang << "); detected by a compile-and-run probe\n";
        return 1;
      }
    }
  }
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
  bool instrumentedRuntime = false;
  if (san != Sanitizer::None) {
    std::string runtimeSrc = runtimeSourcePath();
    if (runtimeSrc.empty()) {
      errs << "warning: C+ runtime source not found; runtime library will "
              "not be instrumented by the selected sanitizers\n";
    } else {
      llvm::SmallString<256> objPath;
      if (llvm::sys::fs::createTemporaryFile("cplus_runtime_san", ".o",
                                             objPath)) {
        errs << "error: cannot create temporary object for runtime "
                "instrumentation\n";
        return 1;
      }
      std::vector<std::string> rtArgs = base;
      rtArgs.push_back("-c");
      rtArgs.push_back("-o");
      rtArgs.push_back(std::string(objPath.str()));
      rtArgs.push_back(runtimeSrc);
      for (const auto &a : opts.extraArgs)
        rtArgs.push_back(a);
      if (runProgram(clang, rtArgs) != 0) {
        errs << "error: failed to compile instrumented C+ runtime\n";
        llvm::sys::fs::remove(objPath);
        return 1;
      }
      objects.push_back(std::string(objPath.str()));
      instrumentedRuntime = true;
      DRIVER_LOG(CLOG_INFO, "instrumented runtime: %s",
                 runtimeSrc.c_str());
    }
  }
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
  if (!instrumentedRuntime) {
    std::string runtime = runtimeLibArg();
    if (!runtime.empty())
      linkArgs.push_back(runtime);
  }
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
