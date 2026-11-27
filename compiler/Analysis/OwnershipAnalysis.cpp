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
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include <clang/Basic/SourceManager.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <tuple>
#include <vector>

using namespace clang;

namespace cplus {
namespace {

struct ContractState {
  std::map<std::string, FunctionEffect> contracts;
  std::vector<ContractIssue> issues;
  bool attempted = false;
  bool loaded = false;
  // The canonical absolute path the contracts were actually read from and the
  // exact bytes parsed. Both are pinned on the first load so a symlink retarget
  // or an in-place rewrite after analysis cannot silently swap the contract set
  // that the STRICT gate relies on.
  std::string resolvedPath;
  std::string parsedBytes;
  bool swapped = false;
  // True when the contract set was loaded from an explicit CPLUS_CONTRACT_PATH
  // override. Such a contract is a caller-made claim, not evidence that a
  // defining body was analysed; the STRICT bodiless-extern gate must not be
  // silenced by it alone.
  bool envPath = false;
};

ContractState &contractState() {
  static ContractState state;
  return state;
}

/// Resolve a contract path once to a canonical absolute path and verify it is
/// a regular file. '..' components and symlinks are collapsed, so a relative
/// CPLUS_CONTRACT_PATH or a symlink cannot name a different file after the
/// analysis than the one that was validated. Directories, devices and broken
/// symlinks fail closed instead of being parsed as contracts.
static bool canonicalContractPath(const std::string &raw, std::string &out) {
  if (raw.empty())
    return false;
  llvm::SmallString<256> abs{llvm::StringRef(raw)};
  if (llvm::sys::fs::make_absolute(abs))
    return false;
  llvm::SmallString<256> real;
  if (llvm::sys::fs::real_path(abs, real))
    return false;
  llvm::sys::fs::file_status st;
  if (llvm::sys::fs::status(real, st))
    return false;
  if (!llvm::sys::fs::is_regular_file(st))
    return false;
  out = std::string(real.str());
  return true;
}

/// Re-check the pinned contract file on every later query. A symlink retarget,
/// a CWD change, or an in-place replacement after the contracts were parsed
/// must fail closed rather than mixing two contract sets within one process.
static bool contractUnchanged(const ContractState &st) {
  if (st.resolvedPath.empty())
    return false;
  std::string stillCanonical;
  if (!canonicalContractPath(st.resolvedPath, stillCanonical) ||
      stillCanonical != st.resolvedPath)
    return false;
  auto buf = llvm::MemoryBuffer::getFile(st.resolvedPath);
  if (!buf)
    return false;
  return llvm::StringRef((*buf)->getBuffer()) == llvm::StringRef(st.parsedBytes);
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
      "malloc",     "calloc",     "malloc_s",  "calloc_s",   "realloc",
      "realloc_s",  "free",       "cplus_release", "free_s", "memcpy",
      "memmove",    "memset",     "memcpy_s",  "memmove_s",  "memset_s",
      "strcpy",     "strcpy_s",   "strncpy",   "strncpy_s",  "strcat",
      "strcat_s",   "strncat",    "strncat_s", "strlen",     "strnlen_s",
      "strcmp",     "fopen",      "fopen_s",   "fclose",     "fclose_s",
      "fread",      "fread_s",    "fwrite",    "fwrite_s",   "snprintf",
      "sprintf_s",  "snprintf_s", "scanf_s"};
  return kBuiltins.count(name) != 0;
}

/// Compiler/runtime entry points are emitted by the rewriter, have known
/// semantics and are linked from the C+ runtime; they are trusted regardless of
/// where the contract file came from. Shared by hasDeclaredEffect() and the
/// authoritative-summary predicate so the two cannot drift apart.
///
/// This MUST be a closed allowlist: a wildcard `cplus_*` / `__cplus_*` prefix
/// would let any user header declare `extern int *cplus_backdoor(int *)` and
/// silence the STRICT unverified-extern gate without a body or a contract.
/// isReservedHelperName() is that closed list (explicit symbols plus a few
/// closed runtime families); the extra exact names below are genuine runtime
/// ABI entry points declared by runtime/include/cplus/cplus.h that are not
/// otherwise reserved. A user helper such as `cplus_core_helper` stays
/// untrusted and must supply a body or a validated contract like any other
/// user function.
static bool isCompilerHelperEffectName(llvm::StringRef name) {
  if (isReservedHelperName(name))
    return true;
  static const std::set<llvm::StringRef> kRuntimeAbi = {
      "cplus_epoch",         "cplus_live_table_stats",
      "cplus_scan_value_at", "cplus_exception_enter",
      "cplus_exception_pop", "cplus_exception_raise"};
  return kRuntimeAbi.count(name) != 0;
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

  // Only these EXACT functions are known to write through an output pointer.
  // A substring match such as `find("scan")` would let `prescan(&x)` or a
  // read-only `read_only_scan(const int *)` silently suppress an uninitialized
  // read. Taking an address for an arbitrary call (or storing it) does NOT
  // initialize it.
  static bool isOutputFunction(llvm::StringRef name) {
    static const std::set<llvm::StringRef> kOutputs = {
        "scanf",         "scanf_s",       "__isoc99_scanf",
        "sscanf",        "sscanf_s",      "fscanf",
        "fscanf_s",      "vscanf",        "vsscanf",
        "vfscanf",       "getline",       "pthread_mutex_init",
        "pthread_cond_init", "pthread_create", "scan",
        "cplus_scan_one", "cplus_scan_value", "cplus_scan_value_at",
        "checked_add",   "cplus_checked_add"};
    return kOutputs.count(name) != 0;
  }

  bool VisitCallExpr(CallExpr *call) {
    const FunctionDecl *callee = call->getDirectCallee();
    llvm::StringRef calleeName = callee ? callee->getName() : llvm::StringRef();
    if (!callee)
      if (const auto *ref = dyn_cast<DeclRefExpr>(
              call->getCallee()->IgnoreParenImpCasts()))
        calleeName = ref->getDecl()->getName();
    // Only a truly unresolved callee name (e.g. a macro-expanded call whose
    // callee cannot be named) is conservatively treated as an unknown output.
    // An indirect call through a function pointer is NOT: its signature says
    // whether a parameter is a const (read-only) pointer, and a body we cannot
    // see cannot be assumed to initialize `&x`. The policy worklist below
    // diagnoses the escape instead of seeding it as initialized.
    bool unknownMacroCall = calleeName.empty() && !callee;
    const bool isOutput = isOutputFunction(calleeName) || unknownMacroCall;
    // By-value scalars are initialised only by the exact C+ scan helpers.
    // An opaque/indirect call must never make `int x; fn(x); return x;` clean.
    bool byValueOutput = calleeName == "cplus_scan_one" ||
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

    // Model `sscanf("0", "%p", &p)`: parsing the literal "0" with %p writes a
    // NULL pointer into p. The variable is still *written* (so definite-init
    // must not call it uninitialised), but its value is provably null. Record
    // that fact for the STRICT null analysis instead of emitting a speculative
    // diagnostic here: a guarded dereference (`if (p) ...`) remains valid.
    if (calleeName == "sscanf" && call->getNumArgs() >= 2) {
      auto literalBytes = [](const Expr *e) -> llvm::StringRef {
        const auto *sl = dyn_cast_or_null<StringLiteral>(e->IgnoreParenImpCasts());
        return sl ? sl->getBytes() : llvm::StringRef();
      };
      const llvm::StringRef src = literalBytes(call->getArg(0));
      const llvm::StringRef fmt = literalBytes(call->getArg(1));
      if (src.trim() == "0" && fmt.contains("%p")) {
        for (unsigned i = 2; i < call->getNumArgs(); ++i) {
          const Expr *a = call->getArg(i)->IgnoreParenCasts();
          const auto *uo = dyn_cast<UnaryOperator>(a);
          if (!uo || uo->getOpcode() != UO_AddrOf)
            continue;
          const Expr *sub = uo->getSubExpr()->IgnoreParenImpCasts();
          const auto *ref = dyn_cast<DeclRefExpr>(sub);
          const auto *vd = ref ? dyn_cast<VarDecl>(ref->getDecl()) : nullptr;
          if (vd && vd->getType()->isPointerType())
            nullWrittenVars_.insert(vd);
        }
      }
    }
    return true;
  }

  bool VisitVarDecl(VarDecl *vd) {
    if (vd->hasGlobalStorage() || vd->isStaticLocal() ||
        isa<ParmVarDecl>(vd))
      return true;
    QualType t = vd->getType();
    // Aggregates are tracked so element/field reads can be diagnosed by the
    // subobject pass below. Scalar tracking is unchanged. Clang's upstream
    // uninit analysis deliberately skips array elements, so relying on it
    // alone would leave `int a[3]; return a[0];` unreported.
    if (!(t->isIntegerType() || t->isPointerType() || t->isEnumeralType() ||
          t->isArrayType() || t->isRecordType()))
      return true;
    vars_.push_back(vd);
    return true;
  }

  const std::vector<const VarDecl *> &vars() const { return vars_; }
  const std::set<const VarDecl *> &addrTaken() const { return addrTaken_; }
  const std::set<const VarDecl *> &nonOutputAddrTaken() const {
    return nonOutputAddrTaken_;
  }
  const std::set<const VarDecl *> &nullWrittenVars() const {
    return nullWrittenVars_;
  }

private:
  ASTContext &ctx_;
  std::vector<const VarDecl *> vars_;
  std::set<const VarDecl *> addrTaken_;
  std::set<const VarDecl *> nonOutputAddrTaken_;
  std::set<const VarDecl *> nullWrittenVars_;
};

// --- Subobject-sensitive definite initialization ---------------------------
// Clang's upstream uninit analysis intentionally skips array elements and does
// not reason about individual struct fields, so `int a[3]; return a[0];` would
// otherwise be accepted. A SubKey names one provably-identifiable subobject of
// a local aggregate; non-constant indices and arrow accesses are deliberately
// NOT keys (their target cannot be proven), so a write through one conservatively
// marks the whole base as written. That keeps this pass strictly a no-FP
// extension: it only reports a read when NO write to that exact subobject (and
// no whole-base write) exists anywhere in the function.
struct SubKey {
  const VarDecl *base = nullptr;
  const FieldDecl *field = nullptr; // null for a whole base or array element
  int index = -1;                   // >= 0 for a[constant], -1 otherwise
  bool operator<(const SubKey &o) const {
    return std::tie(base, field, index) < std::tie(o.base, o.field, o.index);
  }
};
using SubState = std::set<SubKey>;

static SubKey wholeKey(const VarDecl *vd) { return SubKey{vd, nullptr, -1}; }

/// Resolve `e` to a SubKey when it is a direct, constant-index access to a
/// local aggregate. Returns false for anything not precisely identifiable.
static bool subKeyOf(ASTContext &ctx, const Expr *e, SubKey &key) {
  if (!e)
    return false;
  e = e->IgnoreParenImpCasts();
  if (const auto *me = dyn_cast<MemberExpr>(e)) {
    if (me->isArrow())
      return false; // pointee identity is not tracked here
    const auto *fd = dyn_cast<FieldDecl>(me->getMemberDecl());
    if (!fd)
      return false;
    const auto *dr =
        dyn_cast<DeclRefExpr>(me->getBase()->IgnoreParenImpCasts());
    const auto *vd = dr ? dyn_cast<VarDecl>(dr->getDecl()) : nullptr;
    if (vd && vd->getType()->isRecordType()) {
      key = SubKey{vd, fd, -1};
      return true;
    }
    return false;
  }
  if (const auto *ase = dyn_cast<ArraySubscriptExpr>(e)) {
    Expr::EvalResult r;
    if (!ase->getIdx()->EvaluateAsInt(r, ctx) || !r.Val.isInt() ||
        r.Val.getInt().getBitWidth() > 64)
      return false;
    const int64_t raw = r.Val.getInt().getSExtValue();
    if (raw < 0 || raw > INT32_MAX)
      return false;
    const int index = static_cast<int>(raw);
    const Expr *base = ase->getBase()->IgnoreParenImpCasts();
    if (const auto *dr = dyn_cast<DeclRefExpr>(base)) {
      const auto *vd = dyn_cast<VarDecl>(dr->getDecl());
      if (vd) {
        key = SubKey{vd, nullptr, index};
        return true;
      }
    }
    // s.arr[constant] is a field plus a constant index.
    if (const auto *me = dyn_cast<MemberExpr>(base)) {
      if (me->isArrow())
        return false;
      const auto *fd = dyn_cast<FieldDecl>(me->getMemberDecl());
      const auto *dr =
          dyn_cast<DeclRefExpr>(me->getBase()->IgnoreParenImpCasts());
      const auto *vd = dr ? dyn_cast<VarDecl>(dr->getDecl()) : nullptr;
      if (fd && vd && vd->getType()->isRecordType()) {
        key = SubKey{vd, fd, index};
        return true;
      }
    }
    return false;
  }
  return false;
}

class SubWriteCollector : public RecursiveASTVisitor<SubWriteCollector> {
public:
  SubWriteCollector(ASTContext &ctx, const std::set<const VarDecl *> &tracked,
                    SubState &written, std::set<const VarDecl *> &escaped)
      : ctx_(ctx), tracked_(tracked), written_(written), escaped_(escaped) {}

  bool VisitBinaryOperator(BinaryOperator *bo) {
    if (bo->isAssignmentOp())
      markWritten(bo->getLHS());
    return true;
  }
  bool VisitUnaryOperator(UnaryOperator *uo) {
    if (uo->isIncrementOp() || uo->isDecrementOp())
      markWritten(uo->getSubExpr());
    return true;
  }
  bool VisitCallExpr(CallExpr *call) {
    // A callee may write any aggregate passed by value (decayed array) or by
    // address. Mark the whole base so later reads are never falsely reported.
    struct Refs : RecursiveASTVisitor<Refs> {
      std::set<const VarDecl *> &found;
      explicit Refs(std::set<const VarDecl *> &f) : found(f) {}
      bool VisitDeclRefExpr(DeclRefExpr *dr) {
        if (const auto *vd = dyn_cast<VarDecl>(dr->getDecl()))
          found.insert(vd);
        return true;
      }
    } refs(foundInCalls_);
    for (Expr *arg : call->arguments())
      refs.TraverseStmt(arg);
    for (const VarDecl *vd : foundInCalls_)
      if (tracked_.count(vd))
        escaped_.insert(vd);
    foundInCalls_.clear();
    return true;
  }
  bool VisitVarDecl(VarDecl *vd) {
    if (tracked_.count(vd) && vd->hasInit())
      written_.insert(wholeKey(vd)); // {..} / = {...} initialises the aggregate
    return true;
  }

private:
  void markWritten(const Expr *e) {
    if (!e)
      return;
    const Expr *base = e->IgnoreParenImpCasts();
    SubKey k;
    if (subKeyOf(ctx_, base, k)) {
      written_.insert(k);
      return;
    }
    // Non-constant index / arrow / nested: conservatively mark the whole base.
    if (const auto *ase = dyn_cast<ArraySubscriptExpr>(base))
      base = ase->getBase()->IgnoreParenImpCasts();
    else if (const auto *me = dyn_cast<MemberExpr>(base))
      base = me->getBase()->IgnoreParenImpCasts();
    if (const auto *dr = dyn_cast<DeclRefExpr>(base))
      if (const auto *vd = dyn_cast<VarDecl>(dr->getDecl()))
        if (tracked_.count(vd))
          written_.insert(wholeKey(vd));
  }

  ASTContext &ctx_;
  const std::set<const VarDecl *> &tracked_;
  SubState &written_;
  std::set<const VarDecl *> &escaped_;
  std::set<const VarDecl *> foundInCalls_;
};

class SubReadWalker {
public:
  SubReadWalker(ASTContext &ctx, const std::set<const VarDecl *> &tracked,
                const SubState &written, const std::set<const VarDecl *> &escaped,
                const std::set<const VarDecl *> &skipBases,
                const CfgReportFn &report, std::set<const Stmt *> &reported)
      : ctx_(ctx), tracked_(tracked), written_(written), escaped_(escaped),
        skipBases_(skipBases), report_(report), reported_(reported) {}

  void walk(const Stmt *s) {
    if (!s)
      return;
    if (const auto *bo = dyn_cast<BinaryOperator>(s)) {
      if (bo->isAssignmentOp()) {
        if (bo->isCompoundAssignmentOp())
          walk(bo->getLHS());
        walk(bo->getRHS());
        return;
      }
      walk(bo->getLHS());
      walk(bo->getRHS());
      return;
    }
    if (const auto *uo = dyn_cast<UnaryOperator>(s)) {
      if (uo->getOpcode() == UO_AddrOf)
        return; // taking an address does not read the object
      walk(uo->getSubExpr());
      return;
    }
    if (const auto *ase = dyn_cast<ArraySubscriptExpr>(s)) {
      checkRead(s, ase);
      walk(ase->getBase());
      walk(ase->getIdx());
      return;
    }
    if (const auto *me = dyn_cast<MemberExpr>(s)) {
      checkRead(s, me);
      walk(me->getBase());
      return;
    }
    for (const Stmt *child : s->children())
      walk(child);
  }

private:
  void checkRead(const Stmt *s, const Expr *e) {
    SubKey k;
    if (!subKeyOf(ctx_, e, k) || !tracked_.count(k.base))
      return;
    if (escaped_.count(k.base) || skipBases_.count(k.base))
      return;
    if (written_.count(wholeKey(k.base)) || written_.count(k))
      return;
    if (!reported_.insert(s).second)
      return;
    std::string what;
    if (k.field && k.index >= 0)
      what = "element " + std::to_string(k.index) + " of field '" +
             k.field->getNameAsString() + "'";
    else if (k.field)
      what = "field '" + k.field->getNameAsString() + "'";
    else
      what = "element " + std::to_string(k.index);
    report_(s->getBeginLoc(), what + " of '" + k.base->getNameAsString() +
                                  "' is uninitialized (read before it is "
                                  "initialized)");
  }

  ASTContext &ctx_;
  const std::set<const VarDecl *> &tracked_;
  const SubState &written_;
  const std::set<const VarDecl *> &escaped_;
  const std::set<const VarDecl *> &skipBases_;
  CfgReportFn report_;
  std::set<const Stmt *> &reported_;
};

static void checkSubobjectDefiniteInitialization(
    ASTContext &ctx, Stmt *body, const std::set<const VarDecl *> &aggregates,
    const std::set<const VarDecl *> &skipBases, const CfgReportFn &report) {
  if (aggregates.empty())
    return;
  SubState written;
  std::set<const VarDecl *> escaped;
  SubWriteCollector writes(ctx, aggregates, written, escaped);
  writes.TraverseStmt(body);
  std::set<const Stmt *> reported;
  SubReadWalker reader(ctx, aggregates, written, escaped, skipBases, report,
                       reported);
  reader.walk(body);
}

/// A %p conversion of the literal "0" writes a provably NULL pointer. Report a
/// direct dereference of that variable in the SAME basic block, before any
/// reassignment. A guard lives in a different block, so `if (p) return *p;`
/// stays clean; this never speculates about non-literal inputs or other
/// conversions. STRICT null analysis should additionally seed the model from
/// nullPointerOutParams() for inter-block cases.
static void checkNullOutParamDerefs(const CFG &cfg, const CfgReportFn &report) {
  auto literalBytes = [](const Expr *e) -> llvm::StringRef {
    const auto *sl = dyn_cast_or_null<StringLiteral>(e->IgnoreParenImpCasts());
    return sl ? sl->getBytes() : llvm::StringRef();
  };
  for (const CFGBlock *b : cfg) {
    if (!b)
      continue;
    std::set<const VarDecl *> nullVars;
    std::set<const Stmt *> reported;
    for (const CFGElement &el : *b) {
      std::optional<CFGStmt> cs = el.getAs<CFGStmt>();
      if (!cs)
        continue;
      Stmt *s = const_cast<Stmt *>(cs->getStmt());

      if (const auto *call = dyn_cast<CallExpr>(s)) {
        const FunctionDecl *callee = call->getDirectCallee();
        if (callee && callee->getName() == "sscanf" && call->getNumArgs() >= 2 &&
            literalBytes(call->getArg(0)).trim() == "0" &&
            literalBytes(call->getArg(1)).contains("%p")) {
          for (unsigned i = 2; i < call->getNumArgs(); ++i) {
            const Expr *a = call->getArg(i)->IgnoreParenCasts();
            const auto *uo = dyn_cast<UnaryOperator>(a);
            if (!uo || uo->getOpcode() != UO_AddrOf)
              continue;
            const auto *ref = dyn_cast<DeclRefExpr>(
                uo->getSubExpr()->IgnoreParenImpCasts());
            const auto *vd = ref ? dyn_cast<VarDecl>(ref->getDecl()) : nullptr;
            if (vd && vd->getType()->isPointerType())
              nullVars.insert(vd);
          }
        }
      }
      if (const auto *bo = dyn_cast<BinaryOperator>(s)) {
        if (bo->isAssignmentOp()) {
          const auto *dr =
              dyn_cast<DeclRefExpr>(bo->getLHS()->IgnoreParenImpCasts());
          const auto *vd = dr ? dyn_cast<VarDecl>(dr->getDecl()) : nullptr;
          if (vd)
            nullVars.erase(vd);
        }
      }
      struct DerefVisitor : RecursiveASTVisitor<DerefVisitor> {
        const std::set<const VarDecl *> &nullVars;
        const CfgReportFn &report;
        std::set<const Stmt *> &reported;
        DerefVisitor(const std::set<const VarDecl *> &n, const CfgReportFn &r,
                     std::set<const Stmt *> &rep)
            : nullVars(n), report(r), reported(rep) {}
        bool VisitUnaryOperator(UnaryOperator *uo) {
          if (uo->getOpcode() == UO_Deref)
            check(uo, uo->getSubExpr());
          return true;
        }
        bool VisitMemberExpr(MemberExpr *me) {
          if (me->isArrow())
            check(me, me->getBase());
          return true;
        }
        void check(const Stmt *at, const Expr *base) {
          base = base->IgnoreParenImpCasts();
          const auto *dr = dyn_cast<DeclRefExpr>(base);
          const auto *vd = dr ? dyn_cast<VarDecl>(dr->getDecl()) : nullptr;
          if (vd && nullVars.count(vd) && reported.insert(at).second)
            report(at->getBeginLoc(),
                   "variable '" + vd->getNameAsString() +
                       "' is provably null after a '%p' conversion of the "
                       "literal \"0\" (null pointer dereference)");
        }
      } visitor(nullVars, report, reported);
      visitor.TraverseStmt(s);
    }
  }
}

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

/// A deliberately small integer constant folder used ONLY to decide whether
/// the alternative (initialising) side of an upstream "Sometimes" uninit report
/// is compile-time dead. Any expression it cannot fold yields "unknown", which
/// always means "do not escalate" -- so it can never create a false positive.
/// Loop-induction variables modified solely by a for-increment keep their entry
/// value while folding the condition: `for (i=0; i<0; ++i)` is provably dead.
class ConstIntFolder {
public:
  explicit ConstIntFolder(const FunctionDecl *fd) { collect(fd); }

  /// `loopIncVars` is non-null only when folding the condition OF a for-loop
    /// and names exactly the variables that loop increments: those still hold
    /// their entry value at the first condition check. In a nested branch or a
    /// different loop the value may have changed, so trusting it would be
    /// unsound and could create false positives.
  bool eval(const Expr *e, int64_t &out,
            const std::set<const VarDecl *> *loopIncVars = nullptr) const {
    if (!e)
      return false;
    e = e->IgnoreParenImpCasts();
    if (const auto *il = dyn_cast<IntegerLiteral>(e)) {
      llvm::APInt v = il->getValue();
      if (v.getBitWidth() > 64)
        return false;
      out = v.getSExtValue();
      return true;
    }
    if (const auto *cl = dyn_cast<CharacterLiteral>(e)) {
      out = static_cast<int64_t>(cl->getValue());
      return true;
    }
    if (const auto *bl = dyn_cast<CXXBoolLiteralExpr>(e)) {
      out = bl->getValue() ? 1 : 0;
      return true;
    }
    if (const auto *dr = dyn_cast<DeclRefExpr>(e)) {
      const auto *vd = dyn_cast<VarDecl>(dr->getDecl());
      if (!vd)
        return false;
      // incVars are only meaningful at the condition of the loop that owns
      // the increment, never in a nested branch or another loop.
      if (incVars.count(vd) &&
          !(loopIncVars && loopIncVars->count(vd)))
        return false;
      auto it = env_.find(vd);
      if (it == env_.end())
        return false;
      out = it->second;
      return true;
    }
    if (const auto *uo = dyn_cast<UnaryOperator>(e)) {
      int64_t v = 0;
      if (!eval(uo->getSubExpr(), v, loopIncVars))
        return false;
      switch (uo->getOpcode()) {
      case UO_Plus:
        out = v;
        return true;
      case UO_Minus:
        out = -v;
        return true;
      case UO_Not:
        out = ~v;
        return true;
      case UO_LNot:
        out = !v;
        return true;
      default:
        return false;
      }
    }
    if (const auto *bo = dyn_cast<BinaryOperator>(e)) {
      int64_t l = 0, r = 0;
      if (!eval(bo->getLHS(), l, loopIncVars) ||
          !eval(bo->getRHS(), r, loopIncVars))
        return false;
      switch (bo->getOpcode()) {
      case BO_Add:  out = l + r; return true;
      case BO_Sub:  out = l - r; return true;
      case BO_Mul:  out = l * r; return true;
      case BO_Div:  if (!r) return false; out = l / r; return true;
      case BO_Rem:  if (!r) return false; out = l % r; return true;
      case BO_LT:   out = l < r;  return true;
      case BO_GT:   out = l > r;  return true;
      case BO_LE:   out = l <= r; return true;
      case BO_GE:   out = l >= r; return true;
      case BO_EQ:   out = l == r; return true;
      case BO_NE:   out = l != r; return true;
      case BO_And:  out = l & r;  return true;
      case BO_Or:   out = l | r;  return true;
      case BO_Xor:  out = l ^ r;  return true;
      case BO_LAnd: out = l && r; return true;
      case BO_LOr:  out = l || r; return true;
      default:      return false;
      }
    }
    if (const auto *co = dyn_cast<ConditionalOperator>(e)) {
      int64_t c = 0, a = 0, b = 0;
      if (eval(co->getCond(), c, loopIncVars) &&
          eval(co->getTrueExpr(), a, loopIncVars) &&
          eval(co->getFalseExpr(), b, loopIncVars)) {
        out = c ? a : b;
        return true;
      }
    }
    return false;
  }

private:
  void collect(const FunctionDecl *fd) {
    if (!fd || !fd->getBody())
      return;
    struct Candidates : RecursiveASTVisitor<Candidates> {
      std::map<const VarDecl *, int64_t> &env;
      ASTContext &ctx;
      Candidates(std::map<const VarDecl *, int64_t> &e, ASTContext &c)
          : env(e), ctx(c) {}
      bool VisitVarDecl(VarDecl *vd) {
        if (const Expr *init = vd->getInit()) {
          Expr::EvalResult r;
          if (init->EvaluateAsInt(r, ctx) && r.Val.isInt() &&
              r.Val.getInt().getBitWidth() <= 64)
            env[vd] = r.Val.getInt().getSExtValue();
        }
        return true;
      }
    };
    Candidates candidates(env_, fd->getASTContext());
    candidates.TraverseStmt(fd->getBody());

    struct Mods : RecursiveASTVisitor<Mods> {
      std::set<const VarDecl *> &modified;
      explicit Mods(std::set<const VarDecl *> &m) : modified(m) {}
      void add(const Expr *e) {
        e = e->IgnoreParenImpCasts();
        if (const auto *dr = dyn_cast<DeclRefExpr>(e))
          if (const auto *vd = dyn_cast<VarDecl>(dr->getDecl()))
            modified.insert(vd);
      }
      bool VisitBinaryOperator(BinaryOperator *bo) {
        if (bo->isAssignmentOp())
          add(bo->getLHS());
        return true;
      }
      bool VisitUnaryOperator(UnaryOperator *uo) {
        if (uo->isIncrementOp() || uo->isDecrementOp() ||
            uo->getOpcode() == UO_AddrOf)
          add(uo->getSubExpr());
        return true;
      }
    };
    Mods mods(modified);
    mods.TraverseStmt(fd->getBody());

    // Values changed only by a for-increment are still fixed when the loop's
    // condition is evaluated on its first iteration, which is what decides
    // whether the loop body is reachable at all.
    struct Incs : RecursiveASTVisitor<Incs> {
      std::set<const VarDecl *> &incVars;
      explicit Incs(std::set<const VarDecl *> &i) : incVars(i) {}
      struct Refs : RecursiveASTVisitor<Refs> {
        std::set<const VarDecl *> &out;
        explicit Refs(std::set<const VarDecl *> &o) : out(o) {}
        bool VisitDeclRefExpr(DeclRefExpr *dr) {
          if (const auto *vd = dyn_cast<VarDecl>(dr->getDecl()))
            out.insert(vd);
          return true;
        }
      };
      bool VisitForStmt(ForStmt *fs) {
        if (Expr *inc = fs->getInc()) {
          Refs refs(incVars);
          refs.TraverseStmt(inc);
        }
        return true;
      }
    };
    Incs incs(incVars);
    incs.TraverseStmt(fd->getBody());

    for (const VarDecl *vd : modified)
      if (!incVars.count(vd))
        env_.erase(vd);
  }

  std::map<const VarDecl *, int64_t> env_;
  std::set<const VarDecl *> modified;
  std::set<const VarDecl *> incVars;
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
                          std::set<const VarDecl *> &reported,
                          const ConstIntFolder *folder)
        : tracked_(tracked), report_(report), reported_(reported),
          folder_(folder) {}

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
        // Escalate a "Sometimes" to definite only when EVERY listed branch is
        // forced by a compile-time-constant condition: the alternative path
        // (the one that initialises the variable) is then provably dead, e.g.
        // `int x; for (int i=0; i<0; ++i) x=1; return x;`. Unknown conditions
        // keep the old discard policy, so guarded initialisation stays clean.
        if (!folder_ || !allBranchesForced(use))
          return;
        break;
      case clang::UninitUse::Maybe:
        return; // no branch evidence at all: stay conservative
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
    static const Expr *conditionOf(const Stmt *term) {
      if (!term)
        return nullptr;
      if (const auto *s = dyn_cast<IfStmt>(term))
        return s->getCond();
      if (const auto *s = dyn_cast<ForStmt>(term))
        return s->getCond();
      if (const auto *s = dyn_cast<WhileStmt>(term))
        return s->getCond();
      // do-while's body executes before the condition, so a folded condition
      // does not by itself prove an uninitialized path: never escalate on it.
      if (const auto *s = dyn_cast<ConditionalOperator>(term))
        return s->getCond();
      return nullptr;
    }

    /// Clang's Branch::Output is the CFG successor index that leads to the
    /// reported uninit use: 0 = condition true (then/loop body), 1 = false
    /// (else/loop exit). If the condition folds to the same output, every live
    /// path to the use is an uninit path and the report is definite.
    bool allBranchesForced(const clang::UninitUse &use) const {
      if (use.branch_empty())
        return false;
      for (auto it = use.branch_begin(); it != use.branch_end(); ++it) {
        const Expr *cond = conditionOf(it->Terminator);
        int64_t value = 0;
        // Only this loop's own increment variables may keep their entry value;
        // a nested branch or another loop's counter may have changed by now.
        std::set<const VarDecl *> loopIncVars;
        if (const auto *fs =
                dyn_cast_or_null<ForStmt>(it->Terminator)) {
          struct Refs : RecursiveASTVisitor<Refs> {
            std::set<const VarDecl *> &out;
            explicit Refs(std::set<const VarDecl *> &o) : out(o) {}
            bool VisitDeclRefExpr(DeclRefExpr *dr) {
              if (const auto *vd = dyn_cast<VarDecl>(dr->getDecl()))
                out.insert(vd);
              return true;
            }
          } refs(loopIncVars);
          if (const Expr *inc = fs->getInc())
            refs.TraverseStmt(const_cast<Expr *>(inc));
        }
        if (!cond || !folder_->eval(cond, value, &loopIncVars))
          return false;
        const unsigned forced = value != 0 ? 0u : 1u;
        if (it->Output != forced)
          return false;
      }
      return true;
    }

    const std::set<const VarDecl *> &tracked_;
    CfgReportFn report_;
    std::set<const VarDecl *> &reported_;
    const ConstIntFolder *folder_;
  };

  clang::AnalysisDeclContext ac(nullptr, fd);
  ConstIntFolder folder(fd);
  UpstreamUninitHandler handler(tracked, report, upstreamReported, &folder);
  clang::UninitVariablesAnalysisStats stats{};
  clang::runUninitializedVariablesAnalysis(*fd, *cfg, ac, handler, stats);

  // Arrays and records are outside upstream's uninit analysis, so run the
  // subobject pass for them. Variables upstream already reported are skipped as
  // bases to avoid duplicate diagnostics for the same object.
  std::set<const VarDecl *> aggregates;
  for (const VarDecl *vd : collector.vars())
    if (vd->getType()->isArrayType() || vd->getType()->isRecordType())
      aggregates.insert(vd);
  checkSubobjectDefiniteInitialization(ctx, fd->getBody(), aggregates,
                                       upstreamReported, report);
  checkNullOutParamDerefs(*cfg, report);

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
    // Aggregates are handled by the subobject pass, which knows that writing
    // one element/field does not initialise the whole object. The scalar policy
    // worklist reasons only about whole VarDecls and would mis-handle them.
    if (tracked.count(vd) && !aggregates.count(vd) && !entryInit.count(vd) &&
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
  if (st.attempted) {
    // Contracts are parsed exactly once. On every later query confirm the
    // canonical file is still the one we analysed; a swap fails closed instead
    // of silently mixing two contract sets within one process.
    if (!st.swapped && !contractUnchanged(st)) {
      st.swapped = true;
      st.loaded = false;
      st.issues.push_back(
          {"", "libc contract file changed after analysis "
               "(CPLUS_CONTRACT_PATH); refusing to mix contract sets"});
    }
    return st;
  }
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
  // Pin the raw path to a canonical absolute regular file BEFORE the single
  // read. An explicit CPLUS_CONTRACT_PATH that cannot be canonicalised (bad
  // relative path, symlink loop, directory, missing file) fails closed and is
  // never masked by the default. Only the implicit path may use the installed
  // layout fallback, which is canonicalised the same way.
  std::string canonical;
  bool resolved = canonicalContractPath(path, canonical);
  if (!resolved && !explicitEnvPath) {
    std::string exe = llvm::sys::fs::getMainExecutable("c+", nullptr);
    if (!exe.empty()) {
      llvm::SmallString<256> dir(llvm::sys::path::parent_path(exe));
      llvm::sys::path::append(dir, "..", "share", "cplus",
                              "libc_contracts.json");
      if (canonicalContractPath(std::string(dir.str()), canonical))
        resolved = true;
    }
  }
  if (!resolved) {
    st.issues.push_back({"", "libc contract path is not a canonical regular "
                              "file: " + path});
    return st;
  }
  auto buf = llvm::MemoryBuffer::getFile(canonical);
  if (!buf) {
    st.issues.push_back({"", "libc contract file not found: " + canonical});
    return st;
  }
  st.resolvedPath = canonical;
  st.envPath = explicitEnvPath; // evidence vs claim: see hasVerifiedEffectDeclaration
  st.parsedBytes.assign((*buf)->getBuffer().begin(), (*buf)->getBuffer().end());
  auto parsed = llvm::json::parse((*buf)->getBuffer());
  if (!parsed) {
    llvm::consumeError(parsed.takeError());
    st.issues.push_back({"", "libc contract file is not valid JSON: " + canonical});
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

    // Every array-typed field must actually be an array. `optional` is a
    // boolean flag in the shipped v2 contract file, not an array: listing it
    // here made every contract set invalid and masked real diagnostics.
    for (const auto &arr : {"params", "nullableArgs", "effects", "borrow",
                            "dynamic", "paramTypes", "readEffects",
                            "writeEffects", "freeEffects",
                            "threadAssumptions"})
      if (const auto *v = spec->get(arr))
        if (!v->getAsArray())
          st.issues.push_back({fn, std::string("contract field '") + arr +
                                       "' must be an array"});

    // Every remaining list field is a list of strings (nullableArgs is the one
    // integer list and is validated below). A non-string entry would be
    // silently skipped by the effect consumers -- fail closed instead.
    for (const auto &list : {"params", "effects", "borrow", "dynamic",
                             "paramTypes", "readEffects", "writeEffects",
                             "freeEffects", "threadAssumptions"})
      if (const auto *arr = spec->getArray(list))
        for (const auto &item : *arr)
          if (!item.getAsString())
            st.issues.push_back({fn, std::string("contract field '") + list +
                                        "' must contain strings"});

    // Textual fields must be strings when present. JSON null is the shipped
    // spelling of "not applicable" and is accepted; any other wrong type means
    // the contract cannot be fully interpreted, so the set fails closed.
    for (const auto &text : {"ownership", "transfer", "extent", "failure",
                             "borrowLifetime", "returnState",
                             "failureBehavior", "allocationSize",
                             "pointerBounds", "ownershipTransfer"})
      if (const auto *v = spec->get(text))
        if (!v->getAsNull() && !v->getAsString())
          st.issues.push_back({fn, std::string("contract field '") + text +
                                       "' must be a string"});

    FunctionEffect e;
    // diagnostics category: honour the contract's own category.
    if (auto diag = spec->getString("diagnostics")) {
      if (diag->starts_with("CPLUS-SAFE-"))
        e.category = diag->str();
      else
        st.issues.push_back({fn, "invalid diagnostics category"});
    }
    // nullableArgs: array of argument indices. Keep the legacy boolean and
    // additionally record the exact indices for precise consumers. An index
    // outside `params` cannot name an argument, so the summary is incomplete
    // and must fail closed instead of silently granting nullability to a
    // different parameter.
    const auto *nullableParams = spec->getArray("params");
    if (const auto *nullable = spec->getArray("nullableArgs")) {
      for (const auto &v : *nullable) {
        if (auto index = v.getAsInteger()) {
          if (*index < 0 || *index > INT32_MAX ||
              (nullableParams &&
               static_cast<size_t>(*index) >= nullableParams->size())) {
            st.issues.push_back({fn, "nullableArgs index out of range"});
            continue;
          }
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

/// Built-in effect summaries for libc / SafeCRT calls. Kept separate from the
/// JSON contract layer so a known-correct argument index can be applied even
/// when a contract entry exists (the compiled-in contracts use prose extents
/// such as "n <= dst_capacity" that resolve to no argument index at all).
static FunctionEffect builtinLibcEffect(llvm::StringRef name) {
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
  } else if (name == "memcpy" || name == "memmove" || name == "memset" ||
             name == "memcpy_s" || name == "memmove_s" || name == "memset_s") {
    e.borrows = true;
    e.writes = true;
    e.destArg = 0;
    // memcpy/memmove/memset: (dst, src, n) => n is argument 2.
    // *_s variants: (dst, dmax, ...) => destination capacity is argument 1.
    e.sizeArg = (name == "memcpy_s" || name == "memmove_s" ||
                 name == "memset_s") ? 1 : 2;
    e.checkedSize = true;
    e.category = "CPLUS-SAFE-BOUNDS";
  } else if (name == "strcpy" || name == "strcpy_s" ||
             name == "strncpy" || name == "strncpy_s" ||
             name == "strcat" || name == "strcat_s" ||
             name == "strncat" || name == "strncat_s") {
    e.borrows = true;
    e.writes = true;
    e.destArg = 0;
    // strncpy/strncat: (dst, src, n) => n is argument 2, NOT the src pointer
    // at argument 1. The SafeCRT *_s forms put dmax at argument 1.
    if (name == "strcpy_s" || name == "strncpy_s" || name == "strcat_s" ||
        name == "strncat_s")
      e.sizeArg = 1;
    else if (name == "strncpy" || name == "strncat")
      e.sizeArg = 2;
    e.checkedSize = e.sizeArg >= 0;
    e.category = "CPLUS-SAFE-CRT";
  } else if (name == "strlen" || name == "strnlen_s" || name == "strcmp") {
    e.borrows = true;
    e.category = "CPLUS-SAFE-CRT";
  } else if (name == "fopen") {
    e.allocates = true;
    e.returnsOwned = true; // fopen returns the owned FILE*
    e.mayFail = true;
    e.category = "CPLUS-SAFE-CRT";
  } else if (name == "fopen_s") {
    e.allocates = true;
    // fopen_s returns errno_t and writes the owned FILE* through argument 0.
    e.returnsOwned = false;
    e.writes = true;
    e.destArg = 0;
    e.mayFail = true;
    e.category = "CPLUS-SAFE-CRT";
  } else if (name == "fclose" || name == "fclose_s") {
    e.frees = true;
    e.freedArg = 0;
    e.category = "CPLUS-SAFE-CRT";
  } else if (name == "fread" || name == "fread_s" || name == "fwrite" ||
             name == "fwrite_s") {
    e.borrows = true;
    e.writes = true;
    // fread_s/fwrite_s: (buffer, buffersize, elementsize, count, stream).
    e.destArg = 0;
    e.sizeArg = (name == "fread_s" || name == "fwrite_s") ? 1 : -1;
    e.checkedSize = e.sizeArg >= 0;
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

FunctionEffect libcEffect(llvm::StringRef name) {
  FunctionEffect builtin = builtinLibcEffect(name);
  auto &contracts = loadedContracts().contracts;
  auto it = contracts.find(name.str());
  if (it == contracts.end())
    return builtin;
  FunctionEffect e = it->second;
  // Prefer the exact builtin argument indices over a contract's prose extent:
  // without this, `strncpy`'s size would be read from the source pointer at
  // argument 1 instead of the byte count at argument 2.
  if (builtin.sizeArg >= 0) {
    e.sizeArg = builtin.sizeArg;
    e.checkedSize = true;
  }
  if (builtin.destArg >= 0)
    e.destArg = builtin.destArg;
  if (name == "fopen_s") {
    // errno_t fopen_s(FILE **streamptr, ...): ownership is delivered through
    // argument 0, not through the scalar return value.
    e.returnsOwned = false;
    e.writes = true;
    e.destArg = 0;
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
  if (isCompilerHelperEffectName(name))
    return true;
  return loadedContracts().contracts.count(name.str()) != 0;
}

bool hasVerifiedEffectDeclaration(llvm::StringRef name) {
  if (isBuiltinLibcEffectName(name) || isCompilerHelperEffectName(name))
    return true;
  // A contract supplied through CPLUS_CONTRACT_PATH is a claim made by the
  // caller. STRICT must not treat it as evidence that a defining body was
  // analysed: a bodiless pointer extern backed only by such a summary stays
  // unverified. The compiled-in default set ships with the compiler and remains
  // authoritative.
  ContractState &st = contractState();
  if (!st.attempted)
    loadedContracts(); // force the single validated load before checking
  if (st.envPath)
    return false;
  return st.contracts.count(name.str()) != 0;
}

std::set<std::string> nullPointerOutParams(FunctionDecl *fd) {
  std::set<std::string> names;
  if (!fd || !fd->doesThisDeclarationHaveABody() || !fd->getBody())
    return names;
  LocalCollector collector(fd->getASTContext());
  collector.TraverseStmt(fd->getBody());
  for (const VarDecl *vd : collector.nullWrittenVars())
    names.insert(vd->getNameAsString());
  return names;
}

SafetyAstProbe probeSafetyAst(const std::string &source,
                              const std::vector<std::string> &args,
                              const std::string &fileName) {
  SafetyAstProbe result;

  // Keep only the flags that affect parsing, matching checkAstSafety(). A
  // synthetic .c name forces C (not C++) language inference by the tooling
  // layer, and the resource dir mirrors the main safety AST build.
  // NOTE: pass-through flags that change the parse but are discarded here
  // (-include/-imacros/-isystem/-iquote/-idirafter/-x/-include-pch, @response
  // files, -Xclang injection, double-dash spellings) are hard-rejected by the
  // driver's STRICT allow-list, so this probe can never analyse a different
  // program than the backend compiles under the safety gate.
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
      // No body and no *authoritative* effect summary => the pointer effects
      // of this extern are invisible to this TU. A contract is a claim, not
      // evidence: an env-supplied summary does not certify a defining body was
      // analysed, so it cannot silence the STRICT gate on its own.
      if (hasPointerEffectSignature(fd) &&
          !hasVerifiedEffectDeclaration(fd->getName())) {
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

/// True for symbols owned by C+ instrumentation / the runtime ABI. A user
/// declaration or definition of one of these can interpose the generated guard
/// at link time (e.g. defining __cplus_checked_add_i64 silently wraps overflow).
/// This is deliberately an explicit list plus a few closed name families:
/// rejecting every `cplus_*` name would outlaw legitimate project helpers such
/// as `cplus_core_helper` (a multi-TU regression fixture).
bool isReservedHelperName(llvm::StringRef name) {
  // Emitted guard/arithmetic helpers and generated defer/drop helpers.
  if (name.starts_with("__cplus_checked_") ||
      name.starts_with("__cplus_inline_") ||
      name.starts_with("__cplus_defer_") || name.starts_with("cplus_drop_"))
    return true;
  // Closed runtime families (allocation, checks, printing, threading, scope).
  if (name.starts_with("cplus_alloc") || name.starts_with("cplus_check_") ||
      name.starts_with("cplus_print_") || name.starts_with("cplus_mutex_") ||
      name.starts_with("cplus_tls_") || name.starts_with("cplus_scope_") ||
      name.starts_with("cplus_checked"))
    return true;
  // Remaining individual runtime ABI entry points (runtime/include/cplus/cplus.h
  // and the rewriter's generated wrappers).
  static const std::set<llvm::StringRef> kReserved = {
      "__cplus_bounds_check",
      "cplus_address_retired", "cplus_allocation_generation",
      "cplus_arith_fatal", "cplus_cleanup", "cplus_cleanup_kind",
      "cplus_fatal", "cplus_fatal_hook", "cplus_free_registered",
      "cplus_log", "cplus_log_impl", "cplus_memcpy", "cplus_memmove",
      "cplus_move", "cplus_new", "cplus_new_array",
      "cplus_printf_s", "cplus_fprintf_s", "cplus_snprintf_s",
      "cplus_sprintf_s", "cplus_realloc", "cplus_realloc_registered",
      "cplus_register_allocation", "cplus_release",
      "cplus_resource", "cplus_resource_check_borrow",
      "cplus_retired_ledger_full", "cplus_rotl32", "cplus_rotr32",
      "cplus_runtime", "cplus_runtime_log_level",
      "cplus_runtime_logging_init", "cplus_scan_one", "cplus_scan_value",
      "cplus_strcpy", "cplus_strnlen", "cplus_zero", "cplus_zalloc",
  };
  return kReserved.count(name) != 0;
}

std::set<std::string>
findUserDeclaredHelperSymbols(const std::string &source,
                              const std::vector<std::string> &args,
                              const std::string &fileName) {
  std::set<std::string> found;
  // Parse the ORIGINAL source, not the lowered text. The rewriter prepends a
  // generated prelude full of `extern int64_t __cplus_checked_*` declarations
  // and `static inline __cplus_inline_*` definitions; those are legitimate and
  // would otherwise be indistinguishable by source location in the lowered
  // buffer. C+ sources may not parse cleanly as C; that is fine -- we only need
  // the declarations/definitions Clang did recover, and a failed parse yields
  // an empty set (no false positive) rather than an unsafe acceptance of a
  // user definition in an otherwise plain-C source.
  clang::IgnoringDiagConsumer ignore;
  std::unique_ptr<clang::ASTUnit> ast =
      clang::tooling::buildASTFromCodeWithArgs(
          source, safetyParseArgs(args), fileName + ".chelpers.c",
          "cplus-helper-probe", std::make_shared<clang::PCHContainerOperations>(),
          clang::tooling::getClangStripDependencyFileAdjuster(), {}, &ignore);
  if (!ast)
    return found;
  clang::ASTContext &ctx = ast->getASTContext();
  clang::SourceManager &sm = ctx.getSourceManager();
  struct HelperCollector : clang::RecursiveASTVisitor<HelperCollector> {
    clang::SourceManager &sm;
    std::set<std::string> &out;
    HelperCollector(clang::SourceManager &s, std::set<std::string> &o)
        : sm(s), out(o) {}
    bool VisitFunctionDecl(clang::FunctionDecl *fd) {
      if (fd->isImplicit() || !isReservedHelperName(fd->getName()))
        return true;
      clang::SourceLocation loc = fd->getLocation();
      if (loc.isValid() && sm.isInMainFile(loc))
        out.insert(fd->getNameAsString());
      return true;
    }
    bool VisitVarDecl(clang::VarDecl *vd) {
      if (vd->isImplicit() || !isReservedHelperName(vd->getName()))
        return true;
      clang::SourceLocation loc = vd->getLocation();
      if (loc.isValid() && sm.isInMainFile(loc))
        out.insert(vd->getNameAsString());
      return true;
    }
  } collector(sm, found);
  collector.TraverseDecl(ctx.getTranslationUnitDecl());
  return found;
}
} // namespace cplus
