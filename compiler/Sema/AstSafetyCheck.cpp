#include "Sema/AstSafetyCheck.h"

#include "Diagnostics.h"

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
    if (report_)
      nullVars_.clear();
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
      if (handlers_.count(currentFunction_))
        reportGlobalWrite(op->getLHS(), op->getBeginLoc());
    }
    return true;
  }

  bool VisitUnaryOperator(UnaryOperator *op) {
    if (!report_)
      return true;
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

  bool VisitCallExpr(CallExpr *call) {
    const FunctionDecl *callee = call->getDirectCallee();
    if (!callee)
      return true;
    std::string name = callee->getNameAsString();

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
};

} // namespace

void checkAstSafety(const std::string &loweredC, const std::string &filename,
                    const std::vector<std::string> &backendArgs,
                    DiagnosticEngine &diags) {
  // Keep only parse-relevant flags; sanitizer and warning-as-error flags can
  // make the in-memory AST parse fail for unrelated reasons.
  std::vector<std::string> args;
  for (size_t i = 0; i < backendArgs.size(); ++i) {
    const auto &arg = backendArgs[i];
    if (arg == "-isysroot") {
      ++i; // skip the SDK path too: it is not a parse-relevant flag
      continue;
    }
    if (arg.rfind("-isysroot=", 0) == 0)
      continue;
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
}

} // namespace cplus
