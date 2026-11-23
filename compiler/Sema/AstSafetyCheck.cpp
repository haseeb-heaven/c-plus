#include "Sema/AstSafetyCheck.h"

#include "Diagnostics.h"
#include "Analysis/OwnershipAnalysis.h"

#include <llvm/ADT/StringRef.h>

#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/Expr.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/ParentMapContext.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Frontend/PCHContainerOperations.h>

#include <clang/Frontend/ASTUnit.h>
#include <clang/Tooling/Tooling.h>

#include <map>
#include <set>
#include <string>

using namespace clang;

namespace cplus {
namespace {

/// Clang FORTIFY lowers some libc calls to *_chk builtins with an extra
/// object-size argument. Normalise them back to the source function name.
inline std::string normalizeFortifyName(std::string n) {
  static const std::pair<const char *, const char *> kNames[] = {
      {"__builtin___memcpy_chk", "memcpy"},
      {"__builtin___memmove_chk", "memmove"},
      {"__builtin___strcpy_chk", "strcpy"},
      {"__builtin___memset_chk", "memset"},
      {"__builtin___strcat_chk", "strcat"},
      {"__builtin___strncpy_chk", "strncpy"},
      {"__builtin___snprintf_chk", "snprintf"},
      {"__builtin___sprintf_chk", "sprintf"},
      {"__builtin___vsprintf_chk", "vsprintf"},
      {"__builtin___vsnprintf_chk", "vsnprintf"},
      {"__builtin___fprintf_chk", "fprintf"},
      {"__builtin___printf_chk", "printf"},
  };
  for (const auto &kv : kNames)
    if (n.rfind(kv.first, 0) == 0)
      return kv.second;
  return n;
}

/// True when a scanf-style format contains an unbounded `%s` conversion
/// (no field width), which can overflow any fixed-size destination array.
inline bool hasUnboundedStringConversion(const StringLiteral *fmt) {
  llvm::StringRef s = fmt->getBytes();
  for (size_t i = 0; i + 1 < s.size(); ++i) {
    if (s[i] != '%')
      continue;
    size_t j = i + 1;
    bool width = false;
    while (j < s.size() &&
           (s[j] == '-' || s[j] == '+' || s[j] == '#' || s[j] == '0' ||
            (s[j] >= '1' && s[j] <= '9')))
      width = true, ++j;
    if (j < s.size() && s[j] == 's' && !width)
      return true;
  }
  return false;
}

/// True for stdio format functions whose first/second argument must be a
/// literal format string. Shared by direct-call and function-pointer checks.
inline bool isFormatFunctionName(const std::string &n) {
  static const std::set<std::string> kNames = {
      "printf", "fprintf", "sprintf", "snprintf", "vprintf", "vfprintf",
      "vsprintf", "vsnprintf", "printf_s", "fprintf_s", "sprintf_s",
      "snprintf_s", "cplus_printf_s", "cplus_fprintf_s",
      "cplus_snprintf_s", "cplus_sprintf_s", "scanf", "fscanf", "sscanf",
      "scanf_s", "fscanf_s", "sscanf_s"};
  return kNames.count(n) != 0;
}

/// Find a free/release call on a named variable inside a function body.
struct FreeCallFinder : RecursiveASTVisitor<FreeCallFinder> {
  const std::string &target;
  bool found = false;
  explicit FreeCallFinder(const std::string &t) : target(t) {}

  static std::string nameOf(const Expr *e) {
    if (!e)
      return std::string();
    const Expr *x = e->IgnoreParenImpCasts();
    if (const auto *uo = dyn_cast<UnaryOperator>(x))
      if (uo->getOpcode() == UO_AddrOf)
        x = uo->getSubExpr()->IgnoreParenImpCasts();
    if (const auto *ref = dyn_cast<DeclRefExpr>(x))
      if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl()))
        return vd->getNameAsString();
    return std::string();
  }

  bool VisitCallExpr(CallExpr *c) {
    if (!c->getDirectCallee())
      return true;
    std::string n = c->getDirectCallee()->getNameAsString();
    if ((n == "free" || n == "cplus_release") && c->getNumArgs() >= 1 &&
        nameOf(c->getArg(0)) == target)
      found = true;
    return true;
  }
};

/// Find writes to named global variables inside a thread entry function.
struct GlobalWriteFinder : RecursiveASTVisitor<GlobalWriteFinder> {
  const std::set<std::string> &globals;
  std::set<std::string> written;
  std::set<std::string> freed;
  explicit GlobalWriteFinder(const std::set<std::string> &g) : globals(g) {}

  bool synchronized = false;

  bool VisitCallExpr(CallExpr *c) {
    if (!c->getDirectCallee())
      return true;
    std::string n = c->getDirectCallee()->getNameAsString();
    // A lock/unlock pair in the thread entry means the global writes are
    // protected; suppress the may-race report for that function.
    if (n.find("mutex_lock") != std::string::npos ||
        n.find("mutex_unlock") != std::string::npos ||
        n.find("rwlock_") != std::string::npos)
      synchronized = true;
    if (c->getNumArgs() < 1)
      return true;
    if (n != "free" && n != "cplus_release")
      return true;
    std::string arg = nameOf(c->getArg(0)->IgnoreParenCasts());
    if (globals.count(arg))
      freed.insert(arg);
    return true;
  }

  static std::string nameOf(const Expr *e) {
    if (!e)
      return std::string();
    if (const auto *ref = dyn_cast<DeclRefExpr>(e->IgnoreParenImpCasts()))
      if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl()))
        return vd->getNameAsString();
    return std::string();
  }

  bool VisitBinaryOperator(BinaryOperator *op) {
    if (op->isAssignmentOp()) {
      std::string n = nameOf(op->getLHS());
      if (globals.count(n))
        written.insert(n);
    }
    return true;
  }

  bool VisitUnaryOperator(UnaryOperator *op) {
    if (op->isIncrementOp() || op->isDecrementOp()) {
      std::string n = nameOf(op->getSubExpr());
      if (globals.count(n))
        written.insert(n);
    }
    return true;
  }
};

/// Collect variables released anywhere inside a statement (used to build
/// may-release state across branches and loops).
struct ReleaseCollector : RecursiveASTVisitor<ReleaseCollector> {
  std::set<std::string> released;

  bool VisitCallExpr(CallExpr *c) {
    if (!c->getDirectCallee() || c->getNumArgs() < 1)
      return true;
    std::string n = c->getDirectCallee()->getNameAsString();
    if (n != "free" && n != "cplus_release")
      return true;
    const Expr *arg = c->getArg(0)->IgnoreParenImpCasts();
    if (const auto *uo = dyn_cast<UnaryOperator>(arg))
      if (uo->getOpcode() == UO_AddrOf)
        arg = uo->getSubExpr()->IgnoreParenImpCasts();
    if (const auto *ref = dyn_cast<DeclRefExpr>(arg))
      if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl()))
        released.insert(vd->getNameAsString());
    return true;
  }
};

class SafetyVisitor : public RecursiveASTVisitor<SafetyVisitor> {
public:
  SafetyVisitor(ASTContext &ctx, DiagnosticEngine &diags, std::string filename,
                bool report)
      : ctx_(ctx), diags_(diags), filename_(std::move(filename)),
        report_(report) {}

  void setHandlers(const std::set<std::string> &handlers) {
    handlers_ = handlers;
  }
  void setGlobals(const std::set<std::string> &globals) {
    nonVolatileGlobals_ = globals;
  }
  const std::set<std::string> &handlers() const { return handlers_; }
  const std::set<std::string> &destructors() const { return destructors_; }
  void setDestructors(const std::set<std::string> &d) { destructors_ = d; }
  void setStrict(bool strict) { strict_ = strict; }
  const std::map<std::string, std::set<std::string>> &fieldAliases() const {
    return fieldAliases_;
  }
  void setFieldAliases(const std::map<std::string, std::set<std::string>> &m) {
    fieldAliases_ = m;
  }
  const std::map<std::string, std::string> &formatFnAliases() const {
    return formatFnAliases_;
  }
  void setFormatFnAliases(const std::map<std::string, std::string> &m) {
    formatFnAliases_ = m;
  }
  void setHandledReallocs(const std::set<unsigned> &s) { handledReallocs_ = s; }
  const std::set<unsigned> &handledReallocs() const { return handledReallocs_; }
  void setConstLocals(const std::map<std::string, llvm::APSInt> &m) { constLocals_ = m; }
  const std::map<std::string, llvm::APSInt> &constLocals() const { return constLocals_; }
  void setAssignedVars(const std::set<std::string> &s) { assignedVars_ = s; }
  const std::set<std::string> &assignedVars() const { return assignedVars_; }
  const std::set<std::string> &functionsReturningFreed() const {
    return functionsReturningFreed_;
  }
  void setFunctionsReturningFreed(const std::set<std::string> &s) {
    functionsReturningFreed_ = s;
  }
  const std::set<std::string> &nullReturningFunctions() const {
    return nullReturningFunctions_;
  }
  void setNullReturningFunctions(const std::set<std::string> &s) {
    nullReturningFunctions_ = s;
  }
  const std::set<std::string> &maybeReleasedVars() const {
    return maybeReleasedVars_;
  }
  void setMaybeReleasedVars(const std::set<std::string> &s) {
    maybeReleasedVars_ = s;
  }
  const std::map<std::string, unsigned> &maybeReleaseLines() const {
    return maybeReleaseLines_;
  }
  void setMaybeReleaseLines(const std::map<std::string, unsigned> &m) {
    maybeReleaseLines_ = m;
  }
  const std::map<std::string, std::map<unsigned, long long>> &
  callParamExtents() const {
    return callParamExtents_;
  }
  void setCallParamExtents(
      const std::map<std::string, std::map<unsigned, long long>> &m) {
    callParamExtents_ = m;
  }
  const std::set<std::string> &allGlobals() const { return allGlobals_; }
  const std::map<std::string, int> &threadEntryCounts() const {
    return threadEntryCounts_;
  }
  const std::map<std::string, SourceLocation> &threadEntryLocs() const {
    return threadEntryLocs_;
  }
  const std::set<std::string> &nonVolatileGlobals() const {
    return nonVolatileGlobals_;
  }

  bool isNullConstant(const Expr *expr) const {
    if (!expr)
      return false;
    expr = expr->IgnoreParenCasts();
    Expr::EvalResult result;
    if (expr->EvaluateAsInt(result, ctx_) && result.Val.isInt())
      return result.Val.getInt().getZExtValue() == 0;
    return false;
  }

  const VarDecl *nullVarOf(const Expr *expr) const {
    if (!expr)
      return nullptr;
    expr = expr->IgnoreParenImpCasts();
    if (const auto *ref = dyn_cast<DeclRefExpr>(expr))
      if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl()))
        if (nullVars_.count(vd->getNameAsString()))
          return vd;
    return nullptr;
  }

  std::string varNameOf(const Expr *expr) const {
    if (!expr)
      return std::string();
    const Expr *e = expr->IgnoreParenImpCasts();
    const auto *ref = dyn_cast<DeclRefExpr>(e);
    const auto *vd = ref ? dyn_cast<VarDecl>(ref->getDecl()) : nullptr;
    return vd ? vd->getNameAsString() : std::string();
  }

  // ---- Heap-extent tracking -------------------------------------------
  // Only allocations whose byte count is a compile-time constant are tracked.
  // Unknown or computed sizes are deliberately left untracked so that valid C
  // is never rejected on a guess.

  struct HeapAllocState {
    long long bytes = -1; // total byte extent; -1 means unknown
    bool zeroInit = false; // calloc-style zero initialisation
    const VarDecl *decl = nullptr;
  };

  /// Key used by constLocals_ for a scalar local or a struct field constant.
  std::string constKey(const Expr *e) const {
    if (!e)
      return std::string();
    const Expr *x = e->IgnoreParenCasts();
    if (const auto *me = dyn_cast<MemberExpr>(x)) {
      const Expr *base = me->getBase()->IgnoreParenCasts();
      if (const auto *ref = dyn_cast<DeclRefExpr>(base))
        if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl()))
          return vd->getNameAsString() + "." +
                 me->getMemberDecl()->getNameAsString();
      return std::string();
    }
    if (const auto *ref = dyn_cast<DeclRefExpr>(x))
      if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl()))
        return vd->getNameAsString();
    return std::string();
  }

  /// Look up a compile-time integer value, including struct-field constants.
  bool knownConst(const Expr *e, llvm::APSInt &out) const {
    if (!e)
      return false;
    Expr::EvalResult r;
    if (e->IgnoreParenCasts()->EvaluateAsInt(r, ctx_) && r.Val.isInt()) {
      out = r.Val.getInt();
      return true;
    }
    std::string key = constKey(e);
    if (key.empty())
      return false;
    auto it = constLocals_.find(key);
    if (it == constLocals_.end())
      return false;
    // A reassigned local is no longer constant.
    std::string base = key.substr(0, key.find('.'));
    if (assignedVars_.count(base))
      return false;
    out = it->second;
    return true;
  }

  /// Constant-fold integer arithmetic using tracked local/field constants.
  /// Clang will not fold expressions over a non-const local struct, so we do
  /// it ourselves at the source integer width.
  bool evalConstArithmetic(const Expr *e, llvm::APSInt &out) const {
    llvm::APSInt leaf;
    if (knownConst(e, leaf)) {
      // A wrapped unsigned value may be folded by Clang into a signed
      // negative APSInt; work with its unsigned bit pattern instead.
      if (leaf.isNegative())
        leaf = llvm::APSInt(
            llvm::APInt(leaf.getBitWidth(), leaf.getZExtValue()), true);
      out = leaf;
      return true;
    }
    const auto *bo = dyn_cast<BinaryOperator>(e->IgnoreParenCasts());
    if (!bo)
      return false;
    llvm::APSInt a, b;
    if (!evalConstArithmetic(bo->getLHS(), a) ||
        !evalConstArithmetic(bo->getRHS(), b))
      return false;
    unsigned w = ctx_.getIntWidth(bo->getType());
    if (w == 0 || w > 64)
      return false;
    unsigned long long av = a.getZExtValue(), bv = b.getZExtValue(), r = 0;
    switch (bo->getOpcode()) {
    case BO_Mul: r = av * bv; break;
    case BO_Add: r = av + bv; break;
    case BO_Sub: r = av - bv; break;
    default: return false;
    }
    if (w < 64)
      r &= ((1ULL << w) - 1);
    out = llvm::APSInt(llvm::APInt(w, r), true);
    return true;
  }

  /// Evaluate an allocation-size argument to a concrete byte count.  Only
  /// literal integer expressions and never-reassigned local constants are
  /// accepted; everything else is left unknown.
  bool evalByteCount(const Expr *arg, long long &out) const {
    if (!arg)
      return false;
    Expr::EvalResult r;
    if (arg->EvaluateAsInt(r, ctx_) && r.Val.isInt()) {
      llvm::APSInt v = r.Val.getInt();
      if (v.isNegative())
        return false;
      unsigned long long z = v.getZExtValue();
      if (z > (1ULL << 40))
        return false; // absurd; handled as a definitely-failing allocation
      out = (long long)z;
      return true;
    }
    llvm::APSInt v;
    if (!evalConstArithmetic(arg, v) || v.isNegative())
      return false;
    unsigned long long z = v.getZExtValue();
    if (z > (1ULL << 40))
      return false;
    out = (long long)z;
    return true;
  }

  /// Signed arithmetic result check with a wider intermediate type so the
  /// overflow is seen before wrapping.
  bool arithmeticOverflow(const Expr *op, long long a, long long b,
                          bool &overflow) const {
    unsigned w = ctx_.getIntWidth(op->getType());
    if (w == 0 || w > 64)
      return false;
    __int128 lo = -((__int128)1 << (w - 1));
    __int128 hi = ((__int128)1 << (w - 1)) - 1;
    __int128 r = 0;
    const auto *bo = dyn_cast<BinaryOperator>(op);
    if (!bo) return false;
    switch (bo->getOpcode()) {
    case BO_Add: r = (__int128)a + b; break;
    case BO_Sub: r = (__int128)a - b; break;
    case BO_Mul: r = (__int128)a * b; break;
    default: return false;
    }
    overflow = r < lo || r > hi;
    return true;
  }

  /// A divisor is provably non-zero when it is a known constant, a variable
  /// known non-zero on the current guarded path, or a loop counter whose
  /// lower bound (plus a positive offset) keeps it away from zero.
  bool provablyNonZero(const Expr *e) const {
    llvm::APSInt v;
    if (knownConst(e, v))
      return !v.isZero();
    const Expr *x = e->IgnoreParenCasts();
    if (const auto *ref = dyn_cast<DeclRefExpr>(x))
      if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl())) {
        if (nonZeroVars_.count(vd))
          return true;
        auto it = loopLowerBound_.find(vd);
        if (it != loopLowerBound_.end() && it->second > 0)
          return true;
      }
    if (const auto *bo = dyn_cast<BinaryOperator>(x)) {
      llvm::APSInt off;
      if (bo->getOpcode() == BO_Add &&
          knownConst(bo->getRHS(), off) && !off.isNegative()) {
        if (const auto *ref = dyn_cast<DeclRefExpr>(
                bo->getLHS()->IgnoreParenCasts()))
          if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl())) {
            auto it = loopLowerBound_.find(vd);
            if (it != loopLowerBound_.end() && it->second >= 0)
              return true;
          }
      }
    }
    return false;
  }

  /// Record the byte extent of a heap allocation performed in a variable's
  /// initializer.  Called in both passes so the checking pass sees the same
  /// facts as the collection pass.
  void recordHeapAllocation(VarDecl *vd) {
    if (!vd || !vd->getType()->isPointerType() || !vd->getInit())
      return;
    const Expr *init = vd->getInit()->IgnoreParenCasts();
    const auto *call = dyn_cast<CallExpr>(init);
    if (!call || !call->getDirectCallee() || call->getNumArgs() < 1)
      return;
    std::string callee = call->getDirectCallee()->getNameAsString();
    if (callee != "malloc" && callee != "calloc" && callee != "realloc")
      return;
    // realloc(NULL, n) is malloc(n): the extent comes from argument 1.
    if (callee == "realloc" && call->getNumArgs() >= 2 &&
        isNullConstant(call->getArg(0)->IgnoreParenCasts())) {
      const Expr *sizeArg = call->getArg(1)->IgnoreParenImpCasts();
      long long n = 0;
      if (evalByteCount(sizeArg, n) && n > 0) {
        HeapAllocState st;
        st.bytes = n;
        st.decl = vd;
        heapAllocs_[vd->getNameAsString()] = st;
      }
      return;
    }
    const Expr *arg = call->getArg(0)->IgnoreParenImpCasts();

    // An enormous constant allocator request definitely fails.  Treat its
    // result as provably null so the existing null-dereference check (with
    // its guard suppression) rejects an immediate dereference.
    Expr::EvalResult hr;
    if (arg->EvaluateAsInt(hr, ctx_) && hr.Val.isInt()) {
      llvm::APSInt hv = hr.Val.getInt();
      if (hv.isNegative() ||
          (hv.isUnsigned() && hv.isMaxValue()) ||
          hv.getZExtValue() >= (1ULL << 40)) {
        if (report_)
          nullVars_.insert(vd->getNameAsString());
        return;
      }
    }

    // `malloc(strlen(src))` is the classic missing-NUL bug: one byte too few
    // for a later strcpy/strcat.  Record the source for the copy check
    // instead of a byte count.
    if (const auto *sl = dyn_cast<CallExpr>(arg)) {
      if (sl->getDirectCallee() &&
          sl->getDirectCallee()->getNameAsString() == "strlen" &&
          sl->getNumArgs() == 1) {
        if (report_)
          strlenSized_[vd] = varNameOf(sl->getArg(0));
        return;
      }
    }

    long long bytes = -1;
    if (!evalByteCount(arg, bytes)) {
        return;
    }
    // calloc(count, size) allocates count * size bytes, not just count.
    if (callee == "calloc" && call->getNumArgs() >= 2) {
      long long elem = 1;
      if (evalByteCount(call->getArg(1)->IgnoreParenImpCasts(), elem) &&
          elem > 0 && bytes <= LLONG_MAX / elem)
        bytes *= elem;
    }
    HeapAllocState st;
    st.bytes = bytes;
    st.zeroInit = (callee == "calloc");
    st.decl = vd;
    heapAllocs_[vd->getNameAsString()] = st;
  }

  /// True when `e` is a plain reference to a heap variable whose recorded
  /// extent still applies.
  const HeapAllocState *heapStateOf(const Expr *e) const {
    if (!e)
      return nullptr;
    const auto *ref = dyn_cast<DeclRefExpr>(e->IgnoreParenImpCasts());
    if (!ref)
      return nullptr;
    const auto *vd = dyn_cast<VarDecl>(ref->getDecl());
    if (!vd)
      return nullptr;
    auto it = heapAllocs_.find(vd->getNameAsString());
    if (it == heapAllocs_.end() || it->second.decl != vd)
      return nullptr;
    return &it->second;
  }

  /// Resolve a subscript/offset index to a compile-time value.  The worst
  /// case index of an enclosing counted loop is preferred over a local
  /// constant, because a loop induction variable is not actually constant.
  bool evalIndexValue(const Expr *idx, long long &out) const {
    if (!idx)
      return false;
    // `idx + K` (or `K + idx`): resolve the induction variable then add the
    // constant offset, so `arr[idx + 1]` in a counted loop is checked.
    if (const auto *bo = dyn_cast<BinaryOperator>(idx->IgnoreParenImpCasts())) {
      if (bo->getOpcode() == BO_Add || bo->getOpcode() == BO_Sub) {
        long long base = 0, off = 0;
        const Expr *varExpr = nullptr;
        Expr::EvalResult r;
        if (bo->getRHS()->EvaluateAsInt(r, ctx_) && r.Val.isInt()) {
          off = r.Val.getInt().getSExtValue();
          if (bo->getOpcode() == BO_Sub)
            off = -off;
          varExpr = bo->getLHS();
        } else if (bo->getLHS()->EvaluateAsInt(r, ctx_) && r.Val.isInt() &&
                   bo->getOpcode() == BO_Add) {
          off = r.Val.getInt().getSExtValue();
          varExpr = bo->getRHS();
        }
        if (varExpr && evalIndexValue(varExpr, base)) {
          out = base + off;
          return true;
        }
      }
    }
    if (const auto *ref = dyn_cast<DeclRefExpr>(idx->IgnoreParenImpCasts())) {
      if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl())) {
        auto ov = loopIndexOverride_.find(vd);
        if (ov != loopIndexOverride_.end()) {
          out = ov->second;
          return true;
        }
      }
    }
    Expr::EvalResult r;
    if (idx->EvaluateAsInt(r, ctx_) && r.Val.isInt()) {
      out = r.Val.getInt().getSExtValue();
      return true;
    }
    std::string name = varNameOf(idx);
    auto it = constLocals_.find(name);
    if (name.empty() || it == constLocals_.end() || assignedVars_.count(name) ||
        guardedFromConst_.count(name))
      return false;
    out = it->second.getSExtValue();
    return true;
  }

  /// Recognise `for (T i = 0; i < N; i++)` / `i <= N` with a constant bound
  /// and unit increment.  The worst-case index is used only for heap-access
  /// checks inside the loop body.
  bool parseLoopBound(const ForStmt *s, const VarDecl *&vd,
                      long long &maxIndex) const {
    const VarDecl *ivd = nullptr;
    if (const auto *ds = dyn_cast_or_null<DeclStmt>(s->getInit())) {
      for (const Decl *d : ds->decls()) {
        if (const auto *v = dyn_cast<VarDecl>(d)) {
          ivd = v;
          break;
        }
      }
    }
    if (!ivd || !ivd->getInit() || !ivd->getType()->isIntegerType())
      return false;
    Expr::EvalResult ir;
    if (!ivd->getInit()->EvaluateAsInt(ir, ctx_) || !ir.Val.isInt())
      return false;
    if (ir.Val.getInt().getSExtValue() != 0)
      return false;
    const auto *bo = dyn_cast_or_null<BinaryOperator>(s->getCond());
    if (!bo || (bo->getOpcode() != BO_LT && bo->getOpcode() != BO_LE))
      return false;
    const auto *lref =
        dyn_cast<DeclRefExpr>(bo->getLHS()->IgnoreParenImpCasts());
    if (!lref || lref->getDecl() != ivd)
      return false;
    long long bound = 0;
    llvm::APSInt bv;
    if (!knownConst(bo->getRHS(), bv))
      return false;
    bound = bv.isNegative() ? bv.getSExtValue()
                            : (long long)bv.getZExtValue();
    if (bound < 0)
      return false;
    const Expr *inc = s->getInc();
    if (!inc)
      return false;
    const Expr *ie = inc->IgnoreParenImpCasts();
    if (const auto *uo = dyn_cast<UnaryOperator>(ie)) {
      if (uo->getOpcode() != UO_PostInc && uo->getOpcode() != UO_PreInc)
        return false;
    } else if (const auto *bio = dyn_cast<BinaryOperator>(ie)) {
      if (bio->getOpcode() != BO_AddAssign)
        return false;
      Expr::EvalResult sr;
      if (!bio->getRHS()->EvaluateAsInt(sr, ctx_) || !sr.Val.isInt() ||
          sr.Val.getInt() != 1)
        return false;
    } else {
      return false;
    }
    maxIndex = (bo->getOpcode() == BO_LT) ? bound - 1 : bound;
    if (maxIndex < 0)
      return false;
    vd = ivd;
    return true;
  }

  /// Reject array subscripts / pointer offsets beyond a tracked heap extent.
  void checkHeapSubscript(const ArraySubscriptExpr *sub, SourceLocation loc) {
    const HeapAllocState *st = heapStateOf(sub->getBase());
    if (!st)
      return;
    long long idx = 0;
    if (!evalIndexValue(sub->getIdx(), idx))
      return;
    std::string name = varNameOf(sub->getBase());
    if (idx < 0) {
      report(loc, "negative array index on heap allocation '" + name + "'");
      return;
    }
    if (st->bytes < 0)
      return;
    const auto *pt = sub->getBase()->IgnoreParenImpCasts()->getType()->getAs<PointerType>();
    if (!pt)
      return;
    QualType elem = pt->getPointeeType();
    unsigned long long elemSize = 0;
    if ((elem->isIntegerType() || elem->isCharType() ||
         elem->isFloatingType() || elem->isEnumeralType() ||
         elem->isRecordType()) &&
        !elem->isDependentType() && !elem->isIncompleteType())
      elemSize = (unsigned long long)(ctx_.getTypeSize(elem) / 8u);
    if (elemSize == 0)
      return;
    unsigned long long off = (unsigned long long)idx * elemSize;
    if (off >= (unsigned long long)st->bytes)
      report(loc, "array index " + std::to_string(idx) +
                      " exceeds heap allocation '" + name + "' of " +
                      std::to_string(st->bytes) + " bytes");
  }

  /// True when `e` is `array + N` with N exactly one past a fixed array's
  /// end.  Dereferencing such a pointer is undefined behaviour.
  bool isOnePastEndExpr(const Expr *e) const {
    if (!e)
      return false;
    const auto *bo = dyn_cast<BinaryOperator>(e->IgnoreParenImpCasts());
    if (!bo || bo->getOpcode() != BO_Add)
      return false;
    const auto *ref =
        dyn_cast<DeclRefExpr>(bo->getLHS()->IgnoreParenImpCasts());
    const auto *vd = ref ? dyn_cast<VarDecl>(ref->getDecl()) : nullptr;
    if (!vd)
      return false;
    const auto *at = ctx_.getAsConstantArrayType(vd->getType());
    if (!at)
      return false;
    Expr::EvalResult r;
    if (!bo->getRHS()->EvaluateAsInt(r, ctx_) || !r.Val.isInt())
      return false;
    return r.Val.getInt().getSExtValue() ==
           (long long)at->getSize().getZExtValue();
  }

  /// Reject dereferences at the extent of a heap allocation or one past a
  /// fixed array, and reads of heap memory that was never written.
  void checkHeapDeref(const UnaryOperator *op) {
    const Expr *sub = op->getSubExpr();
    SourceLocation loc = op->getBeginLoc();
    if (const auto *ref = dyn_cast<DeclRefExpr>(sub->IgnoreParenImpCasts())) {
      if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl())) {
        if (onePastEnd_.count(vd)) {
          report(loc, "dereference one past the end of array via pointer "
                      "arithmetic");
          return;
        }
      }
    }
    const HeapAllocState *st = heapStateOf(sub);
    if (st) {
      std::string name = varNameOf(sub);
      if (st->bytes == 0) {
        report(loc, "dereference of zero-size heap allocation '" + name +
                        "' (array index out of bounds)");
        return;
      }
      if (const auto *bo = dyn_cast<BinaryOperator>(sub->IgnoreParenImpCasts())) {
        if (bo->getOpcode() == BO_Add || bo->getOpcode() == BO_Sub) {
          const HeapAllocState *baseSt = heapStateOf(bo->getLHS());
          if (baseSt && baseSt->bytes >= 0) {
            long long off = 0;
            if (evalIndexValue(bo->getRHS(), off)) {
              if (bo->getOpcode() == BO_Sub)
                off = -off;
              const auto *pt = bo->getLHS()
                                   ->IgnoreParenImpCasts()
                                   ->getType()
                                   ->getAs<PointerType>();
              if (pt) {
                QualType elem = pt->getPointeeType();
                unsigned long long es = 0;
                if ((elem->isIntegerType() || elem->isCharType() ||
                     elem->isFloatingType() || elem->isEnumeralType()) &&
                    !elem->isDependentType())
                  es = (unsigned long long)(ctx_.getTypeSize(elem) / 8u);
                if (es != 0) {
                  long long byteOff = off * (long long)es;
                  if (byteOff < 0 ||
                      (unsigned long long)byteOff >=
                          (unsigned long long)baseSt->bytes)
                    report(loc, "pointer offset " + std::to_string(off) +
                                    " exceeds heap allocation '" +
                                    varNameOf(bo->getLHS()) + "'");
                }
              }
            }
          }
        }
      }
      // Uninitialized-heap reads are intentionally NOT diagnosed here:
      // writes through aliases, out-parameters and callees are not tracked
      // precisely enough, so reporting them produced false positives on
      // valid C (e.g. `int *q=p; *q=1; return *p;`). Kept as a documented gap.
    }
  }

  /// Reject writes through a pointer known to reference a string literal.
  void checkStringLiteralWrite(const Expr *lhs, SourceLocation loc) {
    if (!lhs)
      return;
    const Expr *e = lhs->IgnoreParenImpCasts();
    const Expr *ptrExpr = nullptr;
    if (const auto *ase = dyn_cast<ArraySubscriptExpr>(e))
      ptrExpr = ase->getBase();
    else if (const auto *uo = dyn_cast<UnaryOperator>(e))
      if (uo->getOpcode() == UO_Deref)
        ptrExpr = uo->getSubExpr();
    if (!ptrExpr)
      return;
    const auto *ref = dyn_cast<DeclRefExpr>(ptrExpr->IgnoreParenCasts());
    const auto *vd = ref ? dyn_cast<VarDecl>(ref->getDecl()) : nullptr;
    if (vd && stringPtrs_.count(vd))
      report(loc, "write to string literal '" + vd->getNameAsString() + "'");
  }

  /// `malloc(strlen(src))` followed by strcpy/strcat overflows by the NUL.
  void checkStrcpyNul(const CallExpr *call) {
    if (call->getNumArgs() < 2)
      return;
    const auto *ref = dyn_cast<DeclRefExpr>(
        call->getArg(0)->IgnoreParenImpCasts());
    const auto *vd = ref ? dyn_cast<VarDecl>(ref->getDecl()) : nullptr;
    if (!vd)
      return;
    auto it = strlenSized_.find(vd);
    if (it == strlenSized_.end())
      return;
    std::string srcName = varNameOf(call->getArg(1));
    if (srcName.empty() || srcName == it->second)
      report(call->getBeginLoc(),
             "string copy needs a NUL terminator: buffer '" +
                 vd->getNameAsString() + "' was sized by strlen alone");
  }

  /// Detect frees of objects that cannot be the start of an allocation.
  /// These are always bugs in C and are rejected in both safety modes.
  void checkInvalidFree(SourceLocation loc, const Expr *arg,
                        const std::string &api) {
    if (!arg)
      return;
    const Expr *e = arg->IgnoreParenCasts();

    if (isa<StringLiteral>(e)) {
      report(loc, "invalid free of string literal via " + api);
      return;
    }

    if (const auto *uo = dyn_cast<UnaryOperator>(e)) {
      if (uo->getOpcode() == UO_AddrOf) {
        const Expr *sub = uo->getSubExpr()->IgnoreParenImpCasts();
        if (const auto *ref = dyn_cast<DeclRefExpr>(sub)) {
          const auto *vd = dyn_cast<VarDecl>(ref->getDecl());
          if (vd && !vd->getType()->isPointerType()) {
            report(loc, "invalid free of address of non-heap object '" +
                            vd->getNameAsString() + "' via " + api);
            return;
          }
        }
        if (const auto *me = dyn_cast<MemberExpr>(sub)) {
          report(loc, "invalid free of address of struct field '" +
                          varNameOf(me->getBase()) + "' via " + api);
          return;
        }
        if (const auto *ase = dyn_cast<ArraySubscriptExpr>(sub)) {
          if (isa<MemberExpr>(ase->getBase()->IgnoreParenImpCasts())) {
            report(loc, "invalid free of address of struct field element via " +
                            api);
            return;
          }
          std::string base = varNameOf(ase->getBase());
          const auto *bref = dyn_cast<DeclRefExpr>(
              ase->getBase()->IgnoreParenImpCasts());
          const auto *bvd = bref ? dyn_cast<VarDecl>(bref->getDecl()) : nullptr;
          if (bvd && bvd->getType()->isArrayType()) {
            report(loc, "invalid free of array element '" + bvd->getNameAsString() +
                            "' via " + api);
            return;
          }
          Expr::EvalResult r;
          if (!base.empty() && ase->getIdx()->EvaluateAsInt(r, ctx_) &&
              r.Val.isInt() && r.Val.getInt() != 0) {
            report(loc, "invalid free of interior pointer '" + base +
                            "' via " + api);
            return;
          }
        }
      }
    }

    if (const auto *ref = dyn_cast<DeclRefExpr>(e)) {
      const auto *vd = dyn_cast<VarDecl>(ref->getDecl());
      if (vd && vd->getType()->isArrayType()) {
        report(loc, "invalid free of array '" + vd->getNameAsString() +
                        "' via " + api);
        return;
      }
    }

    // Pointer variables known to point at non-heap storage: `T *p = &obj;`
    // or `const char *s = "literal";`.
    std::string name = varNameOf(arg->IgnoreParenCasts());
    if (!name.empty()) {
      if (const auto *ref = dyn_cast<DeclRefExpr>(arg->IgnoreParenImpCasts())) {
        if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl())) {
          if (heapInterior_.count(vd)) {
            report(loc, "invalid free of interior pointer '" + name +
                            "' via " + api);
            return;
          }
        }
      }
      const auto *sref = dyn_cast<DeclRefExpr>(e->IgnoreParenCasts());
      const auto *svd = sref ? dyn_cast<VarDecl>(sref->getDecl()) : nullptr;
      if (svd && stringPtrs_.count(svd)) {
        report(loc, "invalid free of string-literal pointer '" + name +
                        "' via " + api);
        return;
      }
      auto bit = borrowOwner_.find(name);
      if (bit != borrowOwner_.end()) {
        report(loc, "invalid free of address of non-heap object '" + name +
                        "' via " + api);
        return;
      }
    }

    if (const auto *bo = dyn_cast<BinaryOperator>(e)) {
      if (bo->getOpcode() == BO_Add || bo->getOpcode() == BO_Sub) {
        long long offset = 0;
        Expr::EvalResult r;
        if (bo->getRHS()->EvaluateAsInt(r, ctx_) && r.Val.isInt())
          offset = r.Val.getInt().getSExtValue();
        else {
          std::string n = varNameOf(bo->getRHS());
          auto it = constLocals_.find(n);
          if (!n.empty() && it != constLocals_.end() &&
              !assignedVars_.count(n))
            offset = it->second.getSExtValue();
        }
        if (offset != 0)
          report(loc, "invalid free of interior pointer '" +
                          varNameOf(bo->getLHS()) + "' via " + api);
      }
    }
  }

  void markReleased(const std::string &name) {
    if (name.empty())
      return;
    releasedVars_.insert(name);
    auto it = aliasGroups_.find(name);
    if (it != aliasGroups_.end())
      for (const auto &alias : it->second)
        releasedVars_.insert(alias);
    for (const auto &entry : borrowOwner_)
      if (entry.second == name)
        releasedVars_.insert(entry.first);
  }

  void clearReleased(const std::string &name) {
    if (name.empty())
      return;
    releasedVars_.erase(name);
    auto it = aliasGroups_.find(name);
    if (it != aliasGroups_.end())
      for (const auto &alias : it->second)
        releasedVars_.erase(alias);
    for (const auto &entry : borrowOwner_)
      if (entry.second == name)
        releasedVars_.erase(entry.first);
  }

  /// Canonical key for a struct field / array-element lvalue, used for
  /// exact field-sensitive release tracking (e.g. `s.p`, `a[0].p`).
  std::string lvalueKey(const Expr *expr) const {
    if (!expr)
      return std::string();
    const Expr *e = expr->IgnoreParenCasts();
    if (const auto *me = dyn_cast<MemberExpr>(e)) {
      std::string base = lvalueKey(me->getBase());
      if (base.empty())
        return std::string();
      return base + (me->isArrow() ? "->" : ".") + me->getMemberDecl()->getNameAsString();
    }
    if (const auto *ase = dyn_cast<ArraySubscriptExpr>(e)) {
      std::string base = lvalueKey(ase->getBase());
      Expr::EvalResult r;
      if (base.empty() || !ase->getIdx()->EvaluateAsInt(r, ctx_))
        return std::string();
      return base + "[" + std::to_string(r.Val.getInt().getSExtValue()) + "]";
    }
    if (const auto *ref = dyn_cast<DeclRefExpr>(e))
      if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl()))
        return vd->getNameAsString();
    return std::string();
  }

  bool isReleased(const Expr *expr) const {
    std::string key = lvalueKey(expr);
    if (releasedVars_.count(varNameOf(expr)) != 0 ||
        (!key.empty() && releasedFields_.count(key) != 0))
      return true;
    // A field alias of an already-released field is also dangling.
    auto it = fieldAliases_.find(key);
    if (it != fieldAliases_.end())
      for (const auto &alias : it->second)
        if (releasedFields_.count(alias))
          return true;
    return false;
  }

  bool isMaybeReleased(const Expr *expr, SourceLocation at = {}) const {
    std::string name = varNameOf(expr);
    if (!maybeReleasedVars_.count(name))
      return false;
    if (!at.isValid())
      return true;
    auto it = maybeReleaseLines_.find(name);
    unsigned pos = at.getRawEncoding();
    return it != maybeReleaseLines_.end() && it->second && pos &&
           it->second < pos;
  }

  // Do not propagate release state across branches: a free in one branch is
  // not a guarantee on every path.
  std::string ownerOfAddress(const Expr *sub) const {
    if (!sub)
      return std::string();
    const Expr *e = sub->IgnoreParenImpCasts();
    if (const auto *ase = dyn_cast<ArraySubscriptExpr>(e))
      e = ase->getBase()->IgnoreParenImpCasts();
    std::string owner = varNameOf(e);
    if (owner.empty())
      if (const auto *me = dyn_cast<MemberExpr>(e))
        owner = varNameOf(me->getBase());
    return owner;
  }

  bool containsAssignFrom(const Stmt *node, const std::string &lhs,
                          const std::string &rhs) const {
    if (!node)
      return false;
    if (const auto *bo = dyn_cast<BinaryOperator>(node)) {
      if (bo->isAssignmentOp() && varNameOf(bo->getLHS()) == lhs &&
          varNameOf(bo->getRHS()) == rhs)
        return true;
    }
    for (const Stmt *child : node->children())
      if (containsAssignFrom(child, lhs, rhs))
        return true;
    return false;
  }

  static bool containsReturn(const Stmt *node) {
    if (!node)
      return false;
    if (isa<ReturnStmt>(node))
      return true;
    for (const Stmt *child : node->children())
      if (containsReturn(child))
        return true;
    return false;
  }

  bool detectReallocSuccess(const IfStmt *s, std::string &owner,
                            std::string &tmp,
                            const CallExpr **out = nullptr) const {
    if (!s->getCond())
      return false;
    const Expr *condExpr = s->getCond();
    const Expr *tmpRef = nullptr;
    bool negated = false;
    if (const auto *uo = dyn_cast<UnaryOperator>(condExpr->IgnoreParenImpCasts())) {
      if (uo->getOpcode() == UO_LNot) {
        negated = true;
        tmpRef = uo->getSubExpr()->IgnoreParenImpCasts();
        tmp = varNameOf(tmpRef);
      }
    }
    if (!negated) {
      if (const auto *bo = dyn_cast<BinaryOperator>(condExpr)) {
        tmp = varNameOf(bo->getLHS());
        tmpRef = bo->getLHS()->IgnoreParenImpCasts();
        if (tmp.empty()) {
          tmp = varNameOf(bo->getRHS());
          tmpRef = bo->getRHS()->IgnoreParenImpCasts();
        }
      } else {
        tmp = varNameOf(condExpr);
        tmpRef = condExpr->IgnoreParenImpCasts();
      }
    }
    if (tmp.empty() || !tmpRef)
      return false;
    const auto *ref = dyn_cast<DeclRefExpr>(tmpRef);
    const auto *vd = ref ? dyn_cast<VarDecl>(ref->getDecl()) : nullptr;
    if (!vd || !vd->getInit())
      return false;
    const auto *call = dyn_cast<CallExpr>(vd->getInit()->IgnoreParenImpCasts());
    if (!call || !call->getDirectCallee() ||
        call->getDirectCallee()->getNameAsString() != "realloc" ||
        call->getNumArgs() < 1)
      return false;
    // `realloc(*pp, n)` with `*pp = t` in the success branch is the standard
    // out-parameter idiom and is fully handled.
    bool throughOutParam = false;
    if (const auto *uo = dyn_cast<UnaryOperator>(
            call->getArg(0)->IgnoreParenImpCasts()))
      if (uo->getOpcode() == UO_Deref) {
        owner = varNameOf(uo->getSubExpr());
        throughOutParam = true;
      }
    if (!throughOutParam)
      owner = varNameOf(call->getArg(0));
    if (out)
      *out = call;
    if (throughOutParam) {
      if (negated && containsReturn(s->getThen()))
        return !owner.empty();
      std::string rhs = tmp;
      bool assigned = false;
      std::function<void(const Stmt *)> scan = [&](const Stmt *node) {
        if (!node || assigned)
          return;
        if (const auto *bo = dyn_cast<BinaryOperator>(node))
          if (bo->isAssignmentOp() && varNameOf(bo->getRHS()) == rhs) {
            if (const auto *lu = dyn_cast<UnaryOperator>(
                    bo->getLHS()->IgnoreParenImpCasts()))
              if (lu->getOpcode() == UO_Deref &&
                  varNameOf(lu->getSubExpr()) == owner)
                assigned = true;
          }
        for (const Stmt *c : node->children())
          scan(c);
      };
      scan(s->getThen());
      return !owner.empty() && assigned;
    }
    if (negated && containsReturn(s->getThen()))
      return !owner.empty(); // failure branch exits: ownership is explicit
    return !owner.empty() && containsAssignFrom(s->getThen(), owner, tmp);
  }

  void collectMayRelease(Stmt *body) {
    if (!body)
      return;
    // A branch that returns cannot leave a may-release state at the join
    // point. Skipping it avoids false positives for realloc-failure cleanup
    // like `void *t = realloc(p,n); if (!t) { free(p); return 1; }`.
    struct ExitFinder : RecursiveASTVisitor<ExitFinder> {
      bool exits = false;
      bool VisitReturnStmt(ReturnStmt *) { exits = true; return true; }
      bool VisitContinueStmt(ContinueStmt *) { exits = true; return true; }
      bool VisitBreakStmt(BreakStmt *) { exits = true; return true; }
    } exits;
    exits.TraverseStmt(body);
    if (exits.exits)
      return;
    ReleaseCollector collector;
    collector.TraverseStmt(body);
    unsigned pos = body->getBeginLoc().getRawEncoding();
    for (const auto &name : collector.released) {
      maybeReleasedVars_.insert(name);
      auto it = maybeReleaseLines_.find(name);
      if (it == maybeReleaseLines_.end() || (pos && pos < it->second))
        maybeReleaseLines_[name] = pos;
    }
  }

  bool TraverseIfStmt(IfStmt *s) {
    // `n = fread(buf, ...); if (n == 0) return buf[0];` — when fread returns
    // zero it wrote nothing, so the destination array is uninitialized.
    if (report_ && s->getCond() && s->getThen()) {
      const BinaryOperator *eq = dyn_cast<BinaryOperator>(
          s->getCond()->IgnoreParenCasts());
      if (eq && eq->getOpcode() == BO_EQ) {
        auto zeroVar = [&](const Expr *e) -> std::string {
          Expr::EvalResult r;
          if (e->IgnoreParenCasts()->EvaluateAsInt(r, ctx_) && r.Val.isInt() &&
              r.Val.getInt() == 0)
            return "ZERO";
          if (const auto *ref = dyn_cast<DeclRefExpr>(e->IgnoreParenCasts()))
            return ref->getDecl()->getNameAsString();
          return std::string();
        };
        std::string a = zeroVar(eq->getLHS()), b = zeroVar(eq->getRHS());
        std::string resultVar;
        if (a == "ZERO" && b != "ZERO") resultVar = b;
        else if (b == "ZERO" && a != "ZERO") resultVar = a;
        auto rit = freadResults_.find(resultVar);
        if (rit != freadResults_.end()) {
          const std::string &dest = rit->second;
          std::function<void(const Stmt *)> scan = [&](const Stmt *n) {
            if (!n) return;
            if (const auto *sub = dyn_cast<ArraySubscriptExpr>(n))
              if (const auto *ref = dyn_cast<DeclRefExpr>(
                      sub->getBase()->IgnoreParenCasts()))
                if (ref->getDecl()->getNameAsString() == dest)
                  report(sub->getBeginLoc(),
                         "read of uninitialized array '" + dest +
                             "'; fread returned zero and wrote nothing");
            for (const Stmt *c : n->children()) scan(c);
          };
          scan(s->getThen());
        }
      }
    }
    ++conditionalDepth_;
    if (!report_) {
      collectMayRelease(s->getThen());
      collectMayRelease(s->getElse());
      // First pass: record realloc calls whose result is handled by an
      // explicit success/failure branch so the STRICT mode does not reject
      // the documented-safe realloc pattern as unverified.
      std::string owner, tmp;
      const CallExpr *rc = nullptr;
      if (detectReallocSuccess(s, owner, tmp, &rc) && rc)
        handledReallocs_.insert(rc->getBeginLoc().getRawEncoding());
    }
    std::string keepEraseVar;
    bool negatedGuard = false;
    if (s->getCond())
      if (const auto *uo = dyn_cast<UnaryOperator>(
              s->getCond()->IgnoreParenImpCasts()))
        negatedGuard = uo->getOpcode() == UO_LNot;
    // `if (!p) p = &x;` — the assignment in the then arm makes p non-null
    bool keepErase = false;
    // on both paths after the statement. Also handles `if (p == NULL) p = &x;`.
    if (report_ && s->getThen()) {
      std::string n;
      if (negatedGuard)
        n = varNameOf(
            cast<UnaryOperator>(s->getCond()->IgnoreParenImpCasts())
                ->getSubExpr());
      if (n.empty())
        if (const auto *bo = dyn_cast_or_null<BinaryOperator>(
                s->getCond() ? s->getCond()->IgnoreParenImpCasts() : nullptr))
          if (bo->isEqualityOp()) {
            n = varNameOf(bo->getLHS());
            if (n.empty() && isNullConstant(bo->getLHS()))
              n = varNameOf(bo->getRHS());
          }
      if (!n.empty()) {
        bool assigns = false;
        std::function<void(const Stmt *)> scan = [&](const Stmt *node) {
          if (!node || assigns)
            return;
          if (const auto *bo = dyn_cast<BinaryOperator>(node))
            if (bo->isAssignmentOp() && varNameOf(bo->getLHS()) == n)
              assigns = true;
          for (const Stmt *c : node->children())
            scan(c);
        };
        if (scan(s->getThen()), assigns) {
          nullVars_.erase(n);
          keepErase = true;
          keepEraseVar = n;
        }
      }
    }
    // `if (!p) return; else ...`: the else arm is the non-null path.
    if (report_ && negatedGuard && s->getElse() &&
        containsReturn(s->getThen())) {
      std::string n = varNameOf(
          cast<UnaryOperator>(s->getCond()->IgnoreParenImpCasts())
              ->getSubExpr());
      if (!n.empty())
        nullVars_.erase(n);
    }
    if (report_) {
      std::string owner, tmp;
      if (detectReallocSuccess(s, owner, tmp)) {
        bool r = TraverseStmt(s->getCond());
        if (negatedGuard) {
          // The then-branch is the FAILURE path (e.g. `if(!t){free(p);
          // return 1;}`). Ownership stays with the original allocation.
          std::string savedRebindNeg = reallocRebindOwner_;
          if (!owner.empty() && !tmp.empty()) {
            reallocRebindOwner_ = owner;
            reallocRebindTemp_ = tmp;
          }
          r &= TraverseStmt(s->getThen());
          if (s->getElse())
            r &= TraverseStmt(s->getElse());
          // If that failure arm exits the function, the only continuing path
          // is realloc SUCCESS: old aliases are invalid from here on.
          bool failureExitsNeg = !s->getElse() &&
                                 containsReturn(s->getThen());
          if (failureExitsNeg)
            markReleased(owner); // keep rebind info for following code
          else
            reallocRebindOwner_ = savedRebindNeg;
          --conditionalDepth_;
          return r;
        }
        // `p = tmp` inside the success branch rebinds the owner to the new
        // block; only that name becomes live again, stale aliases stay dead.
        std::string savedRebind = reallocRebindOwner_;
        if (!owner.empty() && !tmp.empty()) {
          reallocRebindOwner_ = owner;
          reallocRebindTemp_ = tmp;
        }
        // `if (!t) return;` exits on failure, so the only continuing path is
        // realloc success: the old pointer and its aliases stay invalid after
        // the statement. Otherwise they are only invalid inside the branch.
        bool failureExits = negatedGuard && !s->getElse() &&
                            containsReturn(s->getThen());
        if (failureExits) {
          r &= TraverseStmt(s->getThen());
          clearReleased(owner);
          markReleased(owner); // persist for code after the if
        } else {
          markReleased(owner);
          r &= TraverseStmt(s->getThen());
          clearReleased(owner);
        }
        reallocRebindOwner_ = savedRebind;
        if (s->getElse())
          r &= TraverseStmt(s->getElse());
        --conditionalDepth_;
        return r;
      }
    }
    // `if (p) { ... }` tests the pointer for non-null. Suppress the provably
    // null state for that branch so a valid guarded dereference is not a
    // false positive. With an else branch the null path remains checked.
    std::set<std::string> savedNulls = nullVars_;
    if (report_ && !s->getElse()) {
      std::string condVar = varNameOf(s->getCond());
      if (const auto *uo = dyn_cast_or_null<UnaryOperator>(
              s->getCond() ? s->getCond()->IgnoreParenImpCasts() : nullptr))
        if (uo->getOpcode() == UO_LNot) {
          condVar = varNameOf(uo->getSubExpr());
          // `if (!p) return;` proves p non-null after the statement.
          keepErase = containsReturn(s->getThen());
        }
      if (const auto *bo = dyn_cast_or_null<BinaryOperator>(
              s->getCond() ? s->getCond()->IgnoreParenImpCasts() : nullptr))
        if (bo->isEqualityOp() && containsReturn(s->getThen())) {
          std::string n = varNameOf(bo->getLHS());
          if (n.empty() && isNullConstant(bo->getLHS()))
            n = varNameOf(bo->getRHS());
          if (!n.empty()) {
            guardedFromConst_.insert(n);
            // `if (p == 0) return;` proves p is non-null afterwards.
            condVar = n;
            keepErase = true;
            keepEraseVar = n;
          }
        }
      if (!condVar.empty())
        nullVars_.erase(condVar);
    }
    // `if (p) {...} else return;` — the else path exits, so after the if p
    // is non-null. Also guard the then arm itself.
    if (report_ && s->getElse() && containsReturn(s->getElse())) {
      std::string condVar = varNameOf(s->getCond());
      if (const auto *uo = dyn_cast_or_null<UnaryOperator>(
              s->getCond() ? s->getCond()->IgnoreParenImpCasts() : nullptr))
        if (uo->getOpcode() == UO_LNot)
          condVar = varNameOf(uo->getSubExpr());
      if (!condVar.empty()) {
        nullVars_.erase(condVar);
        keepErase = true;
        keepEraseVar = condVar;
      }
    }
    // `if (n < capacity) buf[n] = 0;` bounds a snprintf return value used as
    // an index; suppress the truncation diagnostic inside that guarded arm.
    std::set<std::string> savedGuarded = guardedSnprintf_;
    if (report_ && s->getCond()) {
      if (const auto *bo = dyn_cast<BinaryOperator>(
              s->getCond()->IgnoreParenCasts())) {
        if (bo->getOpcode() == BO_LT || bo->getOpcode() == BO_LE) {
          Expr::EvalResult er;
          if (bo->getRHS()->EvaluateAsInt(er, ctx_) && er.Val.isInt()) {
            if (const auto *ref = dyn_cast<DeclRefExpr>(
                    bo->getLHS()->IgnoreParenCasts()))
              if (snprintfResults_.count(ref->getDecl()->getNameAsString()))
                guardedSnprintf_.insert(ref->getDecl()->getNameAsString());
          }
        }
      }
    }
    const VarDecl *ifGuardVar = nullptr;
    if (report_ && s->getCond())
      if (const auto *ref = dyn_cast<DeclRefExpr>(
              s->getCond()->IgnoreParenCasts()))
        if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl()))
          if (vd->getType()->isIntegerType()) {
            ifGuardVar = vd;
            nonZeroVars_.insert(vd);
          }
    bool r = RecursiveASTVisitor::TraverseIfStmt(s);
    if (ifGuardVar)
      nonZeroVars_.erase(ifGuardVar);
    guardedSnprintf_ = savedGuarded;
    if (keepErase) {
      // The branch we kept may itself assign `p = NULL` (RAII lowering does
      // this on failure paths). The continuing path is still non-null.
      for (const auto &kv : savedNulls)
        nullVars_.erase(kv);
      if (!keepEraseVar.empty())
        nullVars_.erase(keepEraseVar);
    } else {
      nullVars_ = savedNulls;
    }
    --conditionalDepth_;
    return r;
  }
  bool TraverseForStmt(ForStmt *s) {
    ++conditionalDepth_;
    if (!report_)
      collectMayRelease(s->getBody());
    const VarDecl *induction = nullptr;
    long long worstIndex = 0;
    bool haveBound = report_ && parseLoopBound(s, induction, worstIndex);
    // Record the loop counter's starting value so divisions by the counter
    // (or counter + positive offset) can be proved non-zero.
    long long savedLower = 0;
    bool hadLower = false;
    if (report_ && induction && induction->getInit()) {
      Expr::EvalResult lr;
      if (induction->getInit()->EvaluateAsInt(lr, ctx_) && lr.Val.isInt()) {
        auto it = loopLowerBound_.find(induction);
        hadLower = it != loopLowerBound_.end();
        if (hadLower) savedLower = it->second;
        loopLowerBound_[induction] = lr.Val.getInt().getSExtValue();
      }
    }
    auto oldIt = loopIndexOverride_.find(induction);
    bool hadOld = oldIt != loopIndexOverride_.end();
    long long saved = hadOld ? oldIt->second : 0;
    if (haveBound)
      loopIndexOverride_[induction] = worstIndex;
    bool r = RecursiveASTVisitor::TraverseForStmt(s);
    if (haveBound) {
      if (hadOld)
        loopIndexOverride_[induction] = saved;
      else
        loopIndexOverride_.erase(induction);
    }
    if (report_ && induction && induction->getInit()) {
      Expr::EvalResult lr;
      if (induction->getInit()->EvaluateAsInt(lr, ctx_) && lr.Val.isInt()) {
        if (hadLower) loopLowerBound_[induction] = savedLower;
        else loopLowerBound_.erase(induction);
      }
    }
    --conditionalDepth_;
    return r;
  }
  bool TraverseWhileStmt(WhileStmt *s) {
    ++conditionalDepth_;
    if (!report_)
      collectMayRelease(s->getBody());
    const VarDecl *guardVar = nullptr;
    if (report_ && s->getCond())
      if (const auto *ref = dyn_cast<DeclRefExpr>(
              s->getCond()->IgnoreParenCasts()))
        if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl()))
          if (vd->getType()->isIntegerType()) {
            guardVar = vd;
            nonZeroVars_.insert(vd);
          }
    bool r = RecursiveASTVisitor::TraverseWhileStmt(s);
    if (guardVar)
      nonZeroVars_.erase(guardVar);
    --conditionalDepth_;
    return r;
  }
  bool TraverseDoStmt(DoStmt *s) {
    ++conditionalDepth_;
    if (!report_)
      collectMayRelease(s->getBody());
    bool r = RecursiveASTVisitor::TraverseDoStmt(s);
    --conditionalDepth_;
    return r;
  }
  bool TraverseSwitchStmt(SwitchStmt *s) {
    ++conditionalDepth_;
    bool r = RecursiveASTVisitor::TraverseSwitchStmt(s);
    --conditionalDepth_;
    return r;
  }

  bool TraverseFunctionDecl(FunctionDecl *fd) {
    std::string previousFunction = currentFunction_;
    std::set<std::string> previousNulls = nullVars_;
    currentFunction_ = fd->getNameAsString();
    currentFnDecl_ = fd;
    stringPtrs_.clear(); // names are function-scoped, not TU-scoped
    if (report_) {
      nullVars_.clear();
      releasedVars_.clear();
      releasedFields_.clear();
      aliasGroups_.clear();
      borrowOwner_.clear();
      writeBorrows_.clear();
      movedVars_.clear();
      heapAllocs_.clear();
      heapWritten_.clear();
      heapInterior_.clear();
      onePastEnd_.clear();
      strlenSized_.clear();

      auto pit = callParamExtents_.find(currentFunction_);
      if (pit != callParamExtents_.end())
        for (const auto *pv : fd->parameters()) {
          auto eit = pit->second.find(pv->getFunctionScopeIndex());
          if (eit == pit->second.end())
            continue;
          HeapAllocState st;
          st.bytes = eit->second;
          st.decl = pv;
          heapAllocs_[pv->getNameAsString()] = st;
        }
    }
    bool result = RecursiveASTVisitor::TraverseFunctionDecl(fd);
    currentFunction_ = previousFunction;
    nullVars_ = previousNulls;
    return result;
  }

  bool VisitCastExpr(CastExpr *cast) {
    if (!report_)
      return true;
    const Expr *sub = cast->getSubExpr()->IgnoreParenCasts();
    // Record integers derived from a real pointer: `(uintptr_t)p` and tagged
    // forms. Casting such an integer back to a pointer preserves provenance.
    // (the variable-level case is recorded in VisitVarDecl)
    if (!sub || !cast->getType()->isPointerType() ||
        !sub->getType()->isIntegerType() || isNullConstant(sub))
      return true;
    if (const auto *ref = dyn_cast<DeclRefExpr>(sub))
      if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl()))
        if (provenanceInts_.count(vd->getNameAsString()))
          return true; // verified round-trip
    // A mask/tag operation on a provenance-tracked integer (u & ~1) restores
    // the original pointer and is a valid round-trip.
    if (const auto *bo = dyn_cast<BinaryOperator>(sub))
      if (bo->isBitwiseOp()) {
        auto mentionsProvenance = [&](const Expr *e) -> bool {
          if (const auto *r = dyn_cast<DeclRefExpr>(e->IgnoreParenCasts()))
            if (const auto *v = dyn_cast<VarDecl>(r->getDecl()))
              return provenanceInts_.count(v->getNameAsString()) != 0;
          return false;
        };
        if (mentionsProvenance(bo->getLHS()) ||
            mentionsProvenance(bo->getRHS()))
          return true;
      }
    // A pointer forged from an arbitrary integer has no provenance that the
    // checker can verify. Reject in STRICT, warn in DEFAULT.
    warnAt(cast->getBeginLoc(),
           "pointer created from integer value has unverifiable provenance");
    return true;
  }

  // Conservatively record functions whose every return yields a null
  // constant. Any pointer initialized from such a call is provably null, so
  // a later dereference can be diagnosed. We require at least one return and
  // that all returns are null; a function with any non-null return (or a
  // valueless `return;`) is deliberately left untracked to limit false
  // positives.
  bool VisitFunctionDecl(FunctionDecl *fd) {
    if (report_ || !fd || !fd->getBody())
      return true;
    struct ReturnFinder : RecursiveASTVisitor<ReturnFinder> {
      const SafetyVisitor &owner;
      bool allNull = true;
      bool sawReturn = false;
      explicit ReturnFinder(const SafetyVisitor &o) : owner(o) {}
      bool VisitReturnStmt(ReturnStmt *r) {
        sawReturn = true;
        if (!owner.isNullConstant(r->getRetValue()))
          allNull = false;
        return true;
      }
    };
    ReturnFinder finder(*this);
    finder.TraverseStmt(fd->getBody());
    if (finder.sawReturn && finder.allNull)
      nullReturningFunctions_.insert(fd->getNameAsString());
    return true;
  }

  bool VisitReturnStmt(ReturnStmt *s) {
    if (report_ || !s->getRetValue() || !currentFnDecl_ ||
        !currentFnDecl_->getBody())
      return true;
    std::string name = varNameOf(s->getRetValue());
    if (name.empty())
      return true;
    FreeCallFinder finder(name);
    finder.TraverseStmt(currentFnDecl_->getBody());
    if (finder.found)
      functionsReturningFreed_.insert(currentFunction_);
    return true;
  }

  bool VisitVarDecl(VarDecl *vd) {
    if (!report_ && vd->getInit()) {
      // `int (*fn)(const char*, ...) = printf;` — calling through fn must
      // still enforce the literal-format rule.
      const Expr *init = vd->getInit()->IgnoreParenImpCasts();
      if (const auto *ref = dyn_cast<DeclRefExpr>(init))
        if (const auto *fd = dyn_cast<FunctionDecl>(ref->getDecl()))
          if (isFormatFunctionName(fd->getNameAsString()))
            formatFnAliases_[vd->getNameAsString()] = fd->getNameAsString();
    }
    if (!report_ && vd->hasGlobalStorage()) {
      allGlobals_.insert(vd->getNameAsString());
      if (!vd->getType().isVolatileQualified())
        nonVolatileGlobals_.insert(vd->getNameAsString());
    }
    recordHeapAllocation(vd);
    if (report_ && vd->getInit()) {
      const Expr *init = vd->getInit()->IgnoreParenImpCasts();
      std::function<bool(const Expr *)> derivedFromPointer = [&](const Expr *e) -> bool {
        if (!e)
          return false;
        if (const auto *cast = dyn_cast<CastExpr>(e))
          return cast->getType()->isIntegerType() &&
                 cast->getSubExpr()->IgnoreParenImpCasts()->getType()
                     ->isPointerType();
        // `((uintptr_t)&x | 1)` and `x & ~mask` preserve provenance.
        if (const auto *bo = dyn_cast<BinaryOperator>(e))
          if (bo->isBitwiseOp())
            return derivedFromPointer(bo->getLHS()) ||
                   derivedFromPointer(bo->getRHS());
        if (const auto *ref = dyn_cast<DeclRefExpr>(e))
          if (const auto *ivd = dyn_cast<VarDecl>(ref->getDecl()))
            return provenanceInts_.count(ivd->getNameAsString()) != 0;
        return false;
      };
      if (derivedFromPointer(init))
        provenanceInts_.insert(vd->getNameAsString());
    }
    if (!report_) {
      // Record local integer constants for constant-propagated bounds checks.
      if (vd->hasLocalStorage() && vd->getType()->isIntegerType() &&
          vd->getInit() && !vd->getType().isVolatileQualified()) {
        Expr::EvalResult r;
        llvm::APSInt folded;
        if (vd->getInit()->EvaluateAsInt(r, ctx_) && r.Val.isInt()) {
          llvm::APSInt cv = r.Val.getInt();
          if (cv.isNegative())
            cv = llvm::APSInt(
                llvm::APInt(cv.getBitWidth(), cv.getZExtValue()), true);
          constLocals_[vd->getNameAsString()] = cv;
        } else if (evalConstArithmetic(vd->getInit(), folded)) {
          constLocals_[vd->getNameAsString()] = folded;
        } else {
        }
      }
      // Record constant-valued struct fields from an aggregate initializer,
      // including designated ones: `struct P p = {.count = N, .size = 16};`
      // makes `p.count` / `p.size` compile-time constants.
      if (vd->hasLocalStorage() && vd->getInit() &&
          !vd->getType().isVolatileQualified()) {
        const Expr *init = vd->getInit()->IgnoreParenImpCasts();
        if (const auto *il = dyn_cast<InitListExpr>(init)) {
          if (const auto *rd = vd->getType()->getAsRecordDecl()) {
            unsigned idx = 0;
            for (const FieldDecl *fd : rd->fields()) {
              if (idx >= il->getNumInits())
                break;
              if (!fd->getType()->isIntegerType()) { ++idx; continue; }
              if (const Expr *e = il->getInit(idx)) {
                Expr::EvalResult r;
                if (e->EvaluateAsInt(r, ctx_) && r.Val.isInt())
                  constLocals_[vd->getNameAsString() + "." +
                               fd->getNameAsString()] = r.Val.getInt();
              }
              ++idx;
            }
          }
        }
      }
      return true;
    }
    // A provably overflowing constant arithmetic expression used to compute
    // a size: the wrapped value silently under-allocates.
    if (vd->hasLocalStorage() && vd->getType()->isIntegerType() &&
        vd->getInit()) {
      const Expr *initE = vd->getInit()->IgnoreParenCasts();
      if (const auto *bo = dyn_cast<BinaryOperator>(initE)) {
        llvm::APSInt av, bv;
        if ((bo->getOpcode() == BO_Mul || bo->getOpcode() == BO_Add) &&
            knownConst(bo->getLHS(), av) && knownConst(bo->getRHS(), bv) &&
            !av.isNegative() && !bv.isNegative()) {
          unsigned w = ctx_.getIntWidth(bo->getType());
          unsigned long long max =
              w >= 64 ? (unsigned long long)-1 : ((1ULL << w) - 1);
          unsigned long long a = av.getZExtValue(), b = bv.getZExtValue();
          bool over = (bo->getOpcode() == BO_Mul)
                          ? (a != 0 && b > max / a)
                          : (a > max - b);
          if (over)
            overflowingConstVars_.insert(vd->getNameAsString());
        }
      }
    }
    // `int *end = arr + 4;` yields a pointer that is one past the array's
    // last element; dereferencing it is undefined even though forming it is ok.
    if (vd->getInit() && isOnePastEndExpr(vd->getInit()))
      onePastEnd_.insert(vd);
    // `T *b = &owner;` records a borrow for lifetime/conflict checks.
    if (vd->getType()->isPointerType() && vd->getInit()) {
      const Expr *init = vd->getInit()->IgnoreParenCasts();
      if (const auto *uo = dyn_cast<UnaryOperator>(init)) {
        if (uo->getOpcode() == UO_AddrOf) {
          std::string owner = ownerOfAddress(uo->getSubExpr());
          if (!owner.empty()) {
            // Multiple aliases are valid C.  Track the owner only for
            // lifetime invalidation; do not impose Rust-style exclusivity.
            borrowOwner_[vd->getNameAsString()] = owner;
            aliasLocs_[vd->getNameAsString()] = vd->getBeginLoc();
            if (releasedVars_.count(owner))
              releasedVars_.insert(vd->getNameAsString());
          }
        }
      }
      if (const auto *call = dyn_cast<CallExpr>(init)) {
        if (call->getDirectCallee()) {
          std::string callee = call->getDirectCallee()->getNameAsString();
          if (callee == "malloc" || callee == "calloc" || callee == "realloc" ||
              callee.rfind("cplus_new", 0) == 0)
            allocLocs_[vd->getNameAsString()] = vd->getBeginLoc();
        }
      }
      if (isa<StringLiteral>(init->IgnoreParenCasts())) {
        stringPtrs_.insert(vd);
      }
      if (const auto *cle = dyn_cast<CompoundLiteralExpr>(
              init->IgnoreParenCasts()))
        if (const auto *cat = ctx_.getAsConstantArrayType(cle->getType()))
          compoundLiteralVars_[vd] = cat->getSize().getZExtValue();
      if (const auto *call = dyn_cast<CallExpr>(init)) {
        if (call->getDirectCallee() &&
            functionsReturningFreed_.count(
                call->getDirectCallee()->getNameAsString()))
          releasedVars_.insert(vd->getNameAsString());
      }
    }
    if (!vd->getType()->isPointerType())
      return true;
    if (isNullConstant(vd->getInit())) {
      nullVars_.insert(vd->getNameAsString());
      return true;
    }
    // A call to a function whose every return is a null constant also yields
    // a provably null pointer. Guarded dereferences such as `if (p) ... *p`
    // stay accepted: TraverseIfStmt suppresses the null state for that branch.
    if (const Expr *init = vd->getInit()) {
      if (const auto *call = dyn_cast<CallExpr>(init->IgnoreParenCasts())) {
        if (call->getDirectCallee() &&
            nullReturningFunctions_.count(
                call->getDirectCallee()->getNameAsString())) {
          nullVars_.insert(vd->getNameAsString());
          return true;
        }
      }
    }
    // `T *q = p;` creates a simple alias and inherits the release state.
    std::string initName = varNameOf(vd->getInit());
    if (!initName.empty() && conditionalDepth_ == 0) {
      aliasLocs_[vd->getNameAsString()] = vd->getBeginLoc();
      auto &group = aliasGroups_[vd->getNameAsString()];
      auto other = aliasGroups_[initName];
      group.insert(vd->getNameAsString());
      group.insert(initName);
      group.insert(other.begin(), other.end());
      for (const auto &alias : group)
        aliasGroups_[alias] = group;
      if (releasedVars_.count(initName))
        releasedVars_.insert(vd->getNameAsString());
    }
    return true;
  }

  bool TraverseBinaryOperator(BinaryOperator *op) {
    if (!report_ ||
        (op->getOpcode() != BO_LAnd && op->getOpcode() != BO_LOr))
      return RecursiveASTVisitor::TraverseBinaryOperator(op);
    // `p && *p` and `!p || *p` short-circuit: the deref is reachable only
    // when p is non-null, so suppress the null fact in that operand.
    std::string condVar = varNameOf(op->getLHS());
    if (const auto *uo = dyn_cast<UnaryOperator>(
            op->getLHS()->IgnoreParenImpCasts()))
      if (uo->getOpcode() == UO_LNot)
        condVar = varNameOf(uo->getSubExpr());
    std::string saved = nonNullBranch_;
    bool removed = !condVar.empty() && nullVars_.erase(condVar) > 0;
    if (removed)
      nonNullBranch_ = condVar;
    bool r = TraverseStmt(op->getLHS());
    r &= TraverseStmt(op->getRHS());
    nonNullBranch_ = saved;
    if (removed)
      nullVars_.insert(condVar);
    return r;
  }

  bool TraverseConditionalOperator(ConditionalOperator *op) {
    if (!report_)
      return RecursiveASTVisitor::TraverseConditionalOperator(op);
    // `p ? *p : 0` tests p for non-null. Suppress the proven-null fact in the
    // true arm, then restore it for the false arm and the following code.
    std::string condVar = varNameOf(op->getCond());
    std::string saved = nonNullBranch_;
    bool removed = !condVar.empty() && nullVars_.erase(condVar) > 0;
    // A null test also means the release fact must not apply in the true arm
    // (a released pointer is often reset to null and then guarded).
    bool wasReleased = !condVar.empty() && releasedVars_.erase(condVar) > 0;
    if (removed || wasReleased)
      nonNullBranch_ = condVar;
    bool r = TraverseStmt(op->getCond());
    r &= TraverseStmt(op->getTrueExpr());
    nonNullBranch_.clear();
    r &= TraverseStmt(op->getFalseExpr());
    nonNullBranch_ = saved;
    if (removed)
      nullVars_.insert(condVar);
    if (wasReleased)
      releasedVars_.insert(condVar);
    return r;
  }

  bool VisitBinaryOperator(BinaryOperator *op) {
    if (!report_) {
      if (op->isAssignmentOp()) {
        assignedVars_.insert(varNameOf(op->getLHS()));
        // Chained field assignment `a[0].p = a[1].p = malloc(...)`: both
        // field lvalues alias the same allocation. Record the group so
        // releasing one invalidates the other.
        std::string lk = lvalueKey(op->getLHS());
        const Expr *rhs = op->getRHS()->IgnoreParenImpCasts();
        if (!lk.empty())
          if (const auto *bo = dyn_cast<BinaryOperator>(rhs))
            if (bo->isAssignmentOp()) {
              std::string rk = lvalueKey(bo->getLHS());
              if (!rk.empty() && rk != lk) {
                fieldAliases_[lk].insert(rk);
                fieldAliases_[rk].insert(lk);
              }
            }
      }
      return true;
    }
    // Compile-time arithmetic safety (STRICT fails closed; DEFAULT warns).
    // This is C+ analysis, not UBSan instrumentation.
    if (op->getType()->isIntegerType()) {
      const auto opc = op->getOpcode();
      auto asLL = [&](const Expr *e, long long &out) -> bool {
        llvm::APSInt v;
        if (!knownConst(e, v))
          return false;
        // For signed expressions interpret the stored bit pattern as signed
        // even when Clang folded it into an unsigned APSInt.
        out = op->getType()->isSignedIntegerType()
                  ? v.getSExtValue()
                  : (v.isNegative() ? v.getSExtValue() : v.getZExtValue());
        return true;
      };
      if (op->getType()->isSignedIntegerType() &&
          (opc == BO_Add || opc == BO_Sub || opc == BO_Mul)) {
        long long a = 0, b = 0;
        if (asLL(op->getLHS(), a) && asLL(op->getRHS(), b)) {
          bool overflow = false;
          if (arithmeticOverflow(op, a, b, overflow) && overflow) {
            std::string msg = "signed integer overflow in ";
            msg += (opc == BO_Add ? "addition" :
                    opc == BO_Sub ? "subtraction" : "multiplication");
            if (strict_) report(op->getBeginLoc(), msg);
            else warnAt(op->getBeginLoc(), msg);
          }
        }
      }
      if (opc == BO_Div || opc == BO_Rem) {
        llvm::APSInt dv;
        bool known = knownConst(op->getRHS(), dv);
        if (known && dv.isZero()) {
          std::string msg = opc == BO_Div ? "division by zero"
                                          : "remainder by zero";
          if (strict_) report(op->getBeginLoc(), msg);
          else warnAt(op->getBeginLoc(), msg);
        } else if (!known && !provablyNonZero(op->getRHS())) {
          std::string msg = opc == BO_Div
                                ? "division by zero cannot be excluded"
                                : "remainder by zero cannot be excluded";
          if (strict_) report(op->getBeginLoc(), msg);
          else warnAt(op->getBeginLoc(), msg);
        }
      }
    }
    // `p = reallocTemp` rebinds only the owner name; stale aliases stay dead.
    if (!reallocRebindOwner_.empty() && op->isAssignmentOp() &&
        varNameOf(op->getLHS()) == reallocRebindOwner_ &&
        varNameOf(op->getRHS()) == reallocRebindTemp_)
      releasedVars_.erase(reallocRebindOwner_);
    // An unsigned value compared with zero via < is always false: the check
    // can never protect anything, so it is a provable logic bug.
    // Subtracting or relationally comparing pointers into different objects
    // (heap allocations or distinct arrays) is undefined behaviour.
    auto objectDeclOf = [](const Expr *e) -> const VarDecl * {
      if (const auto *ref = dyn_cast_or_null<DeclRefExpr>(
              e ? e->IgnoreParenImpCasts() : nullptr))
        return dyn_cast<VarDecl>(ref->getDecl());
      return nullptr;
    };
    const VarDecl *ld = objectDeclOf(op->getLHS());
    const VarDecl *rd = objectDeclOf(op->getRHS());
    bool relational = op->isRelationalOp() && op->getLHS()->getType()->isPointerType();
    bool subtract = op->getOpcode() == BO_Sub &&
                    op->getType()->isIntegerType() &&
                    op->getLHS()->getType()->isPointerType();
    if ((relational || subtract) && ld && rd && ld != rd) {
      auto isObject = [&](const VarDecl *vd) {
        if (!vd)
          return false;
        if (ctx_.getAsConstantArrayType(vd->getType()))
          return true;
        auto it = heapAllocs_.find(vd->getNameAsString());
        return it != heapAllocs_.end() && it->second.decl == vd;
      };
      if (isObject(ld) && isObject(rd))
        report(op->getBeginLoc(),
               std::string(relational ? "relational comparison" :
                                        "pointer subtraction") +
                   " of pointers to distinct objects '" + ld->getNameAsString() +
                   "' and '" + rd->getNameAsString() + "' is undefined");
    }
    if ((op->getOpcode() == BO_LT && isNullConstant(op->getRHS()) &&
         !op->getLHS()->getType().isNull() &&
         op->getLHS()->getType()->isUnsignedIntegerType()) ||
        (op->getOpcode() == BO_GT && isNullConstant(op->getLHS()) &&
         !op->getRHS()->getType().isNull() &&
         op->getRHS()->getType()->isUnsignedIntegerType()))
      report(op->getBeginLoc(),
             "comparison of unsigned value with 0 via '<' is always false");
    if (op->isAssignmentOp()) {
      std::string lhsName = varNameOf(op->getLHS());
      assignedVars_.insert(lhsName);
      // A fresh value (allocation or null) makes may-release state stale.
      const Expr *rhs = op->getRHS()->IgnoreParenImpCasts();
      if (isNullConstant(rhs) || isa<CallExpr>(rhs))
        maybeReleasedVars_.erase(lhsName);

      // Writing through a string-literal pointer is never valid.
      checkStringLiteralWrite(op->getLHS(), op->getBeginLoc());

      const Expr *lhs = op->getLHS()->IgnoreParenImpCasts();
      // A definite write through a heap pointer proves its memory
      // initialised before any later read of the same pointer.
      if (const auto *uo = dyn_cast<UnaryOperator>(lhs)) {
        if (uo->getOpcode() == UO_Deref) {
          const Expr *inner = uo->getSubExpr()->IgnoreParenImpCasts();
          if (const auto *ref = dyn_cast<DeclRefExpr>(inner)) {
            if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl())) {
              if (heapStateOf(ref))
                heapWritten_.insert(vd);
            }
          }
        }
      } else if (const auto *ase = dyn_cast<ArraySubscriptExpr>(lhs)) {
        const Expr *base = ase->getBase()->IgnoreParenImpCasts();
        if (const auto *ref = dyn_cast<DeclRefExpr>(base)) {
          if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl())) {
            if (heapStateOf(ref))
              heapWritten_.insert(vd);
          }
        }
      }
      if (const auto *ref = dyn_cast<DeclRefExpr>(lhs)) {
        if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl())) {
          // Reassignment hands the pointer an extent we cannot prove.
          auto hit = heapAllocs_.find(vd->getNameAsString());
          if (hit != heapAllocs_.end() && hit->second.decl == vd)
            heapAllocs_.erase(hit);
          if (isOnePastEndExpr(op->getRHS()))
            onePastEnd_.insert(vd);
          else
            onePastEnd_.erase(vd); // e.g. `p = a + n; p--;` becomes in-bounds
        }
      }
    }
    if (op->isAdditiveOp()) {
      const Expr *lhs = op->getLHS();
      const Expr *rhs = op->getRHS();
      const VarDecl *arrayVar = nullptr;
      const Expr *indexExpr = nullptr;
      if (const auto *ref = dyn_cast<DeclRefExpr>(lhs->IgnoreParenImpCasts()))
        if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl()))
          if (vd->getType()->isArrayType()) {
            arrayVar = vd;
            indexExpr = rhs;
          }
      if (arrayVar && indexExpr) {
        Expr::EvalResult r;
        if (indexExpr->EvaluateAsInt(r, ctx_) && r.Val.isInt()) {
          long long offset = r.Val.getInt().getSExtValue();
          const auto *at = ctx_.getAsConstantArrayType(arrayVar->getType());
          if (at) {
            long long extent =
                static_cast<long long>(at->getSize().getZExtValue());
            if ((op->getOpcode() == BO_Add && offset > extent) ||
                (op->getOpcode() == BO_Sub && offset > 0))
              report(op->getBeginLoc(), "pointer arithmetic outside array '" +
                                            arrayVar->getNameAsString() + "'");
          }
        }
      }
    }
    // A comparison against NULL means the code is explicitly checking the
    // pointer; do not report later dereferences of it as provably null.
    if (op->isEqualityOp()) {
      const Expr *lhs = op->getLHS();
      const Expr *rhs = op->getRHS();
      if (isNullConstant(rhs))
        if (const VarDecl *vd = nullVarOf(lhs))
          nullVars_.erase(vd->getNameAsString());
      if (isNullConstant(lhs))
        if (const VarDecl *vd = nullVarOf(rhs))
          nullVars_.erase(vd->getNameAsString());
      return true;
    }
    if (op->isAssignmentOp()) {
      std::string lhsName = varNameOf(op->getLHS());
      std::string rhsName = varNameOf(op->getRHS());
      if (!lhsName.empty() && !rhsName.empty() && lhsName != rhsName &&
          conditionalDepth_ == 0) {
        // q = p: simple pointer alias; merge their obligation groups.
        auto &lgroup = aliasGroups_[lhsName];
        auto rgroup = aliasGroups_[rhsName];
        lgroup.insert(lhsName);
        lgroup.insert(rgroup.begin(), rgroup.end());
        lgroup.insert(rhsName);
        for (const auto &alias : lgroup)
          aliasGroups_[alias] = lgroup;
      }
      const VarDecl *lhsVar = nullptr;
      if (const auto *ref = dyn_cast<DeclRefExpr>(op->getLHS()))
        lhsVar = dyn_cast<VarDecl>(ref->getDecl());
      if (isNullConstant(op->getRHS())) {
        // `p = NULL` (including casts such as `(int *)NULL`) makes p provably
        // null until it is reassigned or guarded.
        if (lhsVar && lhsVar->getType()->isPointerType())
          nullVars_.insert(lhsVar->getNameAsString());
      } else if (lhsVar) {
        nullVars_.erase(lhsVar->getNameAsString());
      }
      const Expr *rhsExpr = op->getRHS()->IgnoreParenImpCasts();
      if (isNullConstant(rhsExpr) ||
          (isa<UnaryOperator>(rhsExpr) &&
           cast<UnaryOperator>(rhsExpr)->getOpcode() == UO_AddrOf)) {
        // Resetting a released pointer to null or to a fresh object address
        // makes it live again.
        const Expr *lhsExpr = op->getLHS()->IgnoreParenImpCasts();
        clearReleased(varNameOf(lhsExpr));
        releasedFields_.erase(varNameOf(lhsExpr));
        maybeReleasedVars_.erase(varNameOf(lhsExpr));
      }
      if (handlers_.count(currentFunction_))
        reportGlobalWrite(op->getLHS(), op->getBeginLoc());

    }
    return true;
  }

  bool VisitUnaryOperator(UnaryOperator *op) {
    if (!report_)
      return true;
    if (op->isIncrementOp() || op->isDecrementOp()) {
      const Expr *se = op->getSubExpr()->IgnoreParenImpCasts();
      if (const auto *ref = dyn_cast<DeclRefExpr>(se)) {
        if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl())) {
          // Advancing a heap pointer makes it an interior pointer; freeing
          // it later is invalid.
          if (heapStateOf(ref))
            heapInterior_.insert(vd);
          // Decrementing a one-past pointer brings it back in bounds.
          if (op->isDecrementOp())
            onePastEnd_.erase(vd);
        }
      }
      // `*p++` / `(*p)++` writes the pointed-to memory.
      if (const auto *uo = dyn_cast<UnaryOperator>(se)) {
        if (uo->getOpcode() == UO_Deref) {
          const Expr *inner = uo->getSubExpr()->IgnoreParenImpCasts();
          if (const auto *ref = dyn_cast<DeclRefExpr>(inner)) {
            if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl())) {
              if (heapStateOf(ref))
                heapWritten_.insert(vd);
            }
          }
        }
      }
    }
    if (op->getOpcode() == UO_AddrOf) {
      // Address taken: a libc out-parameter may write a non-null value.
      const Expr *sub = op->getSubExpr()->IgnoreParenImpCasts();
      if (const auto *ref = dyn_cast<DeclRefExpr>(sub))
        if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl()))
          nullVars_.erase(vd->getNameAsString());
      return true;
    }
    if (op->getOpcode() == UO_Deref) {
      const Expr *sub = op->getSubExpr();
      if (isReleased(sub))
        reportUaf(op->getBeginLoc(), varNameOf(sub),
                  "dereference of released pointer '" +
                      (varNameOf(sub).empty() ? lvalueKey(sub) : varNameOf(sub)) +
                      "'");
      else if (isMaybeReleased(sub, op->getBeginLoc()) && conditionalDepth_ == 0)
        warnAt(op->getBeginLoc(),
               "possible use-after-free: '" + varNameOf(sub) +
                   "' may have been released on a branch path");
      else if (movedVars_.count(varNameOf(sub)))
        report(op->getBeginLoc(), "use-after-move: dereference of moved resource");
      else if ((isNullConstant(sub) || nullVarOf(sub)) &&
               varNameOf(sub) != nonNullBranch_)
        report(op->getBeginLoc(), "null pointer dereference");
      checkHeapDeref(op);
    }
    if (handlers_.count(currentFunction_) &&
        (op->isIncrementOp() || op->isDecrementOp()))
      reportGlobalWrite(op->getSubExpr(), op->getBeginLoc());
    return true;
  }

  bool VisitMemberExpr(MemberExpr *member) {
    if (!report_)
      return true;
    if (!member->isArrow()) {
      // Direct member access on a moved resource is also use-after-move.
      if (movedVars_.count(varNameOf(member->getBase())))
        report(member->getBeginLoc(),
               "use-after-move: member access through moved resource");
      return true;
    }
    if (isReleased(member->getBase()))
      reportUaf(member->getBeginLoc(), ownerOfAddress(member->getBase()),
                "member access through released pointer");
    else if (movedVars_.count(varNameOf(member->getBase())))
      report(member->getBeginLoc(), "use-after-move: member access through moved resource");
    else if (nullVarOf(member->getBase()))
      report(member->getBeginLoc(), "null pointer dereference");
    return true;
  }

  void reportGlobalWrite(Expr *lhs, SourceLocation loc) {
    const Expr *e = lhs->IgnoreParenImpCasts();
    if (const auto *ref = dyn_cast<DeclRefExpr>(e))
      if (nonVolatileGlobals_.count(ref->getDecl()->getNameAsString()))
        report(loc, "signal-modified global must be volatile sig_atomic_t");
  }

  void warnAt(SourceLocation loc, const std::string &message) {
    PresumedLoc ploc = ctx_.getSourceManager().getPresumedLoc(loc);
    Diag d;
    // DEFAULT mode warns about unverified operations; STRICT mode fails
    // closed by promoting them to errors (no silent warning downgrade).
    d.level = strict_ ? DiagLevel::Error : DiagLevel::Warning;
    d.message = "[" + diagnosticCategory(message) + "] " + message;
    d.file = filename_;
    if (ploc.isValid()) {
      d.line = ploc.getLine();
      d.column = ploc.getColumn();
      std::string diagFile = ploc.getFilename();
      // Tooling analyses a synthetic "<file>.c" buffer; map it back.
      if (diagFile == filename_ + ".c")
        diagFile = filename_;
      if (diagFile != filename_)
        d.file = diagFile;
    }
    diags_.report(std::move(d));
  }

  bool isIdentExpr(const Expr *expr, const VarDecl *vd) const {
    if (!expr || !vd)
      return false;
    const Expr *e = expr->IgnoreParenImpCasts();
    const auto *ref = dyn_cast<DeclRefExpr>(e);
    return ref && ref->getDecl() == vd;
  }

  /// For memcpy/memmove with a direct array destination, reject a constant
  /// byte count that exceeds the destination extent.  Unknown extents are not
  /// guessed.
  void checkBoundedWrite(const CallExpr *call, unsigned dstIdx,
                         unsigned sizeIdx, const std::string &op) {
    if (call->getNumArgs() <= std::max(dstIdx, sizeIdx))
      return;
    const Expr *dst = call->getArg(dstIdx)->IgnoreParenImpCasts();
    const Expr *n = call->getArg(sizeIdx)->IgnoreParenImpCasts();
    llvm::APSInt amount;
    if (!knownConst(n, amount))
      return;
    long long bytes = amount.isNegative() ? amount.getSExtValue()
                                          : (long long)amount.getZExtValue();
    if (bytes <= 0)
      return;
    const auto *ref = dyn_cast<DeclRefExpr>(dst);
    if (!ref)
      return;
    const auto *vd = dyn_cast<VarDecl>(ref->getDecl());
    if (!vd)
      return;
    // Heap allocation with a known byte extent.
    if (const HeapAllocState *hs = heapStateOf(dst)) {
      if ((unsigned long long)bytes > (unsigned long long)hs->bytes)
        report(call->getBeginLoc(), op + " of " + std::to_string(bytes) +
                                        " bytes exceeds heap allocation '" +
                                        vd->getNameAsString() + "' of " +
                                        std::to_string(hs->bytes) + " bytes");
      return;
    }
    const auto *at = ctx_.getAsConstantArrayType(vd->getType());
    if (!at)
      return;
    QualType elem = at->getElementType();
    if (!elem->isIntegerType() && !elem->isCharType())
      return;
    unsigned long long elemSize = ctx_.getTypeSize(elem) / 8u;
    unsigned long long capacity =
        at->getSize().getZExtValue() * elemSize;
    if ((unsigned long long)bytes > capacity)
      report(call->getBeginLoc(), op + " of " + std::to_string(bytes) +
                                      " bytes exceeds destination '" +
                                      vd->getNameAsString() + "'");
  }

  void checkBoundedCopy(const CallExpr *call,
                        const std::string &op = "copy") {
    checkBoundedWrite(call, 0, 2, op);
  }

  /// strcat(dst, src): reject when the concatenation provably overflows a
  /// fixed-size destination with a constant string initializer.
  void checkStringConcat(const CallExpr *call) {
    if (call->getNumArgs() < 2)
      return;
    const Expr *dst = call->getArg(0)->IgnoreParenImpCasts();
    const auto *ref = dyn_cast<DeclRefExpr>(dst);
    const auto *vd = ref ? dyn_cast<VarDecl>(ref->getDecl()) : nullptr;
    if (!vd || !vd->getInit())
      return;
    const auto *at = ctx_.getAsConstantArrayType(vd->getType());
    if (!at || !at->getElementType()->isCharType())
      return;
    const auto *initStr = dyn_cast<StringLiteral>(
        vd->getInit()->IgnoreParenCasts());
    const auto *srcStr = dyn_cast<StringLiteral>(
        call->getArg(1)->IgnoreParenCasts());
    if (!initStr || !srcStr)
      return;
    unsigned long long capacity = at->getSize().getZExtValue();
    unsigned long long needed =
        initStr->getLength() + srcStr->getLength() + 1;
    if (needed > capacity)
      report(call->getBeginLoc(),
             "string concatenation needs " + std::to_string(needed) +
                 " bytes but destination '" + vd->getNameAsString() +
                 "' has capacity " + std::to_string(capacity));
  }

  /// Null pointers passed to libc read/write APIs are contract violations.
  void checkLibcNullSource(const CallExpr *call, const std::string &api,
                           unsigned idx = 1) {
    if (call->getNumArgs() <= idx)
      return;
    const Expr *src = call->getArg(idx)->IgnoreParenCasts();
    if (isNullConstant(src) || nullVarOf(src))
      report(call->getBeginLoc(), "null pointer argument " +
                                      std::to_string(idx) + " passed to " +
                                      api);
  }

  /// Detect strcpy from a short string literal into a smaller array.  Source
  /// lengths that are not compile-time constants are unknown.
  void checkStringCopy(const CallExpr *call) {
    const Expr *dst = call->getArg(0)->IgnoreParenImpCasts();
    const Expr *src = call->getArg(1)->IgnoreParenImpCasts();
    const auto *dstRef = dyn_cast<DeclRefExpr>(dst);
    if (!dstRef)
      return;
    const auto *str = dyn_cast<StringLiteral>(src);
    if (!str) {
      // `const char *s = "literal"; strcpy(d, s);`
      if (const auto *sref = dyn_cast<DeclRefExpr>(src))
        if (const auto *svd = dyn_cast<VarDecl>(sref->getDecl()))
          if (svd->getInit())
            str = dyn_cast<StringLiteral>(svd->getInit()->IgnoreParenCasts());
    }
    if (!str)
      return;
    const auto *vd = dyn_cast<VarDecl>(dstRef->getDecl());
    if (!vd)
      return;
    unsigned long long cap = 0;
    if (const auto *at = ctx_.getAsConstantArrayType(vd->getType()))
      cap = at->getSize().getZExtValue();
    else if (const HeapAllocState *hs = heapStateOf(dst))
      cap = static_cast<unsigned long long>(hs->bytes);
    if (!cap)
      return;
    unsigned long long need = str->getLength() + 1;
    if (need > cap)
      report(call->getBeginLoc(), "string copy of " + std::to_string(need - 1) +
                                      " chars plus NUL exceeds destination '" +
                                      vd->getNameAsString() + "'");
  }

  /// strcat into a heap buffer: current content + src + NUL must fit.
  void checkStringConcatHeap(const CallExpr *call) {
    const Expr *dst = call->getArg(0)->IgnoreParenImpCasts();
    const auto *dstRef = dyn_cast<DeclRefExpr>(dst);
    const auto *str = dyn_cast<StringLiteral>(call->getArg(1)->IgnoreParenCasts());
    const HeapAllocState *hs = heapStateOf(dst);
    if (!dstRef || !str || !hs)
      return;
    const auto *vd = dyn_cast<VarDecl>(dstRef->getDecl());
    if (!vd)
      return;
    unsigned long long used = heapWritten_.count(vd) ? 1 : 1; // d[0]=0 idiom
    unsigned long long need = used + str->getLength();
    if (need > static_cast<unsigned long long>(hs->bytes))
      report(call->getBeginLoc(),
             "string concatenation needs " + std::to_string(need) +
                 " bytes but destination '" + vd->getNameAsString() +
                 "' has capacity " + std::to_string(hs->bytes));
  }

  /// memcpy/memmove source must have at least the requested bytes.
  void checkSourceExtent(const CallExpr *call, unsigned srcIdx,
                         unsigned sizeIdx) {
    if (call->getNumArgs() <= std::max(srcIdx, sizeIdx))
      return;
    long long bytes = 0;
    const Expr *n = call->getArg(sizeIdx)->IgnoreParenImpCasts();
    Expr::EvalResult r;
    if (n->EvaluateAsInt(r, ctx_) && r.Val.isInt())
      bytes = r.Val.getInt().getSExtValue();
    else
      return;
    if (bytes <= 0)
      return;
    const Expr *src = call->getArg(srcIdx)->IgnoreParenImpCasts();
    const auto *at = ctx_.getAsConstantArrayType(src->getType());
    if (!at)
      return;
    QualType elem = at->getElementType();
    unsigned long long elemSize = ctx_.getTypeSize(elem) / 8u;
    if (elemSize == 0)
      elemSize = 1;
    unsigned long long cap = at->getSize().getZExtValue() * elemSize;
    if (static_cast<unsigned long long>(bytes) > cap)
      report(call->getBeginLoc(), "copy of " + std::to_string(bytes) +
                                      " bytes exceeds source of length " +
                                      std::to_string(cap));
  }

  bool VisitCallExpr(CallExpr *call) {
    const FunctionDecl *callee = call->getDirectCallee();
    std::string rawName = callee ? callee->getNameAsString() : std::string();
    std::string name = normalizeFortifyName(rawName);
    // FORTIFY builtins insert extra object-size/flag arguments before the
    // format string.
    unsigned fmtShift = 0;
    if (rawName.rfind("__builtin___", 0) == 0) {
      if (rawName.find("nprintf_chk") != std::string::npos) // sn/vsnprintf
        fmtShift = 2;
      else if (rawName.find("printf_chk") != std::string::npos) // printf
        fmtShift = 1;
    }
    if (!callee) {
      const Expr *calleeExpr = call->getCallee()->IgnoreParenImpCasts();
      if (const auto *ref = dyn_cast<DeclRefExpr>(calleeExpr)) {
        callee = dyn_cast<FunctionDecl>(ref->getDecl());
        if (!callee)
          if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl())) {
            auto it = formatFnAliases_.find(vd->getNameAsString());
            if (it != formatFnAliases_.end()) {
              name = it->second;
              callee = nullptr; // indirect call through a function-pointer alias
            }
          }
      }
      if (!callee && name.empty())
        return true;
    }

    // Format-string safety (CWE-134): the format argument must be a string
    // literal. This applies to all printf/scanf families including C+'s _s
    // extensions, because an _s suffix does not sanitise the format itself.
    if (report_) {
      static const std::set<std::string> kPrintfLike = {
          "printf", "fprintf", "sprintf", "snprintf", "vprintf", "vfprintf",
          "vsprintf", "vsnprintf", "printf_s", "fprintf_s", "sprintf_s",
          "snprintf_s", "cplus_printf_s", "cplus_fprintf_s",
          "cplus_snprintf_s", "cplus_sprintf_s"};
      static const std::set<std::string> kScanfLike = {
          "scanf", "fscanf", "sscanf", "scanf_s", "fscanf_s", "sscanf_s"};
      bool isPrintf = kPrintfLike.count(name) != 0;
      bool isScanf = kScanfLike.count(name) != 0;
      if (isPrintf || isScanf) {
        unsigned fmtIdx = 0;
        // fprintf(stream, fmt, ...), sprintf(dst, fmt, ...),
        // snprintf(dst, size, fmt, ...) place the format after the stream or
        // destination (and capacity).
        if (name == "fprintf" || name == "fprintf_s" ||
            name == "cplus_fprintf_s" || name == "sprintf" ||
            name == "sprintf_s" || name == "cplus_sprintf_s")
          fmtIdx = 1;
        else if (name == "snprintf" || name == "snprintf_s" ||
                 name == "cplus_snprintf_s")
          fmtIdx = 2;
        // scanf(fmt, ...), fscanf(stream, fmt, ...), sscanf(src, fmt, ...):
        // scanf has format at 0; fscanf/sscanf have it at 1.
        if (name == "fscanf" || name == "fscanf_s" || name == "sscanf" ||
            name == "sscanf_s")
          fmtIdx = 1;
        fmtIdx += fmtShift;
        if (call->getNumArgs() > fmtIdx) {
          const Expr *fmt = call->getArg(fmtIdx)->IgnoreParenCasts();
          if (!isa<StringLiteral>(fmt)) {
            std::string msg = "format string in " + name +
                              " is not a string literal (CWE-134; attacker-"
                              "controlled format)";
            if (strict_)
              report(call->getBeginLoc(), msg);
            else
              warnAt(call->getBeginLoc(), msg);
          } else if (const auto *lit = dyn_cast<StringLiteral>(fmt)) {
            // %n writes through a vararg pointer with no way for the compiler
            // to prove the target is valid; it is banned in safe C+.
            if (isPrintf && lit->getBytes().contains("%n")) {
              std::string msg = "'%n' in " + name +
                                " performs an unverifiable write through a "
                                "variadic argument";
              if (strict_) report(call->getBeginLoc(), msg);
              else warnAt(call->getBeginLoc(), msg);
            }
            if (isScanf && hasUnboundedStringConversion(lit))
              for (unsigned a = fmtIdx + 1; a < call->getNumArgs(); ++a) {
                const Expr *arg = call->getArg(a)->IgnoreParenCasts();
                const auto *ref = dyn_cast<DeclRefExpr>(arg);
                const auto *vd = ref ? dyn_cast<VarDecl>(ref->getDecl())
                                     : nullptr;
                if (vd && vd->getType()->isArrayType()) {
                  std::string msg = "unbounded '%s' in " + name +
                                    " writes into fixed-size array '" +
                                    vd->getNameAsString() +
                                    "'; add a field width";
                  if (strict_) report(call->getBeginLoc(), msg);
                  else warnAt(call->getBeginLoc(), msg);
                }
              }
            if (isPrintf) {
              // A fixed char array whose initializer has no NUL byte cannot be
              // passed to %s without an over-read.
              for (unsigned a = fmtIdx + 1; a < call->getNumArgs(); ++a) {
                const Expr *arg = call->getArg(a)->IgnoreParenCasts();
                const auto *ref = dyn_cast<DeclRefExpr>(arg);
                const auto *vd = ref ? dyn_cast<VarDecl>(ref->getDecl()) : nullptr;
                if (!vd || !vd->getInit())
                  continue;
                const auto *cat = ctx_.getAsConstantArrayType(vd->getType());
                if (!cat || !cat->getElementType()->isCharType())
                  continue;
                bool terminated = false;
                if (const auto *il = dyn_cast<InitListExpr>(
                        vd->getInit()->IgnoreParenImpCasts())) {
                  for (const Expr *el : il->inits()) {
                    Expr::EvalResult er;
                    if (el->EvaluateAsInt(er, ctx_) && er.Val.isInt() &&
                        er.Val.getInt() == 0)
                      terminated = true;
                  }
                } else if (const auto *sl = dyn_cast<StringLiteral>(
                               vd->getInit()->IgnoreParenImpCasts())) {
                  terminated = sl->getLength() < cat->getSize().getZExtValue();
                }
                if (!terminated)
                  report(call->getBeginLoc(),
                         "char array '" + vd->getNameAsString() +
                             "' has no NUL terminator; %s will over-read");
              }
            }
          }
        }
      }
    }

    if (!report_ && callee) {
      // Record allocation extents passed as arguments so the callee can
      // bounds-check its parameter accesses.
      for (unsigned i = 0; i < call->getNumArgs(); ++i) {
        std::string an = varNameOf(call->getArg(i));
        auto hit = heapAllocs_.find(an);
        if (!an.empty() && hit != heapAllocs_.end())
          callParamExtents_[name][i] = hit->second.bytes;
      }
    }

    if (name == "signal" && call->getNumArgs() >= 2) {
      const Expr *arg = call->getArg(1)->IgnoreParenImpCasts();
      if (const auto *ref = dyn_cast<DeclRefExpr>(arg))
        if (const auto *fn = dyn_cast<FunctionDecl>(ref->getDecl()))
          handlers_.insert(fn->getNameAsString());
      return true;
    }

    if (report_ && handlers_.count(currentFunction_)) {
      static const std::set<std::string> unsafe = {
          "printf", "fprintf", "sprintf", "puts",  "malloc",
          "calloc", "realloc", "free",   "fopen", "fclose"};
      if (unsafe.count(name))
        report(call->getBeginLoc(), "'" + name +
                                        "' is not async-signal-safe");
    }
    // Compiler contracts for ordinary libc (issue #19 / pillar 1).  Only
    // sound, provable patterns are diagnosed; unknown cases are left alone.
    // Clang FORTIFY rewrites memcpy/strcpy to *_chk builtins with an extra
    // object-size argument; treat those as the original calls.
    std::string baseName = name;
    if (!report_ && name.size() > 5 &&
        name.compare(name.size() - 5, 5, "_drop") == 0 &&
        call->getNumArgs() >= 1) {
      destructors_.insert(name);
    }
    if (report_) {
      // A contract we cannot fully interpret must never be treated as proof
      // of safety. STRICT rejects the call; DEFAULT warns.
      for (const auto &issue : contractIssues()) {
        if (!issue.function.empty() && issue.function != name)
          continue;
        std::string key = issue.function + "|" + issue.message;
        if (!contractIssuesReported_.insert(key).second)
          continue;
        std::string msg = "unsupported libc contract";
        if (!issue.function.empty())
          msg += " for '" + issue.function + "'";
        msg += ": " + issue.message;
        SourceLocation at = call->getBeginLoc();
        if (issue.function.empty())
          at = ctx_.getSourceManager().getLocForStartOfFile(
              ctx_.getSourceManager().getMainFileID());
        if (strict_)
          report(at, msg);
        else
          warnAt(at, msg);
      }
      // Conservatively treat any heap pointer passed to a call as possibly
      // written, so a later read is not claimed uninitialised on a guess.
      for (unsigned i = 0; i < call->getNumArgs(); ++i) {
        const Expr *a = call->getArg(i)->IgnoreParenCasts();
        if (const auto *ref = dyn_cast<DeclRefExpr>(a)) {
          if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl())) {
            auto hit = heapAllocs_.find(vd->getNameAsString());
            if (hit != heapAllocs_.end() && hit->second.decl == vd)
              heapWritten_.insert(vd);
          }
        }
      }
      const VarDecl *ptrVar = nullptr;
      if (call->getNumArgs() >= 1) {
        const Expr *arg = call->getArg(0)->IgnoreParenImpCasts();
        if (const auto *uo = dyn_cast<UnaryOperator>(arg))
          if (uo->getOpcode() == UO_AddrOf)
            arg = uo->getSubExpr()->IgnoreParenImpCasts();
        if (const auto *ref = dyn_cast<DeclRefExpr>(arg))
          if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl()))
            ptrVar = vd;
      }
      if (baseName == "cplus_move" && call->getNumArgs() >= 1) {
        std::string moved = varNameOf(call->getArg(0));
        if (!moved.empty()) {
          movedVars_.insert(moved);
          bool hasBorrow = borrowOwner_.count(moved) != 0;
          for (const auto &entry : borrowOwner_)
            if (entry.second == moved)
              hasBorrow = true;
          if (hasBorrow)
            report(call->getBeginLoc(), "borrow of '" + moved +
                                            "' cannot cross a move");
        }
      }
      if ((baseName == "free" || baseName == "cplus_release") && call->getNumArgs() >= 1) {
        const Expr *arg0 = call->getArg(0)->IgnoreParenImpCasts();
        if (const auto *pref = dyn_cast<DeclRefExpr>(arg0))
          if (const auto *parm = dyn_cast<ParmVarDecl>(pref->getDecl()))
            userFrees_[currentFunction_].insert(parm->getFunctionScopeIndex());
      }
      if (baseName == "cplus_check_live" && ptrVar &&
          releasedVars_.count(ptrVar->getNameAsString()))
        report(call->getBeginLoc(),
               "use-after-free: liveness check on released pointer '" +
                   ptrVar->getNameAsString() + "'");

      bool isAllocatorFree = (baseName == "free" ||
                              baseName == "cplus_release");
      if (isAllocatorFree && call->getNumArgs() >= 1) {
        checkInvalidFree(call->getBeginLoc(), call->getArg(0), baseName);
        // Exact field lvalue: record its release even when the argument is
        // not a plain variable reference (free(s.p), free(a[0].p)).
        std::string fkey = lvalueKey(call->getArg(0));
        if (!fkey.empty() && conditionalDepth_ == 0) {
          if (releasedFields_.count(fkey))
            report(call->getBeginLoc(), "double free of '" + fkey + "'");
          releasedFields_.insert(fkey);
        }
      }
      bool invalidates = isAllocatorFree ||
                         destructors_.count(name) != 0;
      if (invalidates && ptrVar) {
        std::string pname = ptrVar->getNameAsString();
        // free(NULL) / cplus_release(NULL) is well-defined and does not
        // create a dangling owner.
        if (isAllocatorFree && nullVars_.count(pname))
          return true;
        if (isAllocatorFree && releasedVars_.count(pname))
          report(call->getBeginLoc(), "double free of '" + pname + "'");
        else if (isAllocatorFree && conditionalDepth_ == 0 &&
                 maybeReleasedVars_.count(pname) &&
                 !releasedVars_.count(pname) && [&] {
                   auto lit = maybeReleaseLines_.find(pname);
                   unsigned here = call->getBeginLoc().getRawEncoding();
                   return lit != maybeReleaseLines_.end() && here &&
                          lit->second && lit->second < here;
                 }())
          report(call->getBeginLoc(),
                 "double free of '" + pname + "' on some branch path");
        else if (conditionalDepth_ == 0) {
          std::string fkey = lvalueKey(call->getArg(0));
          if (!fkey.empty())
            releasedFields_.insert(fkey);
          releaseLocs_[pname] = call->getBeginLoc();
          markReleased(pname);
        }
      }
      // Provably-null arguments to libc APIs that dereference them are
      // contract violations even when the JSON contract has no nullableArgs.
      {
        static const std::set<std::string> kDerefApis = {
            "memcpy", "memmove", "memset", "strcpy", "strncpy", "strcat",
            "strncat", "strlen", "strcmp", "strncmp", "fgets", "fread",
            "fwrite", "fprintf", "fscanf", "puts", "print"};
        if (kDerefApis.count(baseName))
          for (unsigned a = 0; a < call->getNumArgs(); ++a)
            if (call->getArg(a)->getType()->isPointerType())
              checkLibcNullSource(call, baseName, a);
      }
      // Contract-driven check: a function whose JSON contract declares a
      // nullable pointer argument must not receive a provably-null value.
      FunctionEffect contract = libcEffect(baseName);
      if (contract.nullableArg && !contract.frees)
        for (unsigned i = 0; i < call->getNumArgs(); ++i)
          if (isNullConstant(call->getArg(i)->IgnoreParenCasts()) ||
              nullVarOf(call->getArg(i))) {
            report(call->getBeginLoc(),
                   "null pointer argument " + std::to_string(i) + " passed to " +
                       baseName + " (" + contract.category + ")");
            break;
          }
      bool reallocFromNull = baseName == "realloc" && call->getNumArgs() >= 1 &&
                             isNullConstant(call->getArg(0)->IgnoreParenCasts());
      if (baseName == "realloc" && !reallocFromNull &&
          handledReallocs_.count(call->getBeginLoc().getRawEncoding()) == 0) {
        // Do NOT mark the old pointer released: on failure realloc returns
        // NULL and the original block remains owned by the caller.  Alias
        // invalidation is only valid on the success branch, which the current
        // analysis does not track; leave it unknown instead of unsoundly
        // reporting a later free as a double free.
        warnAt(call->getBeginLoc(),
               "realloc result and original pointer require explicit "
               "success/failure handling; alias state is unknown");
      }
      // Allocation sizes computed from local constants: detect size_t wrap.
      if ((baseName == "malloc" || baseName == "calloc" ||
           baseName == "realloc") && call->getNumArgs() >= 1) {
        auto apToU64 = [](const llvm::APSInt &v) -> unsigned long long {
          return v.isNegative()
                     ? static_cast<unsigned long long>(v.getSExtValue())
                     : v.getZExtValue();
        };
        std::function<bool(const Expr *, unsigned long long &, bool &)> evalU64 =
            [&](const Expr *e, unsigned long long &out, bool &overflow) -> bool {
          if (!e) return false;
          const Expr *x = e->IgnoreParenCasts();
          llvm::APSInt leaf;
          if (knownConst(x, leaf)) {
            out = apToU64(leaf);
            return true;
          }
          if (const auto *bo = dyn_cast<BinaryOperator>(x)) {
            unsigned long long a = 0, b = 0;
            if (!evalU64(bo->getLHS(), a, overflow) ||
                !evalU64(bo->getRHS(), b, overflow))
              return false;
            // Respect the *source* integer width: `uint32_t * uint32_t`
            // wraps at 32 bits before the result is widened to size_t.
            unsigned w = ctx_.getIntWidth(bo->getType());
            unsigned long long max =
                w >= 64 ? (unsigned long long)-1 : ((1ULL << w) - 1);
            if (bo->getOpcode() == BO_Mul) {
              if (a != 0 && b > max / a) {
                overflow = true;
                return true;
              }
              out = a * b;
              return true;
            }
            if (bo->getOpcode() == BO_Add) {
              if (a > max - b) {
                overflow = true;
                return true;
              }
              out = a + b;
              return true;
            }
          }
          return false;
        };
        unsigned sizeIdx = (baseName == "realloc") ? 1 : 0;
        if (sizeIdx < call->getNumArgs()) {
          unsigned long long v = 0;
          bool overflow = false;
          if (evalU64(call->getArg(sizeIdx), v, overflow))
            if (overflow)
              report(call->getBeginLoc(),
                     "allocation size wraps size_t; check the multiplication "
                     "before allocating");
        }
      }
      if ((baseName == "malloc" || baseName == "calloc" ||
           baseName == "realloc") && call->getNumArgs() >= 1) {
        unsigned sizeIdx = (baseName == "realloc") ? 1 : 0;
        std::string n = varNameOf(call->getArg(sizeIdx)->IgnoreParenCasts());
        if (overflowingConstVars_.count(n))
          report(call->getBeginLoc(),
                 "allocation size wraps size_t: '" + n +
                     "' was computed with an overflowing constant expression");
      }
      // fread/fwrite multiply size * count in size_t; a constant product that
      // wraps is a buffer-overflow / truncated-transfer bug.
      if (baseName == "fread" && call->getNumArgs() >= 1) {
        for (const auto &p : ctx_.getParents(*call)) {
          const VarDecl *resultVar = nullptr;
          if (const auto *pvd = p.get<VarDecl>())
            if (pvd->getInit() && pvd->getInit()->IgnoreParenImpCasts() == call)
              resultVar = pvd;
          if (const auto *bo = p.get<BinaryOperator>())
            if (bo->isAssignmentOp())
              if (const auto *ref = dyn_cast<DeclRefExpr>(
                      bo->getLHS()->IgnoreParenCasts()))
                resultVar = dyn_cast<VarDecl>(ref->getDecl());
          if (resultVar)
            if (const auto *dr = dyn_cast<DeclRefExpr>(
                    call->getArg(0)->IgnoreParenCasts()))
              if (const auto *dv = dyn_cast<VarDecl>(dr->getDecl()))
                freadResults_[resultVar->getNameAsString()] =
                    dv->getNameAsString();
        }
      }
      if ((baseName == "fread" || baseName == "fwrite") &&
          call->getNumArgs() >= 3) {
        auto constInt = [&](const Expr *e, unsigned long long &out) -> bool {
          Expr::EvalResult r;
          if (e->EvaluateAsInt(r, ctx_) && r.Val.isInt()) {
            out = r.Val.getInt().getZExtValue();
            return true;
          }
          if (const auto *ref = dyn_cast<DeclRefExpr>(e->IgnoreParenCasts()))
            if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl())) {
              auto it = constLocals_.find(vd->getNameAsString());
              if (it != constLocals_.end()) {
                out = it->second.getZExtValue();
                return true;
              }
            }
          return false;
        };
        unsigned long long sz = 0, cnt = 0;
        if (constInt(call->getArg(1), sz) && constInt(call->getArg(2), cnt) &&
            sz != 0 && cnt > (unsigned long long)-1 / sz)
          report(call->getBeginLoc(),
                 "size * count overflows size_t in " + baseName +
                     "; the requested transfer wraps to a small value");
      }
      // Ignoring the fgets return value leaves an uninitialized local buffer
      // readable on EOF/errors (the function returns NULL without writing).
      // Only diagnose when that buffer is actually read on the following path.
      if (baseName == "fgets" && call->getNumArgs() >= 1) {
        const CompoundStmt *parent = nullptr;
        for (const auto &p : ctx_.getParents(*call))
          if (const auto *s = p.get<Stmt>())
            parent = dyn_cast<CompoundStmt>(s);
        if (parent) {
          const Expr *dst = call->getArg(0)->IgnoreParenCasts();
          const VarDecl *destVar = nullptr;
          if (const auto *ref = dyn_cast<DeclRefExpr>(dst))
            destVar = dyn_cast<VarDecl>(ref->getDecl());
          if (destVar && !destVar->hasInit() &&
              destVar->getType()->isArrayType()) {
            auto contains = [&](const Stmt *n, const Stmt *target) {
              std::function<bool(const Stmt *)> walk = [&](const Stmt *x) {
                if (!x) return false;
                if (x == target) return true;
                for (const Stmt *c : x->children())
                  if (walk(c)) return true;
                return false;
              };
              return walk(n);
            };
            bool foundCall = false;
            for (const Stmt *sib : parent->children()) {
              if (!foundCall) {
                if (contains(sib, call)) foundCall = true;
                continue;
              }
              std::function<bool(const Stmt *)> uses = [&](const Stmt *n) {
                if (!n) return false;
                if (const auto *r = dyn_cast<DeclRefExpr>(n))
                  if (r->getDecl() == destVar) return true;
                for (const Stmt *c : n->children())
                  if (uses(c)) return true;
                return false;
              };
              if (uses(sib)) { report(call->getBeginLoc(),
                     "ignored fgets return value leaves '" +
                         destVar->getNameAsString() +
                         "' uninitialized on EOF; check the result");
                break;
              }
            }
          }
        }
      }
      // gets() has no way to bound its write; it is always unsafe.
      if (baseName == "gets") {
        std::string msg = "gets() performs an unbounded write; use fgets "
                          "with an explicit capacity";
        if (strict_) report(call->getBeginLoc(), msg);
        else warnAt(call->getBeginLoc(), msg);
      }
      // sprintf/vsprintf write without a capacity argument. STRICT rejects
      // them unless the caller supplied a provably sufficient destination.
      if ((baseName == "sprintf" || baseName == "vsprintf") &&
          call->getNumArgs() >= 1) {
        bool provablyBounded = false;
        const Expr *dst = call->getArg(0)->IgnoreParenCasts();
        if (const auto *ref = dyn_cast<DeclRefExpr>(dst))
          if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl()))
            if (const auto *cat = ctx_.getAsConstantArrayType(vd->getType()))
              if (const auto *lit = dyn_cast<StringLiteral>(
                      call->getArg(call->getNumArgs() - 1)->IgnoreParenCasts()))
                provablyBounded =
                    lit->getLength() + 1 <= cat->getSize().getZExtValue();
        if (!provablyBounded) {
          std::string msg = std::string(baseName) +
                            "() writes without a destination capacity; use " +
                            (baseName == "sprintf" ? "snprintf" : "vsnprintf") +
                            " or cplus_sprintf_s";
          if (strict_) report(call->getBeginLoc(), msg);
          else warnAt(call->getBeginLoc(), msg);
        }
      }
      // A bounded write through `buffer + i * stride` inside a loop: compare
      // the worst-case end of the written range with the tracked allocation.
      if ((baseName == "memset" || baseName == "memcpy" ||
           baseName == "memmove") && call->getNumArgs() >= 3) {
        const Expr *dst = call->getArg(0)->IgnoreParenCasts();
        llvm::APSInt wsvg;
        if (knownConst(call->getArg(2), wsvg) && !wsvg.isNegative()) {
          const auto *add = dyn_cast<BinaryOperator>(dst);
          if (add && add->getOpcode() == BO_Add) {
            const Expr *baseE = add->getLHS()->IgnoreParenCasts();
            const Expr *offE = add->getRHS()->IgnoreParenCasts();
            if (const auto *bref = dyn_cast<DeclRefExpr>(baseE))
              if (const auto *vd = dyn_cast<VarDecl>(bref->getDecl()))
                if (const HeapAllocState *hs = heapStateOf(baseE)) {
                  if (hs->bytes > 0) {
                    const auto *mul = dyn_cast<BinaryOperator>(offE);
                    if (mul && mul->getOpcode() == BO_Mul) {
                      const auto *ivr = dyn_cast<DeclRefExpr>(
                          mul->getLHS()->IgnoreParenCasts());
                      llvm::APSInt strideV;
                      if (ivr && knownConst(mul->getRHS(), strideV) &&
                          !strideV.isNegative()) {
                        auto it = loopIndexOverride_.find(
                            dyn_cast<VarDecl>(ivr->getDecl()));
                        if (it != loopIndexOverride_.end() && it->second > 0) {
                          unsigned long long maxEnd =
                              (unsigned long long)it->second *
                                  strideV.getZExtValue() +
                              wsvg.getZExtValue();
                          if (maxEnd > (unsigned long long)hs->bytes)
                            report(call->getBeginLoc(),
                                   "write of " + std::to_string(maxEnd) +
                                       " bytes exceeds allocated object '" +
                                       vd->getNameAsString() + "' of " +
                                       std::to_string(hs->bytes) + " bytes");
                        }
                      }
                    }
                  }
                }
          }
        }
      }
      // calloc(count, size) must not wrap size_t.
      if (baseName == "calloc" && call->getNumArgs() >= 2) {
        auto apToU64 = [](const llvm::APSInt &v) -> unsigned long long {
          return v.isNegative()
                     ? static_cast<unsigned long long>(v.getSExtValue())
                     : v.getZExtValue();
        };
        auto constU64 = [&](const Expr *e, unsigned long long &out) -> bool {
          Expr::EvalResult r;
          if (e->IgnoreParenCasts()->EvaluateAsInt(r, ctx_) && r.Val.isInt()) {
            out = apToU64(r.Val.getInt());
            return true;
          }
          if (const auto *ref = dyn_cast<DeclRefExpr>(e->IgnoreParenCasts()))
            if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl())) {
              auto it = constLocals_.find(vd->getNameAsString());
              if (it != constLocals_.end()) {
                out = apToU64(it->second);
                return true;
              }
            }
          return false;
        };
        unsigned long long n = 0, sz = 0;
        if (constU64(call->getArg(0), n) && constU64(call->getArg(1), sz) &&
            n != 0 && sz > (unsigned long long)-1 / n)
          report(call->getBeginLoc(),
                 "calloc count * size overflows size_t; the allocation is "
                 "smaller than requested");
      }
      // A NULL FILE* passed to a stdio function dereferences null in libc.
      if ((baseName == "fgets" || baseName == "fread" ||
           baseName == "fprintf" || baseName == "printf_s" ||
           baseName == "fscanf") &&
          call->getNumArgs() >= 1) {
        unsigned streamIdx = (baseName == "fgets" || baseName == "fread" ||
                              baseName == "fscanf")
                                 ? 2 : 0;
        if (streamIdx < call->getNumArgs() &&
            isNullConstant(call->getArg(streamIdx)->IgnoreParenCasts()))
          report(call->getBeginLoc(),
                 "null FILE* passed to " + baseName);
      }
      // sizeof(pointer) as a size argument is the pointer width, not the
      // capacity of the pointed-to buffer (e.g. fgets(buf, sizeof(buf), ...)
      // where buf is a char* parameter).
      if ((baseName == "fgets" || baseName == "fread") && call->getNumArgs() >= 2) {
        const Expr *sz = call->getArg(1)->IgnoreParenCasts();
        if (const auto *ue = dyn_cast<UnaryExprOrTypeTraitExpr>(sz))
          if (ue->getKind() == UETT_SizeOf && !ue->isArgumentType() &&
              ue->getArgumentExpr()) {
            const Expr *a = ue->getArgumentExpr()->IgnoreParenCasts();
            if (const auto *ref = dyn_cast<DeclRefExpr>(a))
              if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl())) {
                QualType t = vd->getType();
                if (const auto *pt = t->getAs<PointerType>())
                  report(call->getBeginLoc(),
                         "sizeof(" + pt->getPointeeType().getAsString() +
                             "*) is the pointer size, not the buffer size");
              }
          }
      }
      // fgets/fread destination capacity: a constant size argument larger
      // than the fixed destination array is a stack overflow.
      if ((baseName == "fgets" || baseName == "fread") && call->getNumArgs() >= 3) {
        unsigned dstIdx = 0, sizeIdx = (baseName == "fgets") ? 1 : 2;
        const Expr *dst = call->getArg(dstIdx)->IgnoreParenCasts();
        long long amount = 0;
        Expr::EvalResult r;
        if (call->getArg(sizeIdx)->EvaluateAsInt(r, ctx_) && r.Val.isInt())
          amount = r.Val.getInt().getSExtValue();
        if (baseName == "fread") {
          Expr::EvalResult r2;
          if (call->getArg(1)->EvaluateAsInt(r2, ctx_) && r2.Val.isInt())
            amount *= r2.Val.getInt().getSExtValue();
        }
        const auto *dref = dyn_cast<DeclRefExpr>(dst);
        const auto *vd = dref ? dyn_cast<VarDecl>(dref->getDecl()) : nullptr;
        if (vd && amount > 0) {
          if (const auto *at = ctx_.getAsConstantArrayType(vd->getType())) {
            unsigned long long cap = at->getSize().getZExtValue();
            QualType elem = at->getElementType();
            unsigned es = ctx_.getTypeSize(elem) / 8u;
            if (es) cap *= es;
            if ((unsigned long long)amount > cap)
              report(call->getBeginLoc(),
                     baseName + " reads " + std::to_string(amount) +
                         " bytes into destination '" + vd->getNameAsString() +
                         "' of " + std::to_string(cap) + " bytes");
          }
        }
      }
      if ((baseName == "memcpy" || baseName == "memmove") && call->getNumArgs() >= 3) {
        checkBoundedCopy(call);
        checkSourceExtent(call, 1, 2);
        checkLibcNullSource(call, baseName);
      }
      if (baseName == "strncpy" && call->getNumArgs() >= 3) {
        // strncpy pads the destination with NULs, so a count larger than a
        // literal source is not a source over-read.
        if (!isa<StringLiteral>(call->getArg(1)->IgnoreParenCasts()))
          checkSourceExtent(call, 1, 2);
        checkLibcNullSource(call, baseName);
      }
      if (baseName == "memset" && call->getNumArgs() >= 3) {
        checkBoundedCopy(call, "fill");
      }
      if (baseName == "strcpy" && call->getNumArgs() >= 2) {
        if (isa<StringLiteral>(call->getArg(0)->IgnoreParenCasts()))
          report(call->getBeginLoc(), "write to string literal via strcpy");
        checkStringCopy(call);
        checkStrcpyNul(call);
        checkLibcNullSource(call, baseName);
      }
      if (baseName == "strncpy" && call->getNumArgs() >= 3) {
        checkBoundedWrite(call, 0, 2, "string copy");
        checkLibcNullSource(call, baseName);
      }
      if (baseName == "strcat" && call->getNumArgs() >= 2) {
        checkStringConcat(call);
        checkStringConcatHeap(call);
        checkLibcNullSource(call, baseName);
      }
      if ((baseName == "snprintf" || baseName == "vsnprintf") &&
          call->getNumArgs() >= 2) {
        checkBoundedWrite(call, 0, 1, "format write");
        // `int n = snprintf(buf, cap, ...); buf[n] = 0;` — n is the length
        // that WOULD have been written and may exceed cap.
        for (const auto &p : ctx_.getParents(*call)) {
          if (const auto *pvd = p.get<VarDecl>())
            if (pvd->getInit() && pvd->getInit()->IgnoreParenImpCasts() == call) {
              if (const auto *dr = dyn_cast<DeclRefExpr>(
                      call->getArg(0)->IgnoreParenCasts()))
                if (const auto *dv = dyn_cast<VarDecl>(dr->getDecl()))
                  snprintfResults_[pvd->getNameAsString()] =
                      dv->getNameAsString();
            }
          if (const auto *bo = p.get<BinaryOperator>())
            if (bo->isAssignmentOp())
              if (const auto *ref = dyn_cast<DeclRefExpr>(
                      bo->getLHS()->IgnoreParenCasts()))
                if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl()))
                  if (const auto *dr = dyn_cast<DeclRefExpr>(
                          call->getArg(0)->IgnoreParenCasts()))
                    if (const auto *dv = dyn_cast<VarDecl>(dr->getDecl()))
                      snprintfResults_[vd->getNameAsString()] =
                          dv->getNameAsString();
        }
      }
      if (baseName == "memcmp" && call->getNumArgs() >= 3) {
        checkLibcNullSource(call, baseName, 0);
        checkLibcNullSource(call, baseName, 1);
      }
      if ((baseName == "strlen" || baseName == "strcmp" ||
           baseName == "strnlen") && call->getNumArgs() >= 1)
        checkLibcNullSource(call, baseName, 0);
      if (baseName == "cplus_strcpy" && call->getNumArgs() >= 3) {
        const Expr *cap = call->getArg(1)->IgnoreParenImpCasts();
        const auto *lit = dyn_cast<StringLiteral>(
            call->getArg(2)->IgnoreParenImpCasts());
        Expr::EvalResult r;
        bool haveCap = cap->EvaluateAsInt(r, ctx_) && r.Val.isInt();
        unsigned long long capacity =
            haveCap ? r.Val.getInt().getZExtValue() : 0;
        if (lit && haveCap && lit->getLength() + 1 > capacity)
          report(call->getBeginLoc(),
                 "checked string copy needs " +
                     std::to_string(lit->getLength() + 1) +
                     " bytes but capacity is " + std::to_string(capacity));
        // A checked API is only as safe as the capacity the caller supplies.
        if (haveCap) {
          const Expr *dst = call->getArg(0)->IgnoreParenCasts();
          if (const auto *ref = dyn_cast<DeclRefExpr>(dst))
            if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl()))
              if (const auto *at = ctx_.getAsConstantArrayType(vd->getType())) {
                unsigned long long actual = at->getSize().getZExtValue();
                if (capacity > actual)
                  report(call->getBeginLoc(),
                         "checked capacity " + std::to_string(capacity) +
                             " exceeds true capacity " +
                             std::to_string(actual) + " of '" +
                             vd->getNameAsString() +
                             "'; the _s argument does not validate the "
                             "buffer size");
              }
        }
      }
      // User-function effect summaries: a function that frees its parameter
      // transfers that obligation to the call site.
      auto uit = userFrees_.find(name);
      if (uit != userFrees_.end() && !uit->second.empty()) {
        for (int idx : uit->second) {
          if (idx < 0 || (unsigned)idx >= call->getNumArgs())
            continue;
          const Expr *arg = call->getArg(idx)->IgnoreParenImpCasts();
          const auto *ref = dyn_cast<DeclRefExpr>(arg);
          const auto *vd = ref ? dyn_cast<VarDecl>(ref->getDecl()) : nullptr;
          if (!vd)
            continue;
          if (releasedVars_.count(vd->getNameAsString()))
            report(call->getBeginLoc(), "double free after call to '" + name +
                                            "' which frees its argument");
          else
            if (conditionalDepth_ == 0)
              markReleased(vd->getNameAsString());
        }
      }
    }
    if (!report_ && name == "pthread_create" && call->getNumArgs() >= 3) {
      std::string entry;
      if (const auto *ref = dyn_cast<DeclRefExpr>(
              call->getArg(2)->IgnoreParenImpCasts()))
        if (const auto *nd = dyn_cast<NamedDecl>(ref->getDecl()))
          entry = nd->getNameAsString();
      if (!entry.empty()) {
        ++threadEntryCounts_[entry];
        threadEntryLocs_[entry] = call->getBeginLoc();
      }
    }
    if (name == "pthread_create" && call->getNumArgs() >= 4) {
      const Expr *arg = call->getArg(3)->IgnoreParenImpCasts();
      const Expr *borrowed = arg;
      if (const auto *uo = dyn_cast<UnaryOperator>(arg))
        if (uo->getOpcode() == UO_AddrOf)
          borrowed = uo->getSubExpr()->IgnoreParenImpCasts();
      if (const auto *ref = dyn_cast<DeclRefExpr>(borrowed))
        if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl()))
          if (vd->hasLocalStorage())
            warnAt(call->getBeginLoc(),
                   "stack borrow passed to thread: '" +
                       vd->getNameAsString() + "'");
    }
    return true;
  }

  bool VisitArraySubscriptExpr(ArraySubscriptExpr *subscript) {
    if (!report_)
      return true;
    // A negative constant index is always out of bounds, heap or stack.
    long long constIdx = 0;
    if (evalIndexValue(subscript->getIdx(), constIdx) && constIdx < 0) {
      const Expr *base = subscript->getBase()->IgnoreParenCasts();
      if (const auto *ref = dyn_cast<DeclRefExpr>(base))
        if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl()))
          if (vd->getType()->isArrayType() && !heapStateOf(base)) {
            report(subscript->getBeginLoc(),
                   "negative array index on '" + vd->getNameAsString() + "'");
            return true;
          }
    }
    if (const auto *iref = dyn_cast<DeclRefExpr>(
            subscript->getIdx()->IgnoreParenCasts())) {
      auto it = snprintfResults_.find(iref->getDecl()->getNameAsString());
      if (it != snprintfResults_.end() &&
          !guardedSnprintf_.count(it->first))
        if (const auto *bref = dyn_cast<DeclRefExpr>(
                subscript->getBase()->IgnoreParenCasts()))
          if (bref->getDecl()->getNameAsString() == it->second)
            report(subscript->getBeginLoc(),
                   "snprintf return value used as index into its destination "
                   "'" + it->second +
                       "'; it may exceed the buffer capacity on truncation");
    }
    if (isReleased(subscript->getBase()))
      reportUaf(subscript->getBeginLoc(), ownerOfAddress(subscript->getBase()),
                "subscript of released pointer");
    else if (isNullConstant(subscript->getBase()) ||
             nullVarOf(subscript->getBase()))
      report(subscript->getBeginLoc(), "null pointer subscript");
    if (heapStateOf(subscript->getBase())) {
      checkHeapSubscript(subscript, subscript->getBeginLoc());
      return true;
    }
    // Flexible array member: `p->items[i]` where p is a heap allocation with
    // a known byte extent. The FAM begins at the struct's fixed-size prefix.
    if (const auto *me = dyn_cast<MemberExpr>(
            subscript->getBase()->IgnoreParenImpCasts())) {
      const Expr *ownerExpr = me->getBase();
      if (const HeapAllocState *hs = heapStateOf(ownerExpr)) {
        const auto *field = dyn_cast<FieldDecl>(me->getMemberDecl());
        const auto *famType = field ? ctx_.getAsIncompleteArrayType(field->getType())
                                    : nullptr;
        if (famType) {
          long long idx = 0;
          if (!evalIndexValue(subscript->getIdx(), idx) || idx < 0)
            return true;
          QualType elem = famType->getElementType();
          unsigned long long es = ctx_.getTypeSize(elem) / 8u;
          if (es == 0)
            return true;
          // The flexible array begins AFTER the struct's fixed prefix;
          // ignoring that prefix misses one-past-end accesses.
          unsigned long long prefix = 0;
          if (const auto *pt = ownerExpr->getType()->getAs<PointerType>())
            prefix = ctx_.getTypeSize(pt->getPointeeType()) / 8u;
          if (prefix + (unsigned long long)idx * es >=
              (unsigned long long)hs->bytes)
            report(subscript->getBeginLoc(),
                   "flexible array index " + std::to_string(idx) +
                       " exceeds heap allocation of " +
                       std::to_string(hs->bytes) + " bytes (including "
                       "struct prefix)");
          return true;
        }
      }
    }
    Expr *base = subscript->getBase()->IgnoreParenImpCasts();
    unsigned long long compExtent = 0;
    if (const auto *cle = dyn_cast<CompoundLiteralExpr>(base))
      if (const auto *cat = ctx_.getAsConstantArrayType(cle->getType()))
        compExtent = cat->getSize().getZExtValue();
    if (const auto *ref = dyn_cast<DeclRefExpr>(base))
      if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl())) {
        auto it = compoundLiteralVars_.find(vd);
        if (it != compoundLiteralVars_.end())
          compExtent = it->second;
      }
    if (compExtent) {
      long long ci = 0;
      if (evalIndexValue(subscript->getIdx(), ci) && ci >= 0 &&
          (unsigned long long)ci >= compExtent)
        report(subscript->getBeginLoc(),
               "array index " + std::to_string(ci) +
                   " exceeds compound literal of length " +
                   std::to_string(compExtent));
    }
    QualType baseType = base->getType();
    const auto *arrayType = ctx_.getAsConstantArrayType(baseType);
    if (!arrayType)
      return true;
    long long signedIndex = 0;
    if (!evalIndexValue(subscript->getIdx(), signedIndex))
      return true;
    if (signedIndex < 0) {
      report(subscript->getBeginLoc(),
             "array index " + std::to_string(signedIndex) + " is negative");
      return true;
    }
    unsigned long long extent = arrayType->getSize().getZExtValue();
    unsigned long long value = static_cast<unsigned long long>(signedIndex);
    if (value >= extent) {
      std::string name = varNameOf(subscript->getBase());
      if (name.empty())
        name = lvalueKey(subscript->getBase());
      Diag d;
      d.level = DiagLevel::Error;
      d.message = "[CPLUS-SAFE-BOUNDS] constant array index " +
                  std::to_string(value) + " is outside the bounds of '" + name +
                  "'";
      d.file = filename_;
      PresumedLoc ploc =
          ctx_.getSourceManager().getPresumedLoc(subscript->getBeginLoc());
      if (ploc.isValid()) {
        d.line = ploc.getLine();
        d.column = ploc.getColumn();
        std::string f = ploc.getFilename();
        if (f == filename_ + ".c") f = filename_;
        if (!f.empty()) d.file = f;
      }
      d.help = "array contains " + std::to_string(extent) +
               " elements; valid indices are 0.." +
               std::to_string(extent - 1);
      diags_.report(std::move(d));
    }
    return true;
  }

private:
  static std::string diagnosticCategory(const std::string &message) {
    std::string category = "CPLUS-FFI";
    if (message.find("overflow") != std::string::npos ||
        message.find("division by zero") != std::string::npos ||
        message.find("remainder by zero") != std::string::npos)
      category = "CPLUS-ARITH";
    else if (message.find("double free") != std::string::npos ||
        message.find("invalid free") != std::string::npos)
      category = "CPLUS-OWNER";
    else if (message.find("use-after-move") != std::string::npos)
      category = "CPLUS-OWNER";
    else if (message.find("use-after-free") != std::string::npos ||
             message.find("dereference of released") != std::string::npos)
      category = "CPLUS-LIFETIME";
    else if (message.find("borrow") != std::string::npos)
      category = "CPLUS-BORROW";
    else if (message.find("alias state") != std::string::npos)
      category = "CPLUS-ALIAS";
    else if (message.find("realloc") != std::string::npos)
      category = "CPLUS-REALLOC";
    else if (message.find("copy of") != std::string::npos ||
             message.find("exceeds destination") != std::string::npos ||
             message.find("array index") != std::string::npos ||
             message.find("pointer arithmetic") != std::string::npos)
      category = "CPLUS-BOUNDS";
    else if (message.find("RAII") != std::string::npos)
      category = "CPLUS-RAII";
    else if (message.find("null pointer") != std::string::npos ||
             message.find("null source") != std::string::npos ||
             message.find("uninitialized") != std::string::npos)
      category = "CPLUS-LIFETIME";
    else if (message.find("checked string copy") != std::string::npos)
      category = "CPLUS-LIBC";
    else if (message.find("string concatenation") != std::string::npos ||
             message.find("string copy") != std::string::npos ||
             message.find("format write") != std::string::npos)
      category = "CPLUS-BOUNDS";
    else if (message.find("libc") != std::string::npos ||
             message.find("string copy") != std::string::npos)
      category = "CPLUS-LIBC";
    // Keep the older prefix for continuity with existing fixtures/docs.
    if (category == "CPLUS-BOUNDS")
      return category + "] [CPLUS-SAFE-BOUNDS";
    if (category == "CPLUS-LIFETIME")
      return category + "] [CPLUS-SAFE-LIFETIME";
    if (category == "CPLUS-FREE")
      return category + "] [CPLUS-SAFE-FREE";
    return category;
  }

  void report(SourceLocation loc, const std::string &message) {
    PresumedLoc ploc = ctx_.getSourceManager().getPresumedLoc(loc);
    Diag d;
    d.level = DiagLevel::Error;
    d.message = "[" + diagnosticCategory(message) + "] " + message;
    if (message.find("use-after-free") != std::string::npos)
      d.help = "suggested correction: do not dereference after release; copy "
               "the data or transfer ownership with cplus_move()";
    else if (message.find("double free") != std::string::npos)
      d.help = "suggested correction: release each owner exactly once; use "
               "cplus_move() to transfer ownership";
    else if (message.find("uninitialized") != std::string::npos)
      d.help = "suggested correction: initialize the variable on every path "
               "before reading it";
    d.file = filename_;
    if (ploc.isValid()) {
      d.line = ploc.getLine();
      d.column = ploc.getColumn();
      std::string diagFile = ploc.getFilename();
      // Tooling analyses a synthetic "<file>.c" buffer; map it back.
      if (diagFile == filename_ + ".c")
        diagFile = filename_;
      if (diagFile != filename_)
        d.file = diagFile;
    }
    diags_.report(std::move(d));
  }

  /// Use-after-free diagnostic with the origin/alias/release source chain.
  void reportUaf(SourceLocation loc, const std::string &var,
                 const std::string &what) {
    Diag d;
    d.level = DiagLevel::Error;
    d.message = "[CPLUS-LIFETIME] [CPLUS-SAFE-LIFETIME] use-after-free: " + what;
    d.help = "suggested correction: do not use after release; copy the data "
             "or transfer ownership with cplus_move()";
    d.file = filename_;
    PresumedLoc ploc = ctx_.getSourceManager().getPresumedLoc(loc);
    if (ploc.isValid()) {
      d.line = ploc.getLine();
      d.column = ploc.getColumn();
      std::string diagFile = ploc.getFilename();
      // Tooling analyses a synthetic "<file>.c" buffer; map it back.
      if (diagFile == filename_ + ".c")
        diagFile = filename_;
      if (diagFile != filename_)
        d.file = diagFile;
    }
    // Resolve the owner behind an alias so the chain covers allocation and
    // release sites recorded under the original variable name.
    std::string root = var;
    auto git = aliasGroups_.find(var);
    if (git != aliasGroups_.end())
      for (const auto &candidate : git->second)
        if (allocLocs_.count(candidate) || releaseLocs_.count(candidate)) {
          root = candidate;
          break;
        }
    auto addNoteFor = [&](const char *label,
                          const std::map<std::string, SourceLocation> &m,
                          const std::string &key) {
      auto it = m.find(key);
      if (it == m.end())
        return;
      PresumedLoc p = ctx_.getSourceManager().getPresumedLoc(it->second);
      if (!p.isValid())
        return;
      Diag n;
      n.message = std::string(label) + p.getFilename() + ":" +
                  std::to_string(p.getLine());
      d.notes.push_back(std::move(n));
    };
    addNoteFor("allocation: ", allocLocs_, root);
    addNoteFor("alias created: ", aliasLocs_, var);
    addNoteFor("owner released: ", releaseLocs_, root);
    diags_.report(std::move(d));
  }

  ASTContext &ctx_;
  DiagnosticEngine &diags_;
  std::string filename_;
  bool report_;
  bool strict_ = false;
  std::set<unsigned> handledReallocs_;
  std::map<std::string, SourceLocation> allocLocs_;
  std::map<std::string, SourceLocation> aliasLocs_;
  std::map<std::string, SourceLocation> releaseLocs_;
  std::set<const VarDecl *> stringPtrs_;
  std::map<const VarDecl *, unsigned long long> compoundLiteralVars_;
  std::set<const VarDecl *> nonZeroVars_;
  std::map<const VarDecl *, long long> loopLowerBound_;
  std::set<std::string> overflowingConstVars_;
  std::map<std::string, HeapAllocState> heapAllocs_;
  std::set<const VarDecl *> heapWritten_;
  std::set<const VarDecl *> heapInterior_;
  std::set<const VarDecl *> onePastEnd_;
  std::map<const VarDecl *, std::string> strlenSized_;
  std::map<const VarDecl *, long long> loopIndexOverride_;
  std::map<std::string, llvm::APSInt> constLocals_;
  std::set<std::string> assignedVars_;
  std::set<std::string> functionsReturningFreed_;
  std::set<std::string> nullReturningFunctions_;
  std::set<std::string> maybeReleasedVars_;
  std::map<std::string, unsigned> maybeReleaseLines_;
  std::set<std::string> guardedFromConst_;
  std::string nonNullBranch_;
  std::set<std::string> provenanceInts_;
  // Interprocedural allocation extents: callee name -> param index -> bytes,
  // gathered from call sites whose argument is a tracked allocation.
  std::map<std::string, std::map<unsigned, long long>> callParamExtents_;
  std::set<std::string> allGlobals_;
  std::map<std::string, int> threadEntryCounts_;
  std::map<std::string, SourceLocation> threadEntryLocs_;
  const FunctionDecl *currentFnDecl_ = nullptr;
  std::string currentFunction_;

public:
  std::set<std::string> handlers_;
  std::set<std::string> nonVolatileGlobals_;
  std::set<std::string> nullVars_;
  std::set<std::string> releasedVars_;
  std::set<std::string> releasedFields_;
  std::map<std::string, std::set<std::string>> fieldAliases_;
  std::map<std::string, std::string> formatFnAliases_;
  std::map<std::string, std::string> snprintfResults_;
  std::map<std::string, std::string> freadResults_;
  std::set<std::string> contractIssuesReported_;
  std::set<std::string> guardedSnprintf_;
  std::string reallocRebindOwner_, reallocRebindTemp_;
  std::map<std::string, std::set<std::string>> aliasGroups_;
  int conditionalDepth_ = 0;
  std::map<std::string, std::set<int>> userFrees_;
  std::map<std::string, std::string> borrowOwner_;
  std::map<std::string, std::set<std::string>> writeBorrows_;
  std::set<std::string> movedVars_;
  std::set<std::string> destructors_;
};

} // namespace

void checkAstSafety(const std::string &loweredC, const std::string &filename,
                    const std::vector<std::string> &backendArgs,
                    DiagnosticEngine &diags, bool strict) {
  // Keep only parse-relevant flags; sanitizer and warning-as-error flags can
  // make the in-memory AST parse fail for unrelated reasons.
  std::vector<std::string> args;
  for (const auto &arg : backendArgs) {
    if (arg.rfind("-I", 0) == 0 || arg.rfind("-D", 0) == 0 ||
        arg.rfind("-std=", 0) == 0)
      args.push_back(arg);
  }
#ifdef CPLUS_CLANG_RESOURCE_DIR
  args.push_back(std::string("-resource-dir=") + CPLUS_CLANG_RESOURCE_DIR);
#endif
  args.push_back("-fsyntax-only");
  // Tooling infers language from the filename suffix; a .cp suffix would be
  // treated as C++.  Use a synthetic .c name for the in-memory buffer.
  std::string astFile = filename + ".c";
  // Capture Clang's format-security/format warnings instead of discarding
  // them. In STRICT they are promoted to errors; in DEFAULT they warn.
  class FormatWarnConsumer : public clang::DiagnosticConsumer {
  public:
    FormatWarnConsumer(DiagnosticEngine &d, const std::string &f, bool strict)
        : diags(d), file(f), strict(strict) {}
    void HandleDiagnostic(clang::DiagnosticsEngine::Level level,
                          const clang::Diagnostic &info) override {
      clang::DiagnosticConsumer::HandleDiagnostic(level, info);
      if (level == clang::DiagnosticsEngine::Error) {
        llvm::SmallString<256> em;
        info.FormatDiagnostic(em);
          }
      llvm::SmallString<256> msg;
      info.FormatDiagnostic(msg);
      std::string s(msg.str());
      if (s.find("format") == std::string::npos &&
          s.find("conversion") == std::string::npos &&
          s.find("field width") == std::string::npos &&
          s.find("precision") == std::string::npos &&
          s.find("will always overflow") == std::string::npos &&
          s.find("division by zero") == std::string::npos)
        return;
      Diag d;
      d.level = (strict || level == clang::DiagnosticsEngine::Error)
                    ? DiagLevel::Error : DiagLevel::Warning;
      d.message = "[CPLUS-FFI] " + s;
      d.file = file;
      if (info.hasSourceManager() && info.getLocation().isValid()) {
        PresumedLoc p = info.getSourceManager().getPresumedLoc(info.getLocation());
        if (p.isValid()) { d.line = p.getLine(); d.column = p.getColumn(); }
      }
      diags.report(std::move(d));
    }
  private:
    DiagnosticEngine &diags;
    std::string file;
    bool strict;
  } fmtConsumer(diags, filename, strict);
  std::unique_ptr<ASTUnit> ast = tooling::buildASTFromCodeWithArgs(
      loweredC, args, astFile, "cplus-ast-check",
      std::make_shared<PCHContainerOperations>(),
      tooling::getClangStripDependencyFileAdjuster(), {}, &fmtConsumer);
  if (!ast || ast->getDiagnostics().hasErrorOccurred())
    return; // AST parse failed; do not guess from a broken AST.

  SafetyVisitor collect(ast->getASTContext(), diags, filename, false);
  collect.TraverseDecl(ast->getASTContext().getTranslationUnitDecl());

  SafetyVisitor check(ast->getASTContext(), diags, filename, true);
  check.setStrict(strict);
  check.setHandlers(collect.handlers());
  check.setGlobals(collect.nonVolatileGlobals());
  check.setDestructors(collect.destructors());
  check.setFieldAliases(collect.fieldAliases());
  check.setFormatFnAliases(collect.formatFnAliases());
  check.setHandledReallocs(collect.handledReallocs());
  check.setConstLocals(collect.constLocals());
  check.setAssignedVars(collect.assignedVars());
  check.setFunctionsReturningFreed(collect.functionsReturningFreed());
  check.setNullReturningFunctions(collect.nullReturningFunctions());
  check.setMaybeReleasedVars(collect.maybeReleasedVars());
  check.setMaybeReleaseLines(collect.maybeReleaseLines());
  check.setCallParamExtents(collect.callParamExtents());
  check.TraverseDecl(ast->getASTContext().getTranslationUnitDecl());

  // Thread-race heuristic: the same entry function passed to more than one
  // pthread_create that writes a global has unsynchronised shared mutable
  // state. Reject in STRICT, warn in DEFAULT.
  ASTContext &raceCtx = ast->getASTContext();
  for (const auto &entry : collect.threadEntryCounts()) {
    if (entry.second < 2)
      continue;
    const FunctionDecl *entryFn = nullptr;
    struct FnFinder : RecursiveASTVisitor<FnFinder> {
      const std::string &want;
      const FunctionDecl **out;
      explicit FnFinder(const std::string &w, const FunctionDecl **o)
          : want(w), out(o) {}
      bool VisitFunctionDecl(FunctionDecl *fd) {
        if (!*out && fd->getNameAsString() == want)
          *out = fd;
        return true;
      }
    } finder(entry.first, &entryFn);
    finder.TraverseDecl(raceCtx.getTranslationUnitDecl());
    if (!entryFn || !entryFn->getBody())
      continue;
    GlobalWriteFinder gw(collect.allGlobals());
    gw.TraverseStmt(entryFn->getBody());
    if (gw.synchronized)
      continue;
    for (const auto &g : gw.freed) {
      auto lit = collect.threadEntryLocs().find(entry.first);
      Diag d;
      d.level = DiagLevel::Error;
      d.message = "double free of shared global '" + g +
                  "' across multiple threads";
      d.file = filename;
      if (lit != collect.threadEntryLocs().end()) {
        PresumedLoc p = raceCtx.getSourceManager().getPresumedLoc(lit->second);
        if (p.isValid()) { d.line = p.getLine(); d.column = p.getColumn(); }
      }
      diags.report(std::move(d));
    }
    for (const auto &g : gw.written) {
      auto lit = collect.threadEntryLocs().find(entry.first);
      Diag d;
      d.level = strict ? DiagLevel::Error : DiagLevel::Warning;
      d.message = "[" + std::string("CPLUS-FFI") +
                  "] multiple threads mutate shared global '" + g +
                  "' without synchronization";
      d.file = filename;
      if (lit != collect.threadEntryLocs().end()) {
        PresumedLoc p = raceCtx.getSourceManager().getPresumedLoc(lit->second);
        if (p.isValid()) {
          d.line = p.getLine();
          d.column = p.getColumn();
        }
      }
      diags.report(std::move(d));
    }
  }

  // CFG/dataflow definite-initialization analysis on each function body.
  class FunctionRunner
      : public RecursiveASTVisitor<FunctionRunner> {
  public:
    FunctionRunner(ASTContext &ctx, const std::string &file,
                   DiagnosticEngine &diags)
        : ctx_(ctx), file_(file), diags_(diags) {}
    bool VisitFunctionDecl(FunctionDecl *fd) {
      checkDefiniteInitialization(
          ctx_, fd, [this](SourceLocation loc, const std::string &msg) {
            PresumedLoc ploc = ctx_.getSourceManager().getPresumedLoc(loc);
            Diag d;
            d.level = DiagLevel::Error;
            d.message = "[CPLUS-LIFETIME] " + msg;
            d.file = file_;
            if (ploc.isValid()) {
              d.line = ploc.getLine();
              d.column = ploc.getColumn();
              if (std::string(ploc.getFilename()) != file_)
                d.file = ploc.getFilename();
            }
            diags_.report(std::move(d));
          });
      return true;
    }
  private:
    ASTContext &ctx_;
    std::string file_;
    DiagnosticEngine &diags_;
  };
  FunctionRunner runner(ast->getASTContext(), filename, diags);
  runner.TraverseDecl(ast->getASTContext().getTranslationUnitDecl());
}

} // namespace cplus
