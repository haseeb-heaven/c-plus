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

#include <set>
#include <string>

using namespace clang;

namespace cplus {
namespace {

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

  bool TraverseFunctionDecl(FunctionDecl *fd) {
    std::string previousFunction = currentFunction_;
    std::set<std::string> previousNulls = nullVars_;
    currentFunction_ = fd->getNameAsString();
    if (report_) {
      nullVars_.clear();
      releasedVars_.clear();
    }
    bool result = RecursiveASTVisitor::TraverseFunctionDecl(fd);
    currentFunction_ = previousFunction;
    nullVars_ = previousNulls;
    return result;
  }

  bool VisitVarDecl(VarDecl *vd) {
    if (!report_ && vd->hasGlobalStorage() &&
        !vd->getType().isVolatileQualified())
      nonVolatileGlobals_.insert(vd->getNameAsString());
    if (report_ && vd->getType()->isPointerType() &&
        isNullConstant(vd->getInit()))
      nullVars_.insert(vd->getNameAsString());
    return true;
  }

  bool VisitBinaryOperator(BinaryOperator *op) {
    if (!report_)
      return true;
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
      if (const VarDecl *vd = nullVarOf(op->getLHS()))
        nullVars_.erase(vd->getNameAsString());
      if (isNullConstant(op->getRHS())) {
        const Expr *lhs = op->getLHS()->IgnoreParenImpCasts();
        if (const auto *ref = dyn_cast<DeclRefExpr>(lhs))
          if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl()))
            releasedVars_.erase(vd->getNameAsString());
      }
      if (handlers_.count(currentFunction_))
        reportGlobalWrite(op->getLHS(), op->getBeginLoc());
    }
    return true;
  }

  bool VisitUnaryOperator(UnaryOperator *op) {
    if (!report_)
      return true;
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
      if (isNullConstant(sub) || nullVarOf(sub))
        report(op->getBeginLoc(), "null pointer dereference");
    }
    if (handlers_.count(currentFunction_) &&
        (op->isIncrementOp() || op->isDecrementOp()))
      reportGlobalWrite(op->getSubExpr(), op->getBeginLoc());
    return true;
  }

  bool VisitMemberExpr(MemberExpr *member) {
    if (!report_ || !member->isArrow())
      return true;
    if (nullVarOf(member->getBase()))
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
    d.level = DiagLevel::Warning;
    d.message = message;
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
  void checkBoundedCopy(const CallExpr *call) {
    const Expr *dst = call->getArg(0)->IgnoreParenImpCasts();
    const Expr *n = call->getArg(2)->IgnoreParenImpCasts();
    Expr::EvalResult amount;
    if (!n->EvaluateAsInt(amount, ctx_) || !amount.Val.isInt())
      return;
    long long bytes = amount.Val.getInt().getSExtValue();
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
      report(call->getBeginLoc(), "copy of " + std::to_string(bytes) +
                                      " bytes exceeds destination '" +
                                      vd->getNameAsString() + "'");
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
    if (report_) {
      const VarDecl *ptrVar = nullptr;
      if (call->getNumArgs() >= 1) {
        const Expr *arg = call->getArg(0)->IgnoreParenImpCasts();
        if (const auto *ref = dyn_cast<DeclRefExpr>(arg))
          if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl()))
            ptrVar = vd;
      }
      if ((baseName == "free" || baseName == "cplus_release") && call->getNumArgs() >= 1) {
        const Expr *arg0 = call->getArg(0)->IgnoreParenImpCasts();
        if (const auto *pref = dyn_cast<DeclRefExpr>(arg0))
          if (const auto *parm = dyn_cast<ParmVarDecl>(pref->getDecl()))
            userFrees_[currentFunction_].insert(parm->getFunctionScopeIndex());
      }
      if ((baseName == "free" || baseName == "cplus_release") && ptrVar) {
        if (releasedVars_.count(ptrVar->getNameAsString()))
          report(call->getBeginLoc(), "double free of '" +
                                          ptrVar->getNameAsString() + "'");
        else
          releasedVars_.insert(ptrVar->getNameAsString());
      }
      if (baseName == "realloc") {
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
      }
      if (baseName == "strcpy" && call->getNumArgs() >= 2) {
        checkStringCopy(call);
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
            releasedVars_.insert(vd->getNameAsString());
        }
      }
    }
    if (name == "pthread_create" && call->getNumArgs() >= 4) {
      const Expr *arg = call->getArg(3)->IgnoreParenImpCasts();
      if (const auto *uo = dyn_cast<UnaryOperator>(arg)) {
        if (uo->getOpcode() == UO_AddrOf) {
          const Expr *sub = uo->getSubExpr()->IgnoreParenImpCasts();
          if (const auto *ref = dyn_cast<DeclRefExpr>(sub))
            if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl()))
              if (vd->hasLocalStorage())
                warnAt(call->getBeginLoc(),
                       "stack borrow passed to thread: '" +
                           vd->getNameAsString() + "'");
        }
      }
    }
    return true;
  }

  bool VisitArraySubscriptExpr(ArraySubscriptExpr *subscript) {
    if (!report_)
      return true;
    Expr *base = subscript->getBase()->IgnoreParenImpCasts();
    QualType baseType = base->getType();
    const auto *arrayType = ctx_.getAsConstantArrayType(baseType);
    if (!arrayType)
      return true;
    Expr::EvalResult result;
    if (!subscript->getIdx()->EvaluateAsInt(result, ctx_) ||
        !result.Val.isInt())
      return true;
    llvm::APSInt index = result.Val.getInt();
    if (index.isNegative())
      return true;
    unsigned long long extent = arrayType->getSize().getZExtValue();
    unsigned long long value = index.getZExtValue();
    if (value >= extent)
      report(subscript->getBeginLoc(),
             "array index " + std::to_string(value) +
                 " exceeds length " + std::to_string(extent));
    return true;
  }

private:
  void report(SourceLocation loc, const std::string &message) {
    PresumedLoc ploc = ctx_.getSourceManager().getPresumedLoc(loc);
    Diag d;
    d.level = DiagLevel::Error;
    d.message = message;
    d.file = filename_;
    if (ploc.isValid()) {
      d.line = ploc.getLine();
      d.column = ploc.getColumn();
      if (std::string(ploc.getFilename()) != filename_)
        d.file = ploc.getFilename();
    }
    diags_.report(std::move(d));
  }

  ASTContext &ctx_;
  DiagnosticEngine &diags_;
  std::string filename_;
  bool report_;
  std::string currentFunction_;

public:
  std::set<std::string> handlers_;
  std::set<std::string> nonVolatileGlobals_;
  std::set<std::string> nullVars_;
  std::set<std::string> releasedVars_;
  std::map<std::string, std::set<int>> userFrees_;
};

} // namespace

void checkAstSafety(const std::string &loweredC, const std::string &filename,
                    const std::vector<std::string> &backendArgs,
                    DiagnosticEngine &diags) {
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
  check.setHandlers(collect.handlers());
  check.setGlobals(collect.nonVolatileGlobals());
  check.TraverseDecl(ast->getASTContext().getTranslationUnitDecl());

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
            d.message = msg;
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
