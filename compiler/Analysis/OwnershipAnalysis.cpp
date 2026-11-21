#include "Analysis/OwnershipAnalysis.h"

#include <clang/AST/ASTContext.h>
#include <clang/AST/Expr.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Analysis/CFG.h>
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
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
static std::map<std::string, FunctionEffect> &loadedContracts() {
  static std::map<std::string, FunctionEffect> contracts;
  static bool attempted = false;
  if (attempted)
    return contracts;
  attempted = true;

  std::string path = "compiler/Analysis/libc_contracts.json";
#ifdef CPLUS_CONTRACT_PATH
  std::string installed = CPLUS_CONTRACT_PATH;
#elif defined(CPLUS_SOURCE_DIR)
  std::string installed = std::string(CPLUS_SOURCE_DIR) +
                          "/compiler/Analysis/libc_contracts.json";
  if (llvm::sys::fs::exists(installed))
    path = installed;
#endif
  auto buf = llvm::MemoryBuffer::getFile(path);
  if (!buf) {
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
  if (!buf)
    return contracts;
  auto parsed = llvm::json::parse((*buf)->getBuffer());
  if (!parsed) {
    llvm::consumeError(parsed.takeError());
    return contracts;
  }
  const llvm::json::Object *root = parsed->getAsObject();
  if (!root)
    return contracts;
  const llvm::json::Object *functions = root->getObject("functions");
  if (!functions)
    return contracts;
  for (const auto &entry : *functions) {
    const llvm::json::Object *spec = entry.second.getAsObject();
    if (!spec)
      continue;
    FunctionEffect e;
    auto effectList = spec->getArray("effects");
    if (effectList)
      for (const auto &v : *effectList) {
        auto s = v.getAsString();
        if (!s) continue;
        if (*s == "allocates") e.allocates = true;
        else if (*s == "frees") e.frees = true;
        else if (*s == "frees-on-success") e.frees = true;
        else if (*s == "writes") e.writes = true;
        else if (*s == "reads") e.borrows = true;
      }
    auto ownership = spec->getString("ownership");
    if (ownership == "returns_owned") e.returnsOwned = true;
    e.category = "CPLUS-SAFE-CRT";
    contracts[entry.first.str()] = e;
  }
  return contracts;
}

FunctionEffect libcEffect(llvm::StringRef name) {
  auto &contracts = loadedContracts();
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
    e.frees = true; // only on success; branch-sensitive handling is future work
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
} // namespace cplus
