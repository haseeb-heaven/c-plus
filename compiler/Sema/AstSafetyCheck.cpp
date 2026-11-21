#include "Sema/AstSafetyCheck.h"

#include "Diagnostics.h"
#include "Analysis/OwnershipAnalysis.h"

#include <llvm/ADT/StringRef.h>

#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/Expr.h>
#include <clang/AST/RecursiveASTVisitor.h>
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
    std::string name = varNameOf(arg);
    auto it = constLocals_.find(name);
    if (name.empty() || it == constLocals_.end() || assignedVars_.count(name))
      return false;
    llvm::APSInt v = it->second;
    if (v.isNegative())
      return false;
    out = (long long)v.getZExtValue();
    return true;
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
    if (callee != "malloc" && callee != "calloc")
      return;
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
    if (!evalByteCount(arg, bytes))
      return;
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
    if (name.empty() || it == constLocals_.end() || assignedVars_.count(name))
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
    Expr::EvalResult br;
    if (bo->getRHS()->EvaluateAsInt(br, ctx_) && br.Val.isInt()) {
      bound = br.Val.getInt().getSExtValue();
    } else {
      std::string bn = varNameOf(bo->getRHS());
      auto it = constLocals_.find(bn);
      if (bn.empty() || it == constLocals_.end() || assignedVars_.count(bn))
        return false;
      bound = it->second.getSExtValue();
    }
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
         elem->isFloatingType() || elem->isEnumeralType()) &&
        !elem->isDependentType())
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
    if (const auto *ase = dyn_cast<ArraySubscriptExpr>(e)) {
      std::string name = varNameOf(ase->getBase());
      if (!name.empty() && stringPtrs_.count(name))
        report(loc, "write to string literal '" + name + "'");
    } else if (const auto *uo = dyn_cast<UnaryOperator>(e)) {
      if (uo->getOpcode() == UO_Deref) {
        std::string name = varNameOf(uo->getSubExpr());
        if (!name.empty() && stringPtrs_.count(name))
          report(loc, "write to string literal '" + name + "'");
      }
    }
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
      if (stringPtrs_.count(name)) {
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
        Expr::EvalResult r;
        if (bo->getRHS()->EvaluateAsInt(r, ctx_) && r.Val.isInt() &&
            r.Val.getInt() != 0) {
          report(loc, "invalid free of interior pointer '" +
                          varNameOf(bo->getLHS()) + "' via " + api);
        }
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

  bool isReleased(const Expr *expr) const {
    return releasedVars_.count(varNameOf(expr)) != 0;
  }

  bool isMaybeReleased(const Expr *expr) const {
    return maybeReleasedVars_.count(varNameOf(expr)) != 0;
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
    owner = varNameOf(call->getArg(0));
    if (out)
      *out = call;
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
    maybeReleasedVars_.insert(collector.released.begin(),
                              collector.released.end());
  }

  bool TraverseIfStmt(IfStmt *s) {
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
    bool negatedGuard = false;
    if (s->getCond())
      if (const auto *uo = dyn_cast<UnaryOperator>(
              s->getCond()->IgnoreParenImpCasts()))
        negatedGuard = uo->getOpcode() == UO_LNot;
    if (report_) {
      std::string owner, tmp;
      if (detectReallocSuccess(s, owner, tmp)) {
        bool r = TraverseStmt(s->getCond());
        if (negatedGuard) {
          // The then-branch is the FAILURE path (e.g. `if(!t){free(p);
          // return 1;}`). Ownership stays with the original allocation.
          r &= TraverseStmt(s->getThen());
          if (s->getElse())
            r &= TraverseStmt(s->getElse());
          --conditionalDepth_;
          return r;
        }
        // Inside the success branch the old pointer and its aliases are
        // invalid; outside it they remain valid (failure path).
        markReleased(owner);
        r &= TraverseStmt(s->getThen());
        clearReleased(owner);
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
      if (!condVar.empty())
        nullVars_.erase(condVar);
    }
    bool r = RecursiveASTVisitor::TraverseIfStmt(s);
    nullVars_ = savedNulls;
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
    --conditionalDepth_;
    return r;
  }
  bool TraverseWhileStmt(WhileStmt *s) {
    ++conditionalDepth_;
    if (!report_)
      collectMayRelease(s->getBody());
    bool r = RecursiveASTVisitor::TraverseWhileStmt(s);
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
    if (report_) {
      nullVars_.clear();
      releasedVars_.clear();
      aliasGroups_.clear();
      borrowOwner_.clear();
      writeBorrows_.clear();
      movedVars_.clear();
      heapAllocs_.clear();
      heapWritten_.clear();
      heapInterior_.clear();
      onePastEnd_.clear();
      strlenSized_.clear();
    }
    bool result = RecursiveASTVisitor::TraverseFunctionDecl(fd);
    currentFunction_ = previousFunction;
    nullVars_ = previousNulls;
    return result;
  }

  bool VisitCastExpr(CastExpr *cast) {
    if (!report_)
      return true;
    const Expr *sub = cast->getSubExpr()->IgnoreParenImpCasts();
    if (!sub || !cast->getType()->isPointerType() ||
        !sub->getType()->isIntegerType() || isNullConstant(sub))
      return true;
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
    if (!report_ && vd->hasGlobalStorage()) {
      allGlobals_.insert(vd->getNameAsString());
      if (!vd->getType().isVolatileQualified())
        nonVolatileGlobals_.insert(vd->getNameAsString());
    }
    recordHeapAllocation(vd);
    if (!report_) {
      // Record local integer constants for constant-propagated bounds checks.
      if (vd->hasLocalStorage() && vd->getType()->isIntegerType() &&
          vd->getInit() && !vd->getType().isVolatileQualified()) {
        Expr::EvalResult r;
        if (vd->getInit()->EvaluateAsInt(r, ctx_) && r.Val.isInt())
          constLocals_[vd->getNameAsString()] = r.Val.getInt();
      }
      return true;
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
      if (isa<StringLiteral>(init->IgnoreParenCasts()))
        stringPtrs_.insert(vd->getNameAsString());
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

  bool VisitBinaryOperator(BinaryOperator *op) {
    if (!report_) {
      if (op->isAssignmentOp())
        assignedVars_.insert(varNameOf(op->getLHS()));
      return true;
    }
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
      if (const VarDecl *vd = nullVarOf(op->getLHS()))
        nullVars_.erase(vd->getNameAsString());
      if (isNullConstant(op->getRHS())) {
        const Expr *lhs = op->getLHS()->IgnoreParenImpCasts();
        clearReleased(varNameOf(lhs));
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
                  "dereference of released pointer '" + varNameOf(sub) + "'");
      else if (isMaybeReleased(sub) && conditionalDepth_ == 0)
        warnAt(op->getBeginLoc(),
               "possible use-after-free: '" + varNameOf(sub) +
                   "' may have been released on a branch path");
      else if (movedVars_.count(varNameOf(sub)))
        report(op->getBeginLoc(), "use-after-move: dereference of moved resource");
      else if (isNullConstant(sub) || nullVarOf(sub))
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
      if (std::string(ploc.getFilename()) != filename_)
        d.file = ploc.getFilename();
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
    Expr::EvalResult amount;
    long long bytes = 0;
    if (n->EvaluateAsInt(amount, ctx_) && amount.Val.isInt()) {
      bytes = amount.Val.getInt().getSExtValue();
    } else {
      // Constant-propagate an unmodified local `int n = <literal>;`.
      std::string name = varNameOf(n);
      auto it = constLocals_.find(name);
      if (name.empty() || it == constLocals_.end() || assignedVars_.count(name))
        return;
      bytes = it->second.getSExtValue();
    }
    if (bytes <= 0)
      return;
    const auto *ref = dyn_cast<DeclRefExpr>(dst);
    if (!ref)
      return;
    const auto *vd = dyn_cast<VarDecl>(ref->getDecl());
    if (!vd)
      return;
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
    const auto *str = dyn_cast<StringLiteral>(src);
    if (!dstRef || !str)
      return;
    const auto *vd = dyn_cast<VarDecl>(dstRef->getDecl());
    const auto *at = vd ? ctx_.getAsConstantArrayType(vd->getType()) : nullptr;
    if (!at)
      return;
    unsigned long long cap = at->getSize().getZExtValue();
    unsigned long long need = str->getLength() + 1;
    if (need > cap)
      report(call->getBeginLoc(), "string copy of " + std::to_string(need - 1) +
                                      " chars plus NUL exceeds destination '" +
                                      vd->getNameAsString() + "'");
  }

  bool VisitCallExpr(CallExpr *call) {
    const FunctionDecl *callee = call->getDirectCallee();
    std::string name = callee ? callee->getNameAsString() : std::string();
    if (!callee) {
      const Expr *calleeExpr = call->getCallee()->IgnoreParenImpCasts();
      if (const auto *ref = dyn_cast<DeclRefExpr>(calleeExpr))
        callee = dyn_cast<FunctionDecl>(ref->getDecl());
      if (!callee)
        return true;
      name = callee->getNameAsString();
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
    if (baseName.rfind("__builtin___memcpy_chk", 0) == 0)
      baseName = "memcpy";
    else if (baseName.rfind("__builtin___memmove_chk", 0) == 0)
      baseName = "memmove";
    else if (baseName.rfind("__builtin___strcpy_chk", 0) == 0)
      baseName = "strcpy";
    else if (baseName.rfind("__builtin___memset_chk", 0) == 0)
      baseName = "memset";
    else if (baseName.rfind("__builtin___strcat_chk", 0) == 0)
      baseName = "strcat";
    else if (baseName.rfind("__builtin___strncpy_chk", 0) == 0)
      baseName = "strncpy";
    else if (baseName.rfind("__builtin___snprintf_chk", 0) == 0)
      baseName = "snprintf";
    if (!report_ && name.size() > 5 &&
        name.compare(name.size() - 5, 5, "_drop") == 0 &&
        call->getNumArgs() >= 1) {
      destructors_.insert(name);
    }
    if (report_) {
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
      if (isAllocatorFree && call->getNumArgs() >= 1)
        checkInvalidFree(call->getBeginLoc(), call->getArg(0), baseName);
      bool invalidates = isAllocatorFree ||
                         destructors_.count(name) != 0;
      if (invalidates && ptrVar) {
        std::string pname = ptrVar->getNameAsString();
        if (isAllocatorFree && releasedVars_.count(pname))
          report(call->getBeginLoc(), "double free of '" + pname + "'");
        else if (isAllocatorFree && conditionalDepth_ == 0 &&
                 maybeReleasedVars_.count(pname) &&
                 !releasedVars_.count(pname))
          report(call->getBeginLoc(),
                 "double free of '" + pname + "' on some branch path");
        else if (conditionalDepth_ == 0) {
          releaseLocs_[pname] = call->getBeginLoc();
          markReleased(pname);
        }
      }
      if (baseName == "realloc" &&
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
      if ((baseName == "memcpy" || baseName == "memmove") && call->getNumArgs() >= 3) {
        checkBoundedCopy(call);
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
        checkLibcNullSource(call, baseName);
      }
      if ((baseName == "snprintf" || baseName == "vsnprintf") &&
          call->getNumArgs() >= 2) {
        checkBoundedWrite(call, 0, 1, "format write");
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
        if (lit && cap->EvaluateAsInt(r, ctx_) && r.Val.isInt()) {
          unsigned long long capacity = r.Val.getInt().getZExtValue();
          if (lit->getLength() + 1 > capacity)
            report(call->getBeginLoc(),
                   "checked string copy needs " +
                       std::to_string(lit->getLength() + 1) +
                       " bytes but capacity is " + std::to_string(capacity));
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
    Expr *base = subscript->getBase()->IgnoreParenImpCasts();
    QualType baseType = base->getType();
    const auto *arrayType = ctx_.getAsConstantArrayType(baseType);
    if (!arrayType)
      return true;
    Expr::EvalResult result;
    llvm::APSInt index;
    if (subscript->getIdx()->EvaluateAsInt(result, ctx_) &&
        result.Val.isInt()) {
      index = result.Val.getInt();
    } else {
      // Constant-propagate a local `int i = <literal>;` that is never
      // reassigned.  This is still a compile-time proof, not a guess.
      std::string name = varNameOf(subscript->getIdx());
      auto it = constLocals_.find(name);
      if (name.empty() || it == constLocals_.end() ||
          assignedVars_.count(name))
        return true;
      index = it->second;
    }
    if (index.isNegative()) {
      report(subscript->getBeginLoc(),
             "array index " + std::to_string(index.getSExtValue()) +
                 " is negative");
      return true;
    }
    unsigned long long extent = arrayType->getSize().getZExtValue();
    unsigned long long value = index.getZExtValue();
    if (value >= extent)
      report(subscript->getBeginLoc(),
             "array index " + std::to_string(value) +
                 " exceeds length " + std::to_string(extent));
    return true;
  }

private:
  static std::string diagnosticCategory(const std::string &message) {
    std::string category = "CPLUS-FFI";
    if (message.find("double free") != std::string::npos ||
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
      if (std::string(ploc.getFilename()) != filename_)
        d.file = ploc.getFilename();
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
      if (std::string(ploc.getFilename()) != filename_)
        d.file = ploc.getFilename();
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
  std::set<std::string> stringPtrs_;
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
  args.push_back("-fsyntax-only");
  // Tooling infers language from the filename suffix; a .cp suffix would be
  // treated as C++.  Use a synthetic .c name for the in-memory buffer.
  std::string astFile = filename + ".c";
  IgnoringDiagConsumer ignore;
  std::unique_ptr<ASTUnit> ast = tooling::buildASTFromCodeWithArgs(
      loweredC, args, astFile, "cplus-ast-check",
      std::make_shared<PCHContainerOperations>(),
      tooling::getClangStripDependencyFileAdjuster(), {}, &ignore);
  if (!ast || ast->getDiagnostics().hasErrorOccurred())
    return; // AST parse failed; do not guess from a broken AST.

  SafetyVisitor collect(ast->getASTContext(), diags, filename, false);
  collect.TraverseDecl(ast->getASTContext().getTranslationUnitDecl());

  SafetyVisitor check(ast->getASTContext(), diags, filename, true);
  check.setStrict(strict);
  check.setHandlers(collect.handlers());
  check.setGlobals(collect.nonVolatileGlobals());
  check.setDestructors(collect.destructors());
  check.setHandledReallocs(collect.handledReallocs());
  check.setConstLocals(collect.constLocals());
  check.setAssignedVars(collect.assignedVars());
  check.setFunctionsReturningFreed(collect.functionsReturningFreed());
  check.setNullReturningFunctions(collect.nullReturningFunctions());
  check.setMaybeReleasedVars(collect.maybeReleasedVars());
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
