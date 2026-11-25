//===- Driver.cpp - C+ compile pipeline -------------------------*- C++ -*-===//

#include "Driver/Driver.h"

#include "Analysis/OwnershipAnalysis.h"
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

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
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

/// Set for the lifetime of a STRICT Safe run. Security-critical path lookups
/// (runtime library, shipped include dir) must then ignore CPLUS_ROOT: an
/// untrusted source root could otherwise substitute a pass-through runtime or
/// a no-op header while `check` still saw a well-formed safety AST.
static bool g_strictSafePaths = false;

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
std::string findByLayout(const std::string &relative,
                         bool trustEnvRoot = true) {
  std::string exe = llvm::sys::fs::getMainExecutable("c+", nullptr);
  if (!exe.empty()) {
    std::string dir = llvm::sys::path::parent_path(exe).str();
    for (const char *up : {"", "../", "../../"}) {
      std::string candidate = joinPath(dir, up) + relative;
      if (llvm::sys::fs::exists(candidate))
        return candidate;
    }
  }
  // CPLUS_ROOT is an untrusted env override. For security-critical lookups in
  // STRICT Safe mode only the compiled-in / installed locations may be used.
  std::string root = trustEnvRoot ? findSourceRoot() : std::string();
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

/// Resolve the runtime library linked into the final binary. STRICT Safe mode
/// ignores CPLUS_RUNTIME_LIB: a stub library with pass-through bounds checks
/// would strip every trap while `check` still reported a clean result. Only the
/// compiled-in and installed locations (shipped with the compiler) are trusted.
/// DEFAULT/LEGACY keep the env override because they make no soundness claim.
std::string runtimeLibArg(bool trustEnvOverride) {
  std::string result;
  if (trustEnvOverride) {
    result = findCPlusRuntimeLib();
  } else {
#ifdef CPLUS_RUNTIME_LIB
    if (fileExists(CPLUS_RUNTIME_LIB))
      result = CPLUS_RUNTIME_LIB;
#endif
    if (result.empty())
      for (const char *relative :
           {"build/libcplus_runtime.a", "lib/libcplus_runtime.a",
            "libcplus_runtime.a"}) {
        // trustEnvOverride == false here (STRICT): do not fall back to a
        // CPLUS_ROOT-supplied library either.
        std::string found = findByLayout(relative, trustEnvOverride);
        if (!found.empty()) {
          result = found;
          break;
        }
      }
  }
  return result;
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
#ifdef __APPLE__
  // Apple's system clang resolves the installed SDK (MacOSX.sdk) correctly.
  // Homebrew's clang loads arm64-apple-darwin27.cfg, which selects a
  // MacOSX27.sdk that is not installed on this host. Prefer the system
  // compiler instead of relying on PATH order. CPLUS_CLANG above still wins.
  if (fileExists("/usr/bin/clang"))
    return "/usr/bin/clang";
#endif
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
  // In STRICT, CPLUS_ROOT must not redirect the shipped runtime headers.
  std::string found = findByLayout("runtime/include", !g_strictSafePaths);
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
  if (mode == Mode::Safe) {
    checkStaticBounds(text, rewritten.arrayLengths, diags, name);
    // Dynamic indices into fixed-size arrays get a C+ runtime check before
    // the access. Constant indices were either diagnosed above or left alone.
  }
  LoweredSource lowered =
      lowerRaii(rewritten.text, rewritten.resourceTypes, diags, name, mode,
                strict);
  // Exception lowering runs after RAII so cleanup inserted into a `try`
  // block is preserved. It is Safe-mode only: LEGACY keeps original bytes.
  lowered.text = mode == Mode::Safe ? lowerExceptions(lowered.text)
                                    : lowered.text;
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
  // std::getline strips trailing newlines.  Restore the exact count so the
  // lowered C keeps blank lines at EOF byte-for-byte (a real C fixture ends
  // with `}\n\n\n`; restoring only one newline lost two bytes).
  size_t trailing = 0;
  for (size_t i = source.size(); i > 0 && source[i - 1] == '\n'; --i)
    ++trailing;
  while (!result.empty() && result.back() == '\n')
    result.pop_back();
  result.append(trailing, '\n');
  return result;
}

/// Locate the runtime source so it can be instrumented for sanitizer builds.
/// Deliberately does NOT use findSourceRoot(): CPLUS_ROOT is an untrusted env
/// override and a fake root with a no-op __cplus_bounds_check would silently
/// disable the runtime safety checks. Only the compiled-in source tree and
/// the installed layout (both shipped with the compiler) are trusted.
static std::string runtimeSourcePath() {
#ifdef CPLUS_SOURCE_DIR
  std::string candidate =
      joinPath(CPLUS_SOURCE_DIR, "runtime/cplus_runtime.c");
  if (fileExists(candidate))
    return candidate;
#endif
  std::string exe = llvm::sys::fs::getMainExecutable("c+", nullptr);
  if (!exe.empty()) {
    std::string dir = llvm::sys::path::parent_path(exe).str();
    for (const char *up : {"", "../", "../../"}) {
      std::string candidate = joinPath(dir, up) + "/runtime/cplus_runtime.c";
      if (fileExists(candidate))
        return candidate;
    }
  }
  return std::string();
}

static Sanitizer effectiveSanitizer(const DriverOptions &opts) {
  if (!opts.sanitizerExplicit && opts.mode == Mode::LegacyC)
    return Sanitizer::None;
  return opts.sanitizer;
}

/// `check` is static analysis only: it never links an executable, probes a
/// backend capability, or instruments the C+ runtime. All three decisions
/// share this predicate so the command cannot accidentally opt into one path.
static bool isCheckCommand(const DriverOptions &opts) {
  return opts.command == "check";
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

/// Every selectable sanitizer bit and its user-facing name. Shared by the
/// capability probe, diagnostics and `--print-sanitizers` so the three cannot
/// drift apart.
struct SanitizerEntry {
  Sanitizer bit;
  const char *name;
};

static const SanitizerEntry kSanitizerEntries[] = {
    {Sanitizer::Address, "address"},   {Sanitizer::Undefined, "undefined"},
    {Sanitizer::Memory, "memory"},     {Sanitizer::Thread, "thread"},
    {Sanitizer::Leak, "leak"},         {Sanitizer::Cfi, "cfi"},
    {Sanitizer::Integer, "integer"},
};

/// Flags for one sanitizer bit. CFI needs LTO and non-trapping mode so a
/// violation produces a diagnostic instead of a silent SIGTRAP.
static void addSanitizerFlags(std::vector<std::string> &args, Sanitizer san) {
  // An explicit "none" (--sanitize=none / --no-sanitize) must never emit any
  // -fsanitize/-fno-sanitize flag, not even the common debug-info options.
  if (san == Sanitizer::None)
    return;
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
  args.push_back("-fno-sanitize-recover=all");
  args.push_back("-fno-omit-frame-pointer");
  args.push_back("-g"); // debug info for actionable stack traces
}

/// Clang accepts most single-dash options with a double-dash spelling too.
/// Normalize one leading '-' before gate comparisons so spellings such as
/// `--include=evil.h`, `--fno-sanitize-recover=all` or `--Wno-error=format`
/// cannot evade a gate that matches only the single-dash form.
static llvm::StringRef normalizeFlag(llvm::StringRef a) {
  if (a.size() > 2 && a[0] == '-' && a[1] == '-' && a[2] != '-')
    return a.drop_front();
  return a;
}

static std::vector<llvm::StringRef> splitFlagList(llvm::StringRef list);
static bool sanitizerNameIs(llvm::StringRef token, llvm::StringRef canonical);

/// True when pass-through flags disable the backend's signed-overflow
/// sanitizer. checkAstSafety() suppresses native arithmetic guards when the
/// driver selected that sanitizer, so disabling it from `extraArgs` would
/// leave signed overflow silently unenforced. Reject the contradictory
/// configuration instead of letting the gate be bypassed.
static bool extraArgsDisableSignedOverflow(const std::vector<std::string> &extra) {
  for (const auto &raw : extra) {
    llvm::StringRef a = normalizeFlag(raw);
    if (!a.starts_with("-fno-sanitize="))
      continue;
    for (llvm::StringRef token :
         splitFlagList(a.substr(strlen("-fno-sanitize="))))
      if (sanitizerNameIs(token, "undefined") ||
          sanitizerNameIs(token, "signed-integer-overflow"))
        return true;
  }
  return false;
}

/// Pass-through flags that change what the *real* clang compile parses but
/// are discarded before the in-memory safety AST (both checkAstSafety and
/// probeSafetyAst keep only -I/-D/-std). Accepting them in STRICT would let
/// `check` verify one program while `build` compiles a different one -- the
/// same class of bypass as the wave-1 `-D` divergence. Reject them rather
/// than trying to re-implement Clang's pre-processor in the probe. -isysroot
/// is intentionally allowed: it selects an SDK, which the safety analysis
/// does not use for its own decisions, and the regression suites pass it
/// through legitimately.
static bool extraArgsChangeParse(const std::vector<std::string> &extra) {
  for (const auto &raw : extra) {
    llvm::StringRef a = normalizeFlag(raw);
    // Prefix (not exact) matching covers -include=evil.h, --includeevil.h,
    // -imacros, -include-pch and the other header-search forms that change
    // what the backend parses but are discarded from the safety AST.
    if (a.starts_with("-include") || a.starts_with("-imacros") ||
        a.starts_with("-isystem") || a.starts_with("-iquote") ||
        a.starts_with("-idirafter") || a.starts_with("-iprefix") ||
        a.starts_with("-iwithprefix"))
      return true;
    if (a == "-x" ||
        (a.size() > 2 && a[0] == '-' && a[1] == 'x' &&
         std::isalpha(static_cast<unsigned char>(a[2]))))
      return true;
  }
  return false;
}

/// Split a -f...= / -W...= value into whole tokens. Clang accepts ',' and '+'
/// as separators in several option lists, and a probe can combine names into a
/// single token ("undefined+all"). Whole-token comparison is essential: a
/// substring test for "signed-integer-overflow" would also match
/// "unsigned-integer-overflow", which does NOT gate signed overflow.
static std::vector<llvm::StringRef> splitFlagList(llvm::StringRef list) {
  std::vector<llvm::StringRef> out;
  size_t start = 0;
  while (start <= list.size()) {
    size_t sep = list.find_first_of(",+ ", start);
    llvm::StringRef token = list.substr(
        start, sep == llvm::StringRef::npos ? llvm::StringRef::npos
                                            : sep - start);
    token = token.trim();
    if (!token.empty())
      out.push_back(token);
    if (sep == llvm::StringRef::npos)
      break;
    start = sep + 1;
  }
  return out;
}

/// True when `token` denotes the same sanitizer group as `canonical`. Alias
/// spellings and the all-inclusive "all" are handled here so changing only the
/// spelling of a flag cannot slip past a gate that compares one canonical name.
static bool sanitizerNameIs(llvm::StringRef token, llvm::StringRef canonical) {
  if (token == "all")
    return true;
  if (token == canonical)
    return true;
  if (canonical == "undefined")
    return token == "ubsan" || token == "signed-integer-overflow";
  if (canonical == "address")
    return token == "asan";
  if (canonical == "memory")
    return token == "msan";
  if (canonical == "thread")
    return token == "tsan";
  if (canonical == "leak")
    return token == "lsan";
  if (canonical == "integer")
    return token == "unsigned" || token == "unsigned-integer-overflow" ||
           token == "implicit-integer-truncation" ||
           token == "implicit-integer-arithmetic-value-change";
  return false;
}

/// Whole-token membership for -f...= lists (separator- and alias-aware).
static bool sanitizerListHas(llvm::StringRef list,
                             llvm::ArrayRef<llvm::StringRef> tokens) {
  for (llvm::StringRef part : splitFlagList(list))
    for (llvm::StringRef wanted : tokens)
      if (sanitizerNameIs(part, wanted))
        return true;
  return false;
}

/// A pass-through `-fsanitize-recover=`/`-fno-sanitize-trap=` appended after
/// base overrides the driver's `-fno-sanitize-recover=all`. With native
/// arithmetic guards suppressed by the selected UBSan, that turns an aborting
/// overflow check into a silent recovery -- a STRICT soundness hole.
static bool extraArgsWeakenSanitizerAbort(const std::vector<std::string> &extra) {
  static const llvm::StringRef kAbort[] = {"all", "undefined",
                                           "signed-integer-overflow"};
  for (const auto &raw : extra) {
    llvm::StringRef a = normalizeFlag(raw);
    // The bare spellings mean "all" in Clang and must not be missed.
    if (a == "-fsanitize-recover" || a == "-fno-sanitize-trap")
      return true;
    if (a.starts_with("-fsanitize-recover="))
      if (sanitizerListHas(a.substr(strlen("-fsanitize-recover=")), kAbort))
        return true;
    if (a.starts_with("-fno-sanitize-trap="))
      if (sanitizerListHas(a.substr(strlen("-fno-sanitize-trap=")), kAbort))
        return true;
  }
  return false;
}

/// Bounds/null/alignment sanitizers are an independent witness for the C+
/// checks. Disabling them via pass-through would let a runtime-swapped or
/// otherwise broken check go unnoticed, so STRICT rejects the downgrade.
static bool extraArgsDisablePointerSanitizers(
    const std::vector<std::string> &extra) {
  static const llvm::StringRef kPtr[] = {"bounds", "null", "alignment"};
  for (const auto &raw : extra) {
    llvm::StringRef a = normalizeFlag(raw);
    if (a.starts_with("-fno-sanitize="))
      if (sanitizerListHas(a.substr(strlen("-fno-sanitize=")), kPtr))
        return true;
  }
  return false;
}

/// Safety warnings promoted to errors by backendArgs() are a core STRICT gate.
/// `-Wno-error=<safety>` (or `-Wno-<safety>`) appended after base downgrades
/// them back to warnings, so `check` would report success on an unsafe TU.
static bool extraArgsWeakenSafetyWarnings(
    const std::vector<std::string> &extra) {
  static const llvm::StringRef kSafety[] = {
      "array-bounds", "return-stack-address", "uninitialized",
      "sizeof-array-argument", "sizeof-pointer-div", "format-security",
      "unsequenced", "shift-count-overflow", "shift-negative-value",
      "varargs", "switch", "all"};
  for (const auto &raw : extra) {
    llvm::StringRef a = normalizeFlag(raw);
    if (a == "-Wno-error" || a == "-w")
      return true; // disables every -Werror= gate below
    llvm::StringRef list;
    if (a.starts_with("-Wno-error="))
      list = a.substr(strlen("-Wno-error="));
    else if (a.starts_with("-Wno-"))
      list = a.substr(strlen("-Wno-"));
    else
      continue;
    for (llvm::StringRef token : splitFlagList(list)) {
      if (token == "all")
        return true;
      // Group spellings are honoured by Clang too: `-Wno-error=format`
      // downgrades -Werror=format-security, `shift` covers both shift gates.
      for (llvm::StringRef gate : kSafety)
        if (token == gate ||
            (token.size() < gate.size() && gate.starts_with(token) &&
             gate[token.size()] == '-'))
          return true;
    }
  }
  return false;
}

/// Conservative allow-list of pass-through tokens accepted in STRICT Safe mode.
/// STRICT promises that the program analysed is the program compiled and that
/// no gate can be downgraded. Pass-through arguments are an unbounded input
/// surface (`--` is especially dangerous because it bypasses Main.cpp's flag
/// classification), so instead of growing a deny-list we accept only this
/// small, fixed set that the suites legitimately use. Everything else --
/// -Xclang/-Xpreprocessor/@response, -include/-imacros/-isystem/-x spellings,
/// -U/-f... sanitizer toggles, -Wno-* downgrades and bare inputs -- fails
/// closed. Adding a flag here is an explicit, reviewable decision.
/// Only -I/-D/-std= are forwarded to the in-memory safety ASTs; the other
/// accepted flags either do not affect parsing or are link/codegen only, so
/// check and build keep identical safety-AST inputs.
static bool strictSafePassthroughAllowed(const std::vector<std::string> &extra) {
  for (size_t i = 0; i < extra.size(); ++i) {
    llvm::StringRef s = normalizeFlag(extra[i]);
    // Clang expands @response files itself, after the driver's gates, so a
    // token like `@rsp` can hide `-include evil.h` from every flag check.
    // Never allow the @ spelling in STRICT.
    if (extra[i].starts_with('@'))
      return false;
    // Two-token forms produced by Main.cpp (`-I dir`, `-D NAME`, ...) and by
    // `-- -isysroot dir`. Consume the value so a path is not mistaken for a
    // bare/injected token.
    if (s == "-I" || s == "-D" || s == "-L" || s == "-l" || s == "-isysroot") {
      if (++i >= extra.size())
        return false; // dangling value: fail closed
      continue;
    }
    if (s.starts_with("-I") || s.starts_with("-D") || s.starts_with("-L") ||
        s.starts_with("-l") || s.starts_with("-std=") ||
        s.starts_with("-isysroot") || s.starts_with("-O"))
      continue;
    // -U is deliberately excluded: it is parse-affecting but is NOT forwarded
    // to the safety AST, so allowing it would break check/build parity.
    if (s == "-g" || s == "-pthread" || s == "-pedantic" || s == "-ansi" ||
        s == "-pipe" || s == "-fno-common")
      continue;
    if (s.starts_with("-W")) {
      // -Wno-* (including -Wno-error groups) can downgrade STRICT's promoted
      // warnings; additive forms (-Wall, -Wextra, -Werror=...) are fine.
      if (s == "-Wno-error" || s.starts_with("-Wno-"))
        return false;
      // -Wl,/-Wa,/-Wp,/-Wf, are Clang's "forward this list to the linker /
      // assembler / preprocessor" spellings, NOT warning flags. They can
      // inject e.g. -Wl,--wrap=__cplus_bounds_check after every safety gate,
      // interposing instrumentation that the safety AST never sees. Reject
      // them and any comma-bearing -W token; ordinary warning flags never
      // contain a comma, so this is not a blanket -W rejection.
      if (s.find(',') != llvm::StringRef::npos || s == "-Wl" || s == "-Wa" ||
          s == "-Wp" || s == "-Wf")
        return false;
      continue;
    }
    return false;
  }
  return true;
}

/// The driver reserves the CPLUS* gate macros, and the runtime/instrumentation
/// reserves an explicit set of helper entry points (isReservedHelperName). A
/// pass-through -D/-U that redefines one can neutralise a check in the
/// *compiled* program while the analysis still sees a well-formed AST, so
/// STRICT refuses them. Ordinary project macros keep working.
static bool redefinesReservedCPlusMacro(const std::vector<std::string> &extra) {
  for (size_t i = 0; i < extra.size(); ++i) {
    llvm::StringRef a = normalizeFlag(extra[i]);
    llvm::StringRef def;
    if (a == "-D" || a == "-U") {
      if (i + 1 >= extra.size())
        return true;
      def = llvm::StringRef(extra[++i]);
    } else if (a.starts_with("-D") || a.starts_with("-U")) {
      def = a.substr(2);
    } else {
      continue;
    }
    llvm::StringRef name = def.take_while([](char c) {
      return c != '=' && c != '(' && c != ' ';
    });
    // The whole CPLUS/CPLUS_/CPLUS<Name> gate family and the implementation
    // namespace (__cplus_*, reserved to the implementation) are closed sets;
    // reserve the prefix so a future gate macro cannot be redefined by a
    // pass-through -D before its name is added here.
    if (name == "CPLUS" || name.starts_with("CPLUS") ||
        name.starts_with("__cplus_") || isReservedHelperName(name))
      return true;
  }
  return false;
}

/// The exact -Werror= safety flags backendArgs() installs. Re-appended after
/// pass-through flags so a user flag cannot silently downgrade a gate even in
/// modes where we do not hard-reject it.
static std::vector<std::string> safetyWarningErrorFlags() {
  return {"-Werror=array-bounds",        "-Werror=return-stack-address",
          "-Werror=uninitialized",       "-Wwrite-strings",
          "-Werror=sizeof-array-argument", "-Werror=sizeof-pointer-div",
          "-Werror=format-security",     "-Werror=unsequenced",
          "-Werror=shift-count-overflow", "-Werror=shift-negative-value",
          "-Werror=varargs",             "-Werror=switch"};
}

void printSanitizerInfo(llvm::raw_ostream &os) {
  os << "supported sanitizers: none";
  for (const auto &entry : kSanitizerEntries)
    os << ", " << entry.name;
  os << "\n";
  os << "disable with: --sanitize=none or --no-sanitize\n";
  os << "Safe-mode default: undefined (Legacy-C default: none)\n";
  // Resolving the backend only performs filesystem/PATH lookups. It never
  // probes a sanitizer or starts a compilation. CPLUS_CLANG and the macOS
  // /usr/bin/clang preference are applied by findClangBinary().
  os << "backend: " << findClangBinary() << "\n";
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

/// Issue #34: warnings are errors by default in Safe mode. --legacy-c keeps
/// plain-C behaviour unless -Werror was requested explicitly. -w is accepted
/// for gcc compatibility but never promotes itself over C+ safety gates.
static bool warningsAreErrors(const DriverOptions &opts) {
  if (opts.mode == Mode::LegacyC)
    return opts.warningsAsErrorsExplicit && opts.warningsAsErrors;
  return opts.warningsAsErrors;
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
    // Issue #34: these gates are unconditional in Safe mode. `-w` is consumed
    // by the driver for gcc compatibility but is deliberately NOT forwarded:
    // it would inhibit even -Werror=... diagnostics and defeat the C+ warning
    // policy. Its backend scope is documented as --legacy-c only.
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
    // Rust-like enum exhaustiveness: a switch on an enum must handle every
    // enumerator. Compile-time only; no ABI or emitted-code change.
    args.push_back("-Werror=switch");
    args.push_back("-DCPLUS_SAFE=1");
    // Opt-in runtime safety -> catchable exception behavior. Placed in the
    // shared base so it reaches every compiled TU, the instrumented runtime
    // recompilation, and the link step; Legacy C is deliberately unaffected.
    if (opts.autoRuntime)
      args.push_back("-DCPLUS_AUTO_RUNTIME=1");
  } else {
    args.push_back("-DCPLUS_LEGACY_C=1");
    // -w keeps its documented gcc meaning only in the explicit compatibility
    // mode. It cannot reach a Safe-mode invocation (branch above).
    if (opts.disableWarnings && !warningsAreErrors(opts))
      args.push_back("-w");
  }
  // Runtime sanitizers. Flags are part of `base`, which is used for both
  // per-file compilation and the final link, so the sanitizer runtime is
  // linked into the executable as well. A `check` run only type-checks, so it
  // does not need (and must not appear to probe) any sanitizer flags.
  if (!isCheckCommand(opts))
    addSanitizerFlags(args, effectiveSanitizer(opts));
  if (warningsAreErrors(opts))
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
  // Validate the sanitizer selection before doing any work. Conflicts are
  // configuration errors, so they are diagnosed for every command, including
  // `check`. Never silently ignore an option or let an unsupported combination
  // fail later at link.
  Sanitizer san = effectiveSanitizer(opts);
  const std::string conflict = sanitizerConflict(san);
  if (!conflict.empty()) {
    errs << "error: unsupported sanitizer combination: " << conflict << "\n";
    return 1;
  }
  // Native arithmetic guards are suppressed when checkAstSafety's gate sees
  // the driver-selected Undefined sanitizer. A pass-through -fno-sanitize=
  // flag could silently disable that backend check, so reject the pair.
  if (wants(san, Sanitizer::Undefined) &&
      extraArgsDisableSignedOverflow(opts.extraArgs)) {
    errs << "error: pass-through flags disable the signed-integer-overflow "
            "sanitizer that C+ relies on to suppress native guards; remove "
            "'-fno-sanitize=undefined' or use --sanitize=none\n";
    return 1;
  }
  // STRICT is a soundness contract: it must fail closed on any pass-through
  // flag that can make the analysed program differ from the compiled one,
  // downgrade a safety gate, or hide a runtime check. DEFAULT keeps accepting
  // these (no soundness claim), and --legacy-c --strict only gets its
  // documented no-effect disclosure so plain-C ABI/source stays untouched.
  const bool strictSafe = opts.strict && opts.mode == Mode::Safe;
  g_strictSafePaths = strictSafe;
  // STRICT never trusts an environment-supplied runtime library: a pass-through
  // runtime would strip every trap while `check` still reported a clean result.
  // runtimeLibArg(false) already ignores it; drop it here as well so no other
  // consumer can pick it up by accident.
  if (strictSafe) {
    ::unsetenv("CPLUS_RUNTIME_LIB");
    // An env-supplied contract CAN carry a legitimate, validated effect
    // summary (the honest header-declared FFI shape), so it must stay usable.
    // Harden it instead: a relative, symlinked or missing path is a
    // substitution attempt and fails closed before any analysis. Content
    // validation remains in checkAstSafety/OwnershipAnalysis.
    if (const char *env = std::getenv("CPLUS_CONTRACT_PATH")) {
      if (*env) {
        llvm::SmallString<256> canonical;
        std::error_code ec = llvm::sys::fs::real_path(env, canonical);
        if (ec || canonical.empty() ||
            !llvm::sys::path::is_absolute(canonical) ||
            !fileExists(std::string(canonical.str())) ||
            std::string(env) != std::string(canonical.str())) {
          errs << "error: CPLUS_CONTRACT_PATH must name a canonical absolute "
                  "regular file in STRICT Safe mode\n";
          return 1;
        }
      }
    }
  }
  // Clang expands @response files itself, after the driver's flag gates. A
  // token such as `@rsp` can therefore hide `-include guard.h` (which redefines
  // a bounds-check helper to a no-op) from every other check. Reject the @
  // spelling explicitly with a clear diagnostic before the general allow-list.
  if (strictSafe) {
    auto rejectResponseFile = [&](llvm::StringRef a) {
      if (a.starts_with('@')) {
        errs << "error: @response files are not allowed in STRICT Safe mode: "
                "clang expands them after the C+ safety gates; pass flags "
                "directly instead\n";
        return true;
      }
      return false;
    };
    for (const auto &a : opts.extraArgs)
      if (rejectResponseFile(a))
        return 1;
    // Clang expands any argv token beginning with '@', not just pass-through
    // flags. -o's value is forwarded verbatim to the backend, so `-o @rsp`
    // could otherwise smuggle -include/-Wl flags past every gate above.
    if (rejectResponseFile(opts.output))
      return 1;
  }
  if (strictSafe && extraArgsChangeParse(opts.extraArgs)) {
    errs << "error: pass-through flags change how the C+ safety AST parses "
            "the program but are not forwarded to it (-include, -imacros, "
            "-isystem, -x, -include-pch); STRICT would analyse a different "
            "program than it compiles\n";
    return 1;
  }
  if (strictSafe && extraArgsWeakenSanitizerAbort(opts.extraArgs)) {
    errs << "error: pass-through flags weaken sanitizer abort behaviour "
            "(-fsanitize-recover / -fno-sanitize-trap) that C+ relies on; "
            "remove them or use --sanitize=none\n";
    return 1;
  }
  if (strictSafe && extraArgsDisablePointerSanitizers(opts.extraArgs)) {
    errs << "error: pass-through flags disable bounds/null/alignment "
            "sanitizers that witness the C+ runtime checks\n";
    return 1;
  }
  if (strictSafe && extraArgsWeakenSafetyWarnings(opts.extraArgs)) {
    errs << "error: pass-through flags downgrade a STRICT safety warning "
            "promoted to error (-Wno-error=<safety> / -Wno-<safety>)\n";
    return 1;
  }
  if (strictSafe && redefinesReservedCPlusMacro(opts.extraArgs)) {
    errs << "error: pass-through -D/-U redefines a reserved C+ gate macro "
            "(CPLUS*) or a compiler/runtime instrumentation helper; STRICT "
            "cannot verify a program whose safety helpers are replaced\n";
    return 1;
  }
  // Catch-all: anything not on the conservative allow-list fails closed. This
  // is what stops unseen spellings (-Xclang injection, @response files,
  // -include variants, -- ordering tricks, stray bare inputs) rather than a
  // never-complete deny-list.
  if (strictSafe && !strictSafePassthroughAllowed(opts.extraArgs)) {
    errs << "error: pass-through flag outside the STRICT Safe allow-list "
            "(-I/-D/-std=/-isysroot/-O/-W/-L/-l/-g/-pthread/...); flags such "
            "as -Xclang, @response, -include/-imacros/-isystem, -U, -x and "
            "-Wno-* are rejected because they can change the analysed program "
            "or downgrade a gate\n";
    return 1;
  }
  // Capability probing compiles and runs a defect program. `check` must stay
  // a pure static-analysis command, so explicitly selected sanitizers are
  // accepted there without launching any probe.
  if (opts.sanitizerExplicit && !isCheckCommand(opts)) {
    for (const auto &entry : kSanitizerEntries) {
      if (!wants(san, entry.bit))
        continue;
      if (!sanitizerSupported(entry.bit, clang, opts.extraArgs)) {
        errs << "error: sanitizer '" << entry.name
             << "' is not supported by the selected backend (" << clang
             << "); detected by a compile-and-run probe\n";
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
  // `check` and `build` must feed the safety analyser the SAME gate tokens.
  // backendArgs() omits sanitizer flags for `check` (it only type-checks and
  // must not probe a sanitizer), but checkAstSafety uses their presence to
  // decide whether native arithmetic guards are needed. Derive a dedicated
  // analysis base that always carries them, so a program accepted by `check`
  // is analysed exactly as `build` will compile it. STRICT's pass-through
  // allow-list reinforces this: the only parse-affecting tokens that survive
  // (-I/-D/-std=) are appended to `astArgs` below for BOTH commands, while the
  // backend compile uses the same `base` + `extraArgs` pair, so the safety AST
  // and the compiled TU can never diverge on flags.
  std::vector<std::string> analysisBase = base;
  if (isCheckCommand(opts))
    addSanitizerFlags(analysisBase, effectiveSanitizer(opts));
  // Re-assert the gate flags AFTER pass-through flags. Clang uses the last
  // occurrence of -Werror=/-Wno-error= and -f(no-)sanitize-recover, so an
  // appended user token must not silently win over the driver's gate.
  auto appendSafetyTail = [&](std::vector<std::string> &v) {
    if (opts.mode == Mode::Safe)
      for (const auto &f : safetyWarningErrorFlags())
        v.push_back(f);
    if (san != Sanitizer::None)
      v.push_back("-fno-sanitize-recover=all");
  };
  std::vector<std::string> objects;
  bool instrumentedRuntime = false;
  bool strictDisclosureEmitted = false;
  // Recompiling the C+ runtime with sanitizer flags is part of building an
  // instrumented executable. `check` never produces an executable, so skip it.
  if (san != Sanitizer::None && !isCheckCommand(opts)) {
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
      appendSafetyTail(rtArgs);
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
    RewrittenSource rewritten;
    std::string loweredText = lowerUnit(source, input, diags, opts.mode, &ok,
                                        &rewritten, false, opts.strict);
    DRIVER_LOG(CLOG_DEBUG, "lowered %s (%zu bytes)", input.c_str(),
               loweredText.size());

    // The in-memory safety AST must represent the same program the backend
    // compiles. `base` alone omits pass-through -D/-I flags (Main.cpp stores
    // them in extraArgs), which previously let a macro-guarded unsafe path be
    // analysed in one configuration and compiled in another. analysisBase is
    // command-independent (see above) so check and build share the exact gate
    // tokens; sanitizer -f* tokens are filtered here only because they cannot
    // affect parsing and checkAstSafety re-derives its own gate from them.
    std::vector<std::string> astArgs = analysisBase;
    llvm::StringRef astInputDir = llvm::sys::path::parent_path(input);
    if (astInputDir.empty())
      astInputDir = ".";
    astArgs.push_back("-I" + std::string(astInputDir));
    for (size_t i = 0; i < opts.extraArgs.size(); ++i) {
      const std::string &a = opts.extraArgs[i];
      if (a.rfind("-fsanitize=", 0) == 0 || a.rfind("-fno-sanitize=", 0) == 0)
        continue;
      llvm::StringRef na = normalizeFlag(a);
      // safetyParseArgs() keeps only -I/-D/-std= tokens. A two-token `-I dir`
      // / `-D NAME` would be split there into a dangling `-I`/`-D` (dropping
      // the value), making the safety AST fail to parse -- the compiled TU
      // parses it fine. Fold the pair into one token so check and build feed
      // the analyser the same definition the backend compiles.
      if ((na == "-I" || na == "-D") && i + 1 < opts.extraArgs.size()) {
        astArgs.push_back(na.str() + opts.extraArgs[++i]);
        continue;
      }
      astArgs.push_back(a);
    }
    if (opts.mode == Mode::Safe)
      checkAstSafety(loweredText, input, astArgs, diags, opts.strict);

    if (opts.strict) {
      if (opts.mode == Mode::LegacyC) {
        // STRICT has no teeth in LEGACY: no C+ safety analysis runs at all.
        // Disclose that instead of silently reporting a clean check.
        if (!strictDisclosureEmitted) {
          Diag d;
          // Informational disclosure, not a safety finding: issue #34 makes
          // every Warning fatal, so keep the notice visible as a Note.
          d.level = DiagLevel::Note;
          d.message = "[CPLUS-LEGACY] STRICT mode has no effect with --legacy-c: "
                      "C+ safety analysis is disabled to preserve plain-C "
                      "ABI and source";
          d.file = input;
          diags.report(std::move(d));
          strictDisclosureEmitted = true;
        }
      } else {
        // Reserve the compiler/runtime helper namespace (an explicit symbol
        // set, NOT a prefix rule: legitimate helpers like cplus_core_helper
        // stay allowed). A user-written declaration or definition of a
        // reserved symbol in the ORIGINAL source interposes the generated
        // instrumentation at link time (e.g. defining __cplus_checked_add_i64
        // turns the overflow trap into a silent wrap). Parse `source`, not
        // `loweredText`: the lowered text has
        // a generated prelude full of legitimate extern declarations for the
        // very same helpers, which must not be reported as user code. This also
        // runs for `build`/`run`, so the check happens before any link step.
        // (--legacy-c is excluded above: no ABI/source change there.)
        // Cheap textual pre-filter: both reserved prefixes contain "cplus_",
        // so sources that never mention it cannot declare a helper and skip
        // the extra in-memory parse entirely.
        std::set<std::string> reserved;
        if (source.find("cplus_") != std::string::npos)
          reserved = findUserDeclaredHelperSymbols(source, astArgs, input);
        for (const auto &fn : reserved) {
          Diag d;
          d.level = DiagLevel::Error;
          d.message = "[CPLUS-FFI] symbol '" + fn +
                      "' is reserved for the C+ compiler/runtime; a "
                      "user declaration or definition can interpose the "
                      "generated safety instrumentation at link time";
          d.file = input;
          diags.report(std::move(d));
        }

        SafetyAstProbe probe = probeSafetyAst(loweredText, astArgs, input);
        // Fail closed (every TU, both `check` and `build`): a failed parse
        // means checkAstSafety silently skipped every check.  Reporting
        // success in STRICT `check` accepted a program that was never
        // analysed; emitting an executable in STRICT `build` did the same
        // thing while looking like a successful protected build.
        if (!probe.parsed) {
          Diag d;
          d.level = DiagLevel::Error;
          d.message = "[CPLUS-ANALYSIS] cannot build the C+ safety AST for "
                      "this translation unit; STRICT would silently skip all "
                      "safety analysis";
          d.file = input;
          diags.report(std::move(d));
        }
        // A bodiless extern with pointer effects and no declared contract has
        // unverifiable ownership/bounds behaviour. A clean `check` would be a
        // false proof of safety, so make it a hard error in STRICT check.
        // (Builds keep the warning disclosure below: they still emit an
        // executable, and LEGACY/ABI source preservation is unaffected.)
        // Pure libc calls are excluded by probeSafetyAst via the built-in
        // effect table / JSON contract, so printf/malloc etc. stay accepted.
        //
        // A same-named body elsewhere is NOT a safety proof. Analysis is
        // per-translation-unit: a body in a sibling file is never passed to
        // this invocation and is never analysed, while a body in another
        // explicit input only receives an independent per-TU check -- neither
        // yields a validated effect summary at THIS call site. That is exactly
        // the cross-TU double-free shape (`main` frees p after `victim(p)`
        // whose body frees p) that previously slipped through both the
        // `definedInInputs` and `definedInSiblings` name matches. Only a
        // declared/validated effect summary can discharge this gate, and
        // probeSafetyAst already excludes every function that has one from
        // `unverifiedPointerExterns`; there is no name-based exemption left.
        if (isCheckCommand(opts)) {
          for (const auto &fn : probe.unverifiedPointerExterns) {
            Diag d;
            d.level = DiagLevel::Error;
            d.message = "[CPLUS-FFI] function '" + fn +
                        "' has safety-relevant pointer effects but no "
                        "definition with a validated effect summary in the "
                        "checked translation unit and no declared contract; "
                        "STRICT check cannot verify its effects (a body in "
                        "another file or a name match is not a proof)";
            d.file = input;
            diags.report(std::move(d));
          }
        }
        // Per-TU disclosure: a body defined in another TU (or a library) is
        // not safety-checked here, so STRICT acceptance is not cross-TU proof.
        if (!strictDisclosureEmitted &&
            (inputs.size() > 1 || !probe.unresolvedFunctions.empty())) {
          std::string names;
          for (const auto &fn : probe.unresolvedFunctions) {
            if (!names.empty())
              names += ", ";
            names += "'" + fn + "'";
          }
          Diag d;
          // Scope disclosure, not a finding about the program: issue #34 makes
          // every Warning fatal, so this stays a Note and cannot reject a
          // program that is accepted by STRICT's soundness gates.
          d.level = DiagLevel::Note;
          d.message =
              "[CPLUS-MULTITU] STRICT analysis is per-translation-unit: ";
          if (!names.empty())
            d.message += "functions " + names +
                         " have no definition in this TU; ";
          d.message += "cross-TU control/data flow is not safety-checked here";
          d.file = input;
          diags.report(std::move(d));
          strictDisclosureEmitted = true;
        }
      }
    }

    printDiagnostics(diags, errs, input);
    if (diags.hasErrors())
      return 1;
    if (warningsAreErrors(opts) && diags.warningCount() > 0) {
      errs << "error: " << diags.warningCount()
           << " C+ warning(s) treated as errors (default warning policy)\n";
      // Multi-TU `-c`/`-S`/`-E` can have written an output for an earlier TU
      // before a later TU failed. Never leave an artifact that looks like a
      // successful build of a rejected program.
      if (!output.empty())
        llvm::sys::fs::remove(output);
      return 1;
    }
    if (opts.mode == Mode::Safe && opts.command != "check") {
      // Runtime checks are inserted only after all static analysis, so an
      // instrumented index expression cannot mask a provable bounds error.
      loweredText = instrumentDynamicArrayBounds(loweredText,
                                                 rewritten.arrayLengths);
    }

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
    appendSafetyTail(args);
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
    std::string runtime = runtimeLibArg(!strictSafe);
    if (!runtime.empty())
      linkArgs.push_back(runtime);
  }
  for (const auto &a : opts.extraArgs)
    linkArgs.push_back(a);
  appendSafetyTail(linkArgs);
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
