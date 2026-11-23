#include "Analysis/OwnershipAnalysis.h"

#include <clang/AST/ASTContext.h>
#include <clang/AST/Expr.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Analysis/CFG.h>
// Reuse Clang's upstream uninitialized-value dataflow analysis instead of a
// C+-specific reimplementation.  Licensed Apache-2.0 WITH LLVM-exception;
// see docs/compiler/upstream-reuse.md for the pinned source and notice.
#include <clang/Analysis/Analyses/UninitializedValues.h>
#include <clang/Analysis/AnalysisDeclContext.h>
// For the driver's fail-closed parse probe. Licensed Apache-2.0 WITH
// LLVM-exception; see docs/compiler/upstream-reuse.md.
#include <clang/Basic/Diagnostic.h>
#include <clang/Frontend/ASTUnit.h>
#include <clang/Frontend/PCHContainerOperations.h>
#include <clang/Tooling/Tooling.h>
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include <clang/Basic/SourceManager.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <deque>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <vector>

using namespace clang;

namespace cplus {
namespace {

struct ContractState {
  std::map<std::string, FunctionEffect> contracts;
  std::vector<ContractIssue> issues;
  bool attempted = false;
  bool loaded = false;
};

ContractState &contractState() {
  static ContractState state;
  return state;
}

/// All v2 fields. Unknown fields are reported instead of silently ignored.
bool isKnownContractField(llvm::StringRef f) {
  static const std::set<llvm::StringRef> kFields = {
      "params",           "nullableArgs",     "ownership",
      "transfer",         "extent",           "effects",
      "borrow",           "failure",          "diagnostics",
      "dynamic",          "paramTypes",       "pointerBounds",
      "allocationSize",   "ownershipTransfer", "borrowLifetime",
      "readEffects",      "writeEffects",     "freeEffects",
      "returnState",      "failureBehavior",  "threadAssumptions",
      "optional",         "origin"};
  return kFields.count(f) != 0;
}

/// Fields that must be present on every function contract. `optional` and
/// `origin` are deliberately excluded: they are only present on some entries.
bool isRequiredContractField(llvm::StringRef f) {
  static const std::set<llvm::StringRef> kRequired = {
      "params",           "nullableArgs",      "ownership",
      "transfer",         "extent",            "effects",
      "borrow",           "failure",           "diagnostics",
      "dynamic",          "paramTypes",        "pointerBounds",
      "allocationSize",   "ownershipTransfer", "borrowLifetime",
      "readEffects",      "writeEffects",      "freeEffects",
      "returnState",      "failureBehavior",   "threadAssumptions"};
  return kRequired.count(f) != 0;
}

/// Strip whitespace so that a declaration like "FILE *stream" and an effect
/// target like "FILE*" can be compared directly.
static std::string compactDecl(llvm::StringRef s) {
  std::string out;
  for (char c : s)
    if (!std::isspace(static_cast<unsigned char>(c)))
      out.push_back(c);
  return out;
}

/// True when `pos` starts a declaration word (not in the middle of an
/// identifier). Prevents e.g. "s" from matching the "s" in "size_t".
static bool atWordStart(const std::string &s, size_t pos) {
  if (pos == 0)
    return true;
  const char c = s[pos - 1];
  return !std::isalnum(static_cast<unsigned char>(c)) && c != '_';
}

/// Index of the parameter whose declaration matches `token` (a parameter name
/// such as "dst" or a type such as "FILE*"). Returns -1 when no parameter
/// matches -- notably for the checked `_s` entries, whose `params` are prose
/// placeholders rather than real signatures, so no index is representable.
static int paramIndexFor(const llvm::json::Array &params,
                         llvm::StringRef token) {
  if (token.empty())
    return -1;
  const std::string want = compactDecl(token);
  int idx = 0;
  for (const auto &v : params) {
    auto decl = v.getAsString();
    if (decl) {
      const std::string have = compactDecl(*decl);
      const size_t pos = have.find(want);
      if (pos != std::string::npos && atWordStart(have, pos)) {
        const size_t end = pos + want.size();
        // A type token ending in '*' may be followed directly by the variable
        // name ("FILE*" -> "FILE*stream").
        const bool rightOk = end == have.size() || want.back() == '*' ||
                             (!std::isalnum(
                                  static_cast<unsigned char>(have[end])) &&
                              have[end] != '_');
        if (rightOk)
          return idx;
      }
    }
    ++idx;
  }
  return -1;
}

/// Index of the first parameter declared as a pointer. Prose placeholders
/// contain no '*', so this returns -1 for those contracts.
static int firstPointerParam(const llvm::json::Array &params) {
  int idx = 0;
  for (const auto &v : params) {
    auto decl = v.getAsString();
    if (decl && decl->contains('*'))
      return idx;
    ++idx;
  }
  return -1;
}

/// Split an effect string such as "frees FILE*" or "writes dst" into its verb
/// and an optional target token naming a parameter.
struct EffectExpr {
  llvm::StringRef verb;
  llvm::StringRef target;
};

static EffectExpr parseEffectExpr(llvm::StringRef s) {
  const size_t sp = s.find_first_of(" \t");
  return {s.take_front(sp), s.substr(sp).trim()};
}

static void addIndex(std::vector<int> &indices, int idx) {
  if (idx < 0)
    return;
  if (std::find(indices.begin(), indices.end(), idx) == indices.end())
    indices.push_back(idx);
}

/// Names with a built-in effect summary in libcEffect() even when the JSON
/// contract file does not mention them. Kept in one place so the driver's
/// "unverified extern" probe and libcEffect() cannot drift apart: a pure libc
/// call must stay accepted while an unannotated user extern must not.
static bool isBuiltinLibcEffectName(llvm::StringRef name) {
  static const std::set<llvm::StringRef> kBuiltins = {
      "malloc",    "calloc",    "malloc_s",  "calloc_s",   "realloc",
      "realloc_s", "free",      "cplus_release", "free_s", "memcpy",
      "memmove",   "memcpy_s",  "memmove_s", "memset",     "strcpy",
      "strcpy_s",  "strncpy",   "strncpy_s", "strcat",     "strcat_s",
      "strlen",    "strnlen_s", "strcmp",    "fopen",      "fopen_s",
      "fclose",    "fread",     "fwrite",    "snprintf",   "sprintf_s",
      "snprintf_s", "scanf_s"};
  return kBuiltins.count(name) != 0;
}

/// True when a function declaration can carry safety-relevant pointer effects.
/// Any pointer parameter (including a callback/function pointer) or a pointer
/// return makes the callee's behaviour unverifiable without a body or a
/// declared contract; scalar-only externs (e.g. `int ext_add(int,int)`) are
/// pure by construction and must stay accepted in STRICT.
static bool hasPointerEffectSignature(const clang::FunctionDecl *fd) {
  auto isPtr = [](clang::QualType t) {
    return t->isPointerType() || t->isFunctionPointerType() ||
           t->isMemberPointerType();
  };
  if (isPtr(fd->getReturnType()))
    return true;
  for (const clang::ParmVarDecl *p : fd->parameters())
    if (isPtr(p->getType()))
      return true;
  return false;
}

/// Argument index denoted by `ownershipTransfer` (with `transfer`/`ownership`
/// as fallbacks) where the description refers to an argument rather than the
/// return value. Returns -1 when no index is representable.
static int ownershipTransferArg(const llvm::json::Object &spec,
                                const llvm::json::Array &params) {
  auto transfer = spec.getString("ownershipTransfer");
  auto altTransfer = spec.getString("transfer");
  auto ownership = spec.getString("ownership");
  bool viaArg = false;
  if (transfer)
    viaArg = *transfer == "arg" || transfer->contains("old alias");
  if (!viaArg && altTransfer)
    viaArg = *altTransfer == "arg";
  if (!viaArg && ownership)
    viaArg = *ownership == "consumes";
  return viaArg ? firstPointerParam(params) : -1;
}

class LocalCollector : public RecursiveASTVisitor<LocalCollector> {
public:
  explicit LocalCollector(ASTContext &ctx) : ctx_(ctx) {}

  // Only these functions are known to write through an output pointer. Taking
  // an address for an arbitrary call (or storing it) does NOT initialize it.
  static bool isOutputFunction(llvm::StringRef name) {
    return name.contains("scanf") || name == "getline" ||
           name == "pthread_mutex_init" || name == "pthread_cond_init" ||
           name == "pthread_create" || name == "scan" ||
           name == "cplus_scan_one" || name == "cplus_scan_value" ||
           name == "checked_add" || name == "cplus_checked_add";
  }

  bool VisitCallExpr(CallExpr *call) {
    const FunctionDecl *callee = call->getDirectCallee();
    llvm::StringRef calleeName = callee ? callee->getName() : llvm::StringRef();
    if (calleeName.empty())
      if (const auto *ref = dyn_cast<DeclRefExpr>(
              call->getCallee()->IgnoreParenImpCasts()))
        calleeName = ref->getDecl()->getName();
    // An unresolved callee name (e.g. the C+ `scan` macro) may write through
    // by-value arguments. Conservatively treat those arguments as outputs so
    // valid macro-based I/O is not falsely diagnosed.
    bool unknownMacroCall = calleeName.empty() || !callee ||
                            calleeName.find("scan") != llvm::StringRef::npos;
    const bool isOutput = isOutputFunction(calleeName) || unknownMacroCall;
    bool byValueOutput = unknownMacroCall || calleeName == "cplus_scan_one" ||
                         calleeName == "cplus_scan_value";
    for (const Expr *arg : call->arguments()) {
      const Expr *a = arg->IgnoreParenCasts();
      const auto *uo = dyn_cast<UnaryOperator>(a);
      if (uo && uo->getOpcode() == UO_AddrOf) {
        a = uo->getSubExpr()->IgnoreParenImpCasts();
        if (const auto *ref = dyn_cast<DeclRefExpr>(a))
          if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl())) {
            // C+ policy: only a whitelist of output functions initialises a
            // variable through `&v`. An opaque callee does not. Record the
            // latter separately so Clang's address-taken suppression can be
            // compensated for without seeding the whitelist as initialised.
            if (isOutput)
              addrTaken_.insert(vd);
            else
              nonOutputAddrTaken_.insert(vd);
          }
        continue;
      }
      if (!byValueOutput)
        continue;
      if (const auto *ref = dyn_cast<DeclRefExpr>(a))
        if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl()))
          addrTaken_.insert(vd);
    }
    return true;
  }

  bool VisitVarDecl(VarDecl *vd) {
    if (vd->hasGlobalStorage() || vd->isStaticLocal() ||
        isa<ParmVarDecl>(vd))
      return true;
    QualType t = vd->getType();
    if (!(t->isIntegerType() || t->isPointerType() || t->isEnumeralType()))
      return true;
    vars_.push_back(vd);
    return true;
  }

  const std::vector<const VarDecl *> &vars() const { return vars_; }
  const std::set<const VarDecl *> &addrTaken() const { return addrTaken_; }
  const std::set<const VarDecl *> &nonOutputAddrTaken() const {
    return nonOutputAddrTaken_;
  }

private:
  ASTContext &ctx_;
  std::vector<const VarDecl *> vars_;
  std::set<const VarDecl *> addrTaken_;
  std::set<const VarDecl *> nonOutputAddrTaken_;
};

class DefInitWalker {
public:
  DefInitWalker(ASTContext &ctx, const std::set<const VarDecl *> &tracked,
                std::set<const VarDecl *> &init, CfgReportFn report,
                std::set<const Stmt *> &reported)
      : ctx_(ctx), tracked_(tracked), init_(init), report_(report),
        reported_(reported) {}

  void walk(const Stmt *s) {
    if (!s)
      return;
    if (isa<UnaryExprOrTypeTraitExpr>(s))
      return; // sizeof etc. does not evaluate its operand

    if (const auto *bo = dyn_cast<BinaryOperator>(s)) {
      if (bo->isAssignmentOp()) {
        // Compound assignments (+=, -=, ...) READ the previous value before
        // writing, so the LHS must be checked for initialization first.
        if (bo->isCompoundAssignmentOp())
          walk(bo->getLHS());
        walk(bo->getRHS());
        markInitialized(bo->getLHS());
        return;
      }
      walk(bo->getLHS());
      walk(bo->getRHS());
      return;
    }
    if (const auto *uo = dyn_cast<UnaryOperator>(s)) {
      if (uo->isIncrementOp() || uo->isDecrementOp()) {
        // ++/-- both read and write; the read is checked by the caller's
        // DeclRefExpr handling, then the value is initialized.
        walk(uo->getSubExpr());
        markInitialized(uo->getSubExpr());
        return;
      }
      if (uo->getOpcode() == UO_AddrOf)
        return; // taking an address alone does not initialize
      walk(uo->getSubExpr());
      return;
    }
    if (const auto *call = dyn_cast<CallExpr>(s)) {
      // Writing functions initialise their output buffer through `&var`.
      const FunctionDecl *fd = call->getDirectCallee();
      if (fd && LocalCollector::isOutputFunction(fd->getName())) {
        // Output pointers may appear at any argument position (e.g.
        // checked_add(a, b, &out)). Mark every address-of argument as
        // initialized through the call's contract.
        for (const Expr *argument : call->arguments()) {
          const Expr *arg = argument->IgnoreParenCasts();
          if (const auto *uo = dyn_cast<UnaryOperator>(arg))
            if (uo->getOpcode() == UO_AddrOf)
              markInitialized(uo->getSubExpr());
        }
      }
      // Raw bounded-copy APIs also write through their first argument.
      std::string n = fd ? fd->getName().str() : std::string();
      bool rawWriter = n.find("memcpy") != std::string::npos ||
                       n.find("memmove") != std::string::npos ||
                       n == "memset" || n.find("strcpy") != std::string::npos ||
                       n.find("va_start") != std::string::npos ||
                       n.find("printf") != std::string::npos;
      if (rawWriter && call->getNumArgs() >= 1) {
        const Expr *arg = call->getArg(0)->IgnoreParenCasts();
        if (n.find("va_start") != std::string::npos) {
          markInitialized(arg); // __builtin_va_start takes the lvalue directly
        } else if (const auto *uo = dyn_cast<UnaryOperator>(arg)) {
          if (uo->getOpcode() == UO_AddrOf)
            markInitialized(uo->getSubExpr());
        }
      }
      for (const Stmt *child : s->children())
        walk(child);
      return;
    }
    if (const auto *ds = dyn_cast<DeclStmt>(s)) {
      for (const Decl *d : ds->decls())
        if (const auto *vd = dyn_cast<VarDecl>(d)) {
          if (vd->hasInit()) {
            walk(vd->getInit());
            if (tracked_.count(vd))
              init_.insert(vd);
          }
        }
      return;
    }
    if (const auto *dr = dyn_cast<DeclRefExpr>(s)) {
      if (const auto *vd = dyn_cast<VarDecl>(dr->getDecl())) {
        if (tracked_.count(vd) && !init_.count(vd) &&
            reported_.insert(dr).second)
          report_(dr->getBeginLoc(),
                  "variable '" + vd->getNameAsString() +
                      "' is uninitialized (read before it is initialized)");
      }
      return;
    }
    for (const Stmt *child : s->children())
      walk(child);
  }

private:
  void markInitialized(const Expr *e) {
    e = e->IgnoreParenImpCasts();
    if (const auto *dr = dyn_cast<DeclRefExpr>(e))
      if (const auto *vd = dyn_cast<VarDecl>(dr->getDecl()))
        if (tracked_.count(vd))
          init_.insert(vd);
  }

  ASTContext &ctx_;
  const std::set<const VarDecl *> &tracked_;
  std::set<const VarDecl *> &init_;
  CfgReportFn report_;
  std::set<const Stmt *> &reported_;
};

} // namespace

void checkDefiniteInitialization(ASTContext &ctx, FunctionDecl *fd,
                                 const CfgReportFn &report) {
  if (!fd->doesThisDeclarationHaveABody() || !fd->getBody())
    return;
  CFG::BuildOptions opts;
  std::unique_ptr<CFG> cfg =
      CFG::buildCFG(fd, fd->getBody(), &ctx, opts);
  if (!cfg)
    return;

  LocalCollector collector(ctx);
  collector.TraverseStmt(fd->getBody());
  std::set<const VarDecl *> tracked(collector.vars().begin(),
                                    collector.vars().end());
  if (tracked.empty())
    return;

  // Parameters are initialized on entry.
  std::set<const VarDecl *> entryInit;
  for (const ParmVarDecl *p : fd->parameters())
    if (tracked.count(p))
      entryInit.insert(p);
  // Variables passed by address to output parameters (scanf, pthread_create,
  // ...) are treated as initialized on entry; Clang's own warnings cover
  // genuinely uninitialized direct reads.
  for (const VarDecl *vd : collector.addrTaken())
    if (tracked.count(vd))
      entryInit.insert(vd);

  // Upstream Clang dataflow replaces the previous hand-rolled worklist.
  // The handler filters to variables C+ tracks and to definitely-uninitialized
  // uses, preserving C+'s existing diagnostic wording and false-positive policy.
  // Reported variables are recorded so the C+ policy pass below never emits a
  // duplicate diagnostic for the same variable.
  std::set<const VarDecl *> upstreamReported;
  class UpstreamUninitHandler : public clang::UninitVariablesHandler {
  public:
    UpstreamUninitHandler(const std::set<const VarDecl *> &tracked,
                          const CfgReportFn &report,
                          std::set<const VarDecl *> &reported)
        : tracked_(tracked), report_(report), reported_(reported) {}

    void handleUseOfUninitVariable(const VarDecl *vd,
                                   const clang::UninitUse &use) override {
      if (!vd || !tracked_.count(vd))
        return;
      const Expr *user = use.getUser();
      if (!user)
        return;
      // Always / AfterDecl / AfterCall are definite on the analysed paths;
      // Sometimes / Maybe match the old must-analysis's conservative policy.
      switch (use.getKind()) {
      case clang::UninitUse::Always:
      case clang::UninitUse::AfterDecl:
      case clang::UninitUse::AfterCall:
        break;
      case clang::UninitUse::Sometimes:
      case clang::UninitUse::Maybe:
        return;
      }
      reported_.insert(vd);
      report_(user->getBeginLoc(),
              "variable '" + vd->getNameAsString() +
                  "' is uninitialized (read before it is initialized)");
    }
    void handleSelfInit(const VarDecl *vd) override {
      if (vd && tracked_.count(vd)) {
        reported_.insert(vd);
        report_(vd->getLocation(),
                "variable '" + vd->getNameAsString() +
                    "' is uninitialized (read before it is initialized)");
      }
    }

  private:
    const std::set<const VarDecl *> &tracked_;
    CfgReportFn report_;
    std::set<const VarDecl *> &reported_;
  };

  clang::AnalysisDeclContext ac(nullptr, fd);
  UpstreamUninitHandler handler(tracked, report, upstreamReported);
  clang::UninitVariablesAnalysisStats stats{};
  clang::runUninitializedVariablesAnalysis(*fd, *cfg, ac, handler, stats);

  // Preserve C+'s diagnostic policy where upstream Clang deliberately
  // suppresses reports: Clang treats a variable as initialised when its
  // address is passed to *any* function, while C+ only recognises a whitelist
  // of output functions (scanf/getline/pthread_init/...). A variable whose
  // address escapes to an opaque callee would otherwise be accepted even when
  // it is read before any definite initialisation (e.g. `void h(int*);
  // int f(void){int x; h(&x); return x;}`). Run the original must-analysis
  // worklist for precisely those variables, seeding the same entry state and
  // skipping anything upstream already reported. Valid output-parameter and
  // initialised programs are untouched, so this restores detection without
  // introducing new diagnostics for correct code.
  std::set<const VarDecl *> needsPolicyPass;
  for (const VarDecl *vd : collector.nonOutputAddrTaken())
    if (tracked.count(vd) && !entryInit.count(vd) &&
        !upstreamReported.count(vd))
      needsPolicyPass.insert(vd);
  if (needsPolicyPass.empty())
    return;

  const size_t n = cfg->size();
  std::vector<CFGBlock *> blocks(n, nullptr);
  for (CFGBlock *b : *cfg)
    if (b)
      blocks[b->getBlockID()] = b;
  std::vector<std::set<const VarDecl *>> in(n);
  std::vector<bool> reached(n, false);
  std::set<const Stmt *> reportedStmts;

  std::deque<size_t> work;
  const size_t entryId = cfg->getEntry().getBlockID();
  in[entryId] = entryInit;
  reached[entryId] = true;
  work.push_back(entryId);

  while (!work.empty()) {
    const size_t id = work.front();
    work.pop_front();
    CFGBlock *block = blocks[id];
    if (!block)
      continue;
    std::set<const VarDecl *> cur = in[id];
    for (const CFGElement &el : *block) {
      std::optional<CFGStmt> stmt = el.getAs<CFGStmt>();
      if (!stmt)
        continue;
      DefInitWalker walker(ctx, needsPolicyPass, cur, report, reportedStmts);
      walker.walk(stmt->getStmt());
    }
    for (const CFGBlock *succ : block->succs()) {
      if (!succ)
        continue;
      const size_t sid = succ->getBlockID();
      if (!reached[sid]) {
        in[sid] = cur;
        reached[sid] = true;
        work.push_back(sid);
      } else {
        std::set<const VarDecl *> merged;
        std::set_intersection(in[sid].begin(), in[sid].end(), cur.begin(),
                              cur.end(), std::inserter(merged, merged.begin()));
        if (merged != in[sid]) {
          in[sid] = merged;
          work.push_back(sid);
        }
      }
    }
  }
}

} // namespace cplus

namespace cplus {
static ContractState &loadedContracts() {
  ContractState &st = contractState();
  if (st.attempted)
    return st;
  st.attempted = true;

  // Env override first: needed for tests and for relocatable installs.
  // Track whether the user explicitly supplied a non-empty path: an unloadable
  // explicit path must fail closed (no silent fallback to the compiled-in
  // default), otherwise a program could be verified against a different
  // contract than the one the user asked for. An empty value means "no
  // override" and still uses the compiled-in default.
  std::string path;
  bool explicitEnvPath = false;
  if (const char *env = std::getenv("CPLUS_CONTRACT_PATH")) {
    path = env;
    // An explicitly EMPTY value means "no override": fall back to the
    // compiled-in default. A non-empty but unloadable path is a real override
    // and must fail closed instead of being silently replaced by the default.
    explicitEnvPath = !path.empty();
  }
#ifdef CPLUS_CONTRACT_PATH
  if (path.empty())
    path = CPLUS_CONTRACT_PATH;
#endif
  if (path.empty()) {
    path = "compiler/Analysis/libc_contracts.json";
#ifdef CPLUS_SOURCE_DIR
    std::string sourcePath = std::string(CPLUS_SOURCE_DIR) +
                             "/compiler/Analysis/libc_contracts.json";
    if (llvm::sys::fs::exists(sourcePath))
      path = sourcePath;
#endif
  }
  auto buf = llvm::MemoryBuffer::getFile(path);
  // The installed-layout fallback is only for the implicit (compiled-in or
  // default) path. Never mask a bad CPLUS_CONTRACT_PATH with it.
  if (!buf && !explicitEnvPath) {
    std::string exe = llvm::sys::fs::getMainExecutable("c+", nullptr);
    if (!exe.empty()) {
      llvm::SmallString<256> dir(llvm::sys::path::parent_path(exe));
      llvm::sys::path::append(dir, "..", "share", "cplus",
                              "libc_contracts.json");
      if (llvm::sys::fs::exists(dir))
        path = std::string(dir.str());
    }
    buf = llvm::MemoryBuffer::getFile(path);
  }
  if (!buf) {
    st.issues.push_back({"", "libc contract file not found: " + path});
    return st;
  }
  auto parsed = llvm::json::parse((*buf)->getBuffer());
  if (!parsed) {
    llvm::consumeError(parsed.takeError());
    st.issues.push_back({"", "libc contract file is not valid JSON: " + path});
    return st;
  }
  const llvm::json::Object *root = parsed->getAsObject();
  if (!root) {
    st.issues.push_back({"", "libc contract root is not an object"});
    return st;
  }
  auto version = root->getInteger("version");
  if (!version || *version != 2)
    st.issues.push_back({"", "unsupported libc contract version (expected 2)"});
  const llvm::json::Object *functions = root->getObject("functions");
  if (!functions) {
    st.issues.push_back({"", "libc contract has no 'functions' object"});
    return st;
  }
  for (const auto &entry : *functions) {
    const std::string fn = entry.first.str();
    const llvm::json::Object *spec = entry.second.getAsObject();
    if (!spec) {
      st.issues.push_back({fn, "function contract is not an object"});
      continue;
    }
    for (const auto &key : *spec) {
      if (!isKnownContractField(key.first))
        st.issues.push_back({fn, "unsupported contract field '" +
                                     key.first.str() + "'"});
    }
    for (const auto &req : {"params", "nullableArgs", "ownership", "transfer",
                            "extent", "effects", "borrow", "failure",
                            "diagnostics", "dynamic", "paramTypes",
                            "pointerBounds", "allocationSize",
                            "ownershipTransfer", "borrowLifetime",
                            "readEffects", "writeEffects", "freeEffects",
                            "returnState", "failureBehavior",
                            "threadAssumptions"})
      if (!isRequiredContractField(req))
        continue;
      else if (!spec->get(req))
        st.issues.push_back({fn, std::string("missing contract field '") + req +
                                     "'"});

    // Every array-typed field must actually be an array.
    for (const auto &arr : {"params", "nullableArgs", "effects", "borrow",
                            "dynamic", "paramTypes", "readEffects",
                            "writeEffects", "freeEffects",
                            "threadAssumptions", "optional"})
      if (const auto *v = spec->get(arr))
        if (!v->getAsArray())
          st.issues.push_back({fn, std::string("contract field '") + arr +
                                       "' must be an array"});

    FunctionEffect e;
    // diagnostics category: honour the contract's own category.
    if (auto diag = spec->getString("diagnostics")) {
      if (diag->starts_with("CPLUS-SAFE-"))
        e.category = diag->str();
      else
        st.issues.push_back({fn, "invalid diagnostics category"});
    }
    // nullableArgs: array of argument indices. Keep the legacy boolean and
    // additionally record the exact indices for precise consumers.
    if (const auto *nullable = spec->getArray("nullableArgs")) {
      for (const auto &v : *nullable) {
        if (auto index = v.getAsInteger()) {
          e.nullableArg = true;
          e.nullableArgIndices.push_back(static_cast<int>(*index));
        } else {
          st.issues.push_back({fn, "nullableArgs must contain integers"});
        }
      }
    }
    // effects: only known verbs may be relied upon.
    if (const auto *effects = spec->getArray("effects")) {
      for (const auto &v : *effects) {
        auto s = v.getAsString();
        if (!s) {
          st.issues.push_back({fn, "effects must contain strings"});
          continue;
        }
        llvm::StringRef verb = s->take_while([](char c) {
          return !std::isspace(static_cast<unsigned char>(c));
        });
        if (verb == "allocates") {
          e.allocates = true;
          e.returnsOwned = true;
        } else if (verb == "frees" || verb == "frees-on-success") {
          e.frees = true;
        } else if (verb == "writes") {
          e.writes = true;
        } else if (verb == "reads" || verb == "borrows") {
          e.borrows = true;
        } else if (verb == "zero-init" || verb == "checked" ||
                   verb == "overlap-safe" || verb == "advances") {
          // Descriptive v2 verbs with no ownership consequence: recognised
          // and validated, so they are not treated as unsupported contracts.
        } else {
          // A verb we cannot interpret: the contract is not fully understood.
          st.issues.push_back({fn, "unsupported effect '" + verb.str() + "'"});
        }
      }
    }
    if (auto ownership = spec->getString("ownership"))
      if (*ownership == "returns_owned")
        e.returnsOwned = true;
    // Argument indices encoded in the contract, when present.
    auto argIndex = [&](llvm::StringRef name) -> int {
      const auto *params = spec->getArray("params");
      if (!params)
        return -1;
      int idx = 0;
      for (const auto &v : *params) {
        if (auto p = v.getAsString())
          if (p->contains(name))
            return idx;
        ++idx;
      }
      return -1;
    };
    if (auto ext = spec->getString("extent"))
      e.sizeArg = argIndex(*ext);
    if (auto alloc = spec->getString("allocationSize"))
      if (e.sizeArg < 0)
        e.sizeArg = argIndex(*alloc);
    if (auto ptr = spec->getString("pointerBounds"))
      e.destArg = argIndex(*ptr);
    if (auto fail = spec->getString("failureBehavior"))
      e.mayFail = !fail->empty();
    e.checkedSize = e.sizeArg >= 0 && e.writes;
    const auto *effectArray = spec->getArray("effects");
    if (e.frees && effectArray)
      for (const auto &eff : *effectArray) {
        if (auto s = eff.getAsString()) {
          llvm::StringRef rest = s->substr(s->find_first_of(' '));
          rest = rest.trim();
          if (!rest.empty() && rest.getAsInteger(10, e.freedArg))
            e.freedArg = -1;
        }
      }

    // --- Precise v2 argument-index metadata -------------------------------
    // `ownershipTransfer`, `freeEffects` and `writeEffects` were recognised
    // but previously unused. Parse them into explicit argument indices where
    // the contract supplies a real signature in `params`. Placeholder `_s`
    // signatures and return-value-only transfers stay empty. No new
    // diagnostics are produced by this metadata extraction.
    const llvm::json::Array *params = spec->getArray("params");
    auto indexFor = [params](llvm::StringRef token) {
      return params ? paramIndexFor(*params, token) : -1;
    };
    auto firstPtr = [params]() {
      return params ? firstPointerParam(*params) : -1;
    };
    const int transferIdx =
        params ? ownershipTransferArg(*spec, *params) : -1;
    addIndex(e.ownershipTransferArgs, transferIdx);

    if (const auto *freeEffects = spec->getArray("freeEffects")) {
      for (const auto &v : *freeEffects) {
        auto s = v.getAsString();
        if (!s)
          continue;
        EffectExpr ex = parseEffectExpr(*s);
        if (!ex.verb.starts_with("frees"))
          continue;
        int idx = ex.target.empty() ? -1 : indexFor(ex.target);
        if (idx < 0)
          idx = transferIdx; // the transferred argument is the freed one
        addIndex(e.freeArgs, idx);
      }
    }

    if (const auto *writeEffects = spec->getArray("writeEffects")) {
      for (const auto &v : *writeEffects) {
        auto s = v.getAsString();
        if (!s)
          continue;
        EffectExpr ex = parseEffectExpr(*s);
        if (ex.verb != "write" && !ex.verb.starts_with("writes"))
          continue;
        int idx = ex.target.empty() ? -1 : indexFor(ex.target);
        if (idx < 0)
          idx = firstPtr(); // default: the destination pointer argument
        addIndex(e.writeArgs, idx);
      }
    }
    st.contracts[fn] = e;
  }
  // A contract set with ANY validation issue (missing/unknown field, bad type,
  // unrecognised effect, bad version) is not a sound safety summary. Treat it
  // as not loaded so STRICT fails closed even when no call names that exact
  // function; checkAstSafety already reports each individual issue at a call.
  st.loaded = st.issues.empty();
  return st;
}

FunctionEffect libcEffect(llvm::StringRef name) {
  auto &contracts = loadedContracts().contracts;
  auto it = contracts.find(name.str());
  if (it != contracts.end())
    return it->second;
  FunctionEffect e;
  if (name == "malloc" || name == "calloc" || name == "malloc_s" ||
      name == "calloc_s") {
    e.allocates = true;
    e.returnsOwned = true;
    e.mayFail = true;
    e.category = "CPLUS-SAFE-ALLOC";
  } else if (name == "realloc" || name == "realloc_s") {
    e.allocates = true;
    e.returnsOwned = true;
    // Success-only invalidation; the AST checker applies this state only on
    // the verified non-NULL branch (see detectReallocSuccess / TraverseIfStmt).
    e.frees = true;
    e.freedArg = 0;
    e.mayFail = true;
    e.category = "CPLUS-SAFE-REALLOC";
  } else if (name == "free" || name == "cplus_release" || name == "free_s") {
    e.frees = true;
    e.freedArg = 0;
    e.nullableArg = true; // free(NULL) is defined
    e.category = "CPLUS-SAFE-FREE";
  } else if (name == "memcpy" || name == "memmove" || name == "memcpy_s" ||
             name == "memmove_s" || name == "memset") {
    e.borrows = true;
    e.writes = true;
    e.destArg = 0;
    e.sizeArg = (name == "memcpy_s" || name == "memmove_s") ? 1 : -1;
    e.checkedSize = e.sizeArg >= 0;
    e.category = "CPLUS-SAFE-BOUNDS";
  } else if (name == "strcpy" || name == "strcpy_s" ||
             name == "strncpy" || name == "strncpy_s" ||
             name == "strcat" || name == "strcat_s") {
    e.borrows = true;
    e.writes = true;
    e.destArg = 0;
    e.sizeArg = (name.find("_s") != std::string::npos ||
                 name == "strncpy") ? 1 : -1;
    e.checkedSize = e.sizeArg >= 0;
    e.category = "CPLUS-SAFE-CRT";
  } else if (name == "strlen" || name == "strnlen_s" || name == "strcmp") {
    e.borrows = true;
    e.category = "CPLUS-SAFE-CRT";
  } else if (name == "fopen" || name == "fopen_s") {
    e.allocates = true;
    e.returnsOwned = true;
    e.mayFail = true;
    e.category = "CPLUS-SAFE-CRT";
  } else if (name == "fclose") {
    e.frees = true;
    e.freedArg = 0;
    e.category = "CPLUS-SAFE-CRT";
  } else if (name == "fread" || name == "fwrite") {
    e.borrows = true;
    e.writes = true;
    e.mayFail = true;
    e.category = "CPLUS-SAFE-CRT";
  } else if (name == "snprintf" || name == "sprintf_s" ||
             name == "snprintf_s" || name == "scanf_s") {
    e.writes = true;
    e.checkedSize = name.starts_with("sn") || name.find("_s") != std::string::npos;
    e.mayFail = true;
    e.category = "CPLUS-SAFE-CRT";
  }
  return e;
}
const std::vector<ContractIssue> &contractIssues() {
  return loadedContracts().issues;
}

bool contractsLoaded() {
  return loadedContracts().loaded;
}

/// Parse-relevant flags shared by the probe and the definition scanner. Kept
/// in one place so both ASTs are always built from the same view of the TU.
static std::vector<std::string>
safetyParseArgs(const std::vector<std::string> &args) {
  std::vector<std::string> parseArgs;
  for (const auto &arg : args) {
    if (arg.rfind("-I", 0) == 0 || arg.rfind("-D", 0) == 0 ||
        arg.rfind("-std=", 0) == 0)
      parseArgs.push_back(arg);
  }
#ifdef CPLUS_CLANG_RESOURCE_DIR
  parseArgs.push_back(std::string("-resource-dir=") + CPLUS_CLANG_RESOURCE_DIR);
#endif
  parseArgs.push_back("-fsyntax-only");
  return parseArgs;
}

bool hasDeclaredEffect(llvm::StringRef name) {
  if (isBuiltinLibcEffectName(name))
    return true;
  // The rewriter lowers C+ operations into calls to the compiler/runtime API
  // (cplus_register_allocation, __cplus_bounds_check, ...). These are emitted
  // by the compiler itself, have known semantics, and are linked from the C+
  // runtime rather than defined in the source TU. They are not user FFI and
  // must not trip the unverified-extern gate.
  if (name.starts_with("cplus_") || name.starts_with("__cplus_"))
    return true;
  return loadedContracts().contracts.count(name.str()) != 0;
}

SafetyAstProbe probeSafetyAst(const std::string &source,
                              const std::vector<std::string> &args,
                              const std::string &fileName) {
  SafetyAstProbe result;

  // Keep only the flags that affect parsing, matching checkAstSafety(). A
  // synthetic .c name forces C (not C++) language inference by the tooling
  // layer, and the resource dir mirrors the main safety AST build.
  // NOTE: pass-through flags that change the parse but are discarded here
  // (-include/-imacros/-isystem/-x/-include-pch) are hard-rejected by the
  // driver in STRICT, so this probe can never analyse a different program
  // than the backend compiles under the safety gate.
  std::vector<std::string> parseArgs = safetyParseArgs(args);
  const std::string astFile = fileName + ".cprobe.c";

  clang::IgnoringDiagConsumer ignore;
  std::unique_ptr<clang::ASTUnit> ast =
      clang::tooling::buildASTFromCodeWithArgs(
          source, parseArgs, astFile, "cplus-ast-probe",
          std::make_shared<clang::PCHContainerOperations>(),
          clang::tooling::getClangStripDependencyFileAdjuster(), {}, &ignore);
  if (!ast || ast->getDiagnostics().hasErrorOccurred()) {
    result.parsed = false;
    return result;
  }

  clang::ASTContext &ctx = ast->getASTContext();
  clang::SourceManager &sm = ctx.getSourceManager();

  // A function is "used" when it is directly called OR merely referenced
  // (e.g. `void (*f)(int *) = ext; f(p);`). getDirectCallee() alone misses the
  // latter, which is how the cross-TU disclosure used to be evaded. Collect
  // both so an indirect call to a bodiless extern is still disclosed and,
  // when it has unsafe pointer effects, rejected by STRICT check.
  std::set<std::string> used;
  struct RefCollector : clang::RecursiveASTVisitor<RefCollector> {
    std::set<std::string> &names;
    explicit RefCollector(std::set<std::string> &n) : names(n) {}
    bool VisitCallExpr(clang::CallExpr *call) {
      if (const clang::FunctionDecl *fd = call->getDirectCallee())
        names.insert(fd->getNameAsString());
      return true;
    }
    bool VisitDeclRefExpr(clang::DeclRefExpr *ref) {
      if (const auto *fd = dyn_cast<clang::FunctionDecl>(ref->getDecl()))
        names.insert(fd->getNameAsString());
      return true;
    }
  } refs(used);
  refs.TraverseDecl(ctx.getTranslationUnitDecl());

  std::set<std::string> unresolved;
  std::set<std::string> unverified;
  std::set<std::string> headerExterns;
  struct DeclCollector : clang::RecursiveASTVisitor<DeclCollector> {
    clang::SourceManager &sm;
    const std::set<std::string> &used;
    std::set<std::string> &out;
    std::set<std::string> &unverified;
    std::set<std::string> &headerExterns;
    DeclCollector(clang::SourceManager &s, const std::set<std::string> &u,
                  std::set<std::string> &o, std::set<std::string> &v,
                  std::set<std::string> &h)
        : sm(s), used(u), out(o), unverified(v), headerExterns(h) {}
    bool VisitFunctionDecl(clang::FunctionDecl *fd) {
      // hasBody() considers redeclarations, so a prototype followed by a
      // definition in the same TU is not mistaken for an external function.
      if (fd->isImplicit() || fd->hasBody())
        return true;
      const std::string name = fd->getNameAsString();
      if (!used.count(name))
        return true;
      clang::SourceLocation loc = fd->getLocation();
      // System-header declarations (printf/malloc/...) keep the disclosure
      // exemption: they are the pure libc calls STRICT must accept. The
      // driver rejects pass-through -isystem in STRICT, so user headers cannot
      // disguise themselves as system headers to dodge this check.
      if (loc.isValid() && sm.isInSystemHeader(loc))
        return true;
      out.insert(name);
      // No body and no declared effect => the pointer effects of this extern
      // are invisible to this TU. Flag for the driver's STRICT check gate.
      if (hasPointerEffectSignature(fd) && !hasDeclaredEffect(fd->getName())) {
        unverified.insert(name);
        // Remember whether the prototype came from a header. The driver uses
        // this to suppress the hard error when a sibling TU supplies the body
        // for a normal multi-TU API, while a source-local extern (the red
        // case) stays an error.
        if (loc.isValid() && !sm.isInMainFile(loc))
          headerExterns.insert(name);
      }
      return true;
    }
  } decls(sm, used, unresolved, unverified, headerExterns);
  decls.TraverseDecl(ctx.getTranslationUnitDecl());

  result.unresolvedFunctions.assign(unresolved.begin(), unresolved.end());
  result.unverifiedPointerExterns.assign(unverified.begin(), unverified.end());
  result.headerDeclaredExterns.assign(headerExterns.begin(), headerExterns.end());
  return result;
}

std::set<std::string> definedFunctionNames(const std::string &source,
                                           const std::vector<std::string> &args,
                                           const std::string &fileName) {
  std::set<std::string> defined;
  clang::IgnoringDiagConsumer ignore;
  std::unique_ptr<clang::ASTUnit> ast =
      clang::tooling::buildASTFromCodeWithArgs(
          source, safetyParseArgs(args), fileName + ".cdefs.c",
          "cplus-ast-defs", std::make_shared<clang::PCHContainerOperations>(),
          clang::tooling::getClangStripDependencyFileAdjuster(), {}, &ignore);
  if (!ast || ast->getDiagnostics().hasErrorOccurred())
    return defined;
  clang::ASTContext &ctx = ast->getASTContext();
  clang::SourceManager &sm = ctx.getSourceManager();
  struct DefCollector : clang::RecursiveASTVisitor<DefCollector> {
    clang::SourceManager &sm;
    std::set<std::string> &out;
    DefCollector(clang::SourceManager &s, std::set<std::string> &o)
        : sm(s), out(o) {}
    bool VisitFunctionDecl(clang::FunctionDecl *fd) {
      if (fd->isImplicit() || !fd->doesThisDeclarationHaveABody() ||
          !fd->getBody())
        return true;
      clang::SourceLocation loc = fd->getLocation();
      if (loc.isValid() && sm.isInSystemHeader(loc))
        return true;
      out.insert(fd->getNameAsString());
      return true;
    }
  } defs(sm, defined);
  defs.TraverseDecl(ctx.getTranslationUnitDecl());
  return defined;
}
} // namespace cplus
