#include "Analysis/OwnershipAnalysis.h"

#include <clang/AST/ASTContext.h>
#include <clang/AST/Expr.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Analysis/CFG.h>
#include <clang/Basic/SourceManager.h>

#include <algorithm>
#include <deque>
#include <iterator>
#include <optional>
#include <set>
#include <vector>

using namespace clang;

namespace cplus {
namespace {

class LocalCollector : public RecursiveASTVisitor<LocalCollector> {
public:
  explicit LocalCollector(ASTContext &ctx) : ctx_(ctx) {}

  bool VisitUnaryOperator(UnaryOperator *uo) {
    if (uo->getOpcode() == UO_AddrOf) {
      const Expr *sub = uo->getSubExpr()->IgnoreParenImpCasts();
      if (const auto *ref = dyn_cast<DeclRefExpr>(sub))
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

private:
  ASTContext &ctx_;
  std::vector<const VarDecl *> vars_;
  std::set<const VarDecl *> addrTaken_;
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
        walk(bo->getRHS());
        markInitialized(bo->getLHS());
        return;
      }
      walk(bo->getLHS());
      walk(bo->getRHS());
      return;
    }
    if (const auto *uo = dyn_cast<UnaryOperator>(s)) {
      if (uo->isIncrementOp() || uo->isDecrementOp() ||
          uo->getOpcode() == UO_AddrOf) {
        // Taking an address (scanf, pthread_create, ...) is treated as a
        // potential output initialization; Clang's own warnings cover the
        // remaining direct uninitialized reads.
        markInitialized(uo->getSubExpr());
        return;
      }
      walk(uo->getSubExpr());
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
  const CfgReportFn &report_;
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

  const size_t n = cfg->size();
  std::vector<CFGBlock *> blocks(n, nullptr);
  for (CFGBlock *b : *cfg)
    if (b)
      blocks[b->getBlockID()] = b;
  std::vector<std::set<const VarDecl *>> in(n), out(n);
  std::vector<bool> reached(n, false);
  std::set<const Stmt *> reported;

  std::deque<size_t> work;
  work.push_back(cfg->getEntry().getBlockID());
  in[cfg->getEntry().getBlockID()] = entryInit;
  reached[cfg->getEntry().getBlockID()] = true;

  while (!work.empty()) {
    size_t id = work.front();
    work.pop_front();
    CFGBlock *block = blocks[id];
    if (!block)
      continue;
    std::set<const VarDecl *> cur = in[id];

    for (const CFGElement &el : *block) {
      std::optional<CFGStmt> stmt = el.getAs<CFGStmt>();
      if (!stmt)
        continue;
      DefInitWalker walker(ctx, tracked, cur, report, reported);
      walker.walk(stmt->getStmt());
    }
    out[id] = cur;

    for (const CFGBlock *succ : block->succs()) {
      if (!succ)
        continue;
      size_t sid = succ->getBlockID();
      if (!reached[sid]) {
        in[sid] = cur;
        reached[sid] = true;
        work.push_back(sid);
      } else {
        std::set<const VarDecl *> merged;
        std::set_intersection(in[sid].begin(), in[sid].end(), cur.begin(),
                              cur.end(),
                              std::inserter(merged, merged.begin()));
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
FunctionEffect libcEffect(llvm::StringRef name) {
  FunctionEffect e;
  if (name == "malloc" || name == "calloc") {
    e.allocates = true;
    e.returnsOwned = true;
  } else if (name == "realloc") {
    e.allocates = true;
    e.returnsOwned = true;
    e.frees = true; // only on success; see caller-side conservative handling
    e.freedArg = 0;
  } else if (name == "free" || name == "cplus_release") {
    e.frees = true;
    e.freedArg = 0;
  } else if (name == "memcpy" || name == "memmove" || name == "strcpy" ||
             name == "strcat" || name == "memset") {
    e.borrows = true;
    e.writes = true;
  } else if (name == "strlen" || name == "strcmp" || name == "fputs") {
    e.borrows = true;
  } else if (name == "fopen") {
    e.allocates = true;
    e.returnsOwned = true;
  } else if (name == "fclose") {
    e.frees = true;
    e.freedArg = 0;
  } else if (name == "fread" || name == "fwrite") {
    e.borrows = true;
    e.writes = true;
  }
  return e;
}
} // namespace cplus
