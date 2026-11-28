#include "Sema/AstSafetyCheck.h"

#include "Diagnostics.h"
#include "Analysis/OwnershipAnalysis.h"
#include "Sema/SemaCPlus.h"

#include <llvm/ADT/StringRef.h>
#include <llvm/Support/JSON.h>
#include <llvm/Support/MemoryBuffer.h>

#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/Expr.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/ParentMapContext.h>
#include <clang/AST/PrettyPrinter.h>
#include <clang/Analysis/CFG.h> // issue #180: forward/backward CFG dataflow
#include <clang/Basic/SourceManager.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Frontend/PCHContainerOperations.h>
#include <clang/Lex/Lexer.h>

#include <clang/Frontend/ASTUnit.h>
#include <clang/Rewrite/Core/Rewriter.h>
#include <clang/Tooling/Tooling.h>

#include <llvm/Support/raw_ostream.h>

#include <algorithm>
#include <cstdlib>
#include <climits>
#include <cstdint>
#include <cmath>
#include <cmath>
#include <deque>
#include <functional>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace clang;

namespace cplus {

// Issue #179 — Cake qualifier facts recorded by CPlusRewriter.cpp and the
// ownership-effect summaries in OwnershipAnalysis.cpp. Primitive parameter
// types keep this boundary header-free; see the definitions in those files.
void cplusCakeClearQualifiers();
void cplusCakeRecordQualifier(const std::string &name, unsigned line, int kind);
int cplusCakeQualifierOf(const std::string &name, unsigned line);
std::vector<unsigned> cplusCakeMoveSites(const std::string &name);
bool cplusCakeEffectConsumesArg(const std::string &fn, int arg);

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

/// getZExtValue()/getSExtValue() assert for values wider than 64 bits
/// (e.g. `__int128`). All narrowing call sites must check this first.
inline bool fits64(const llvm::APSInt &v) { return v.getBitWidth() <= 64; }

/// Add `delta * scale` to a signed byte offset without signed overflow.
inline bool addScaledOffset(long long &offset, long long delta,
                            long long scale) {
  if (scale <= 0)
    return false;
  if (delta > 0 && delta > LLONG_MAX / scale)
    return false;
  if (delta < 0 && delta < LLONG_MIN / scale)
    return false;
  const long long scaled = delta * scale;
  if (scaled > 0 && offset > LLONG_MAX - scaled)
    return false;
  if (scaled < 0 && offset < LLONG_MIN - scaled)
    return false;
  offset += scaled;
  return true;
}

/// Compute `base + delta * scale` with an explicit overflow check. Returns
/// false on overflow so callers can reject the offset as out of bounds.
inline bool mulAddFits(unsigned long long base, unsigned long long delta,
                       unsigned long long scale, unsigned long long &out) {
  if (scale == 0 || delta > (ULLONG_MAX - base) / scale)
    return false;
  out = base + delta * scale;
  return true;
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

/// (argument index, field width) for every `%s`-style scanf conversion.
/// A negative width means unbounded. Suppressed assignments (`%*s`) are
/// skipped so argument indices stay aligned with the variadic arguments.
inline std::vector<std::pair<unsigned, int>>
scanfStringWidths(const StringLiteral *fmt) {
  std::vector<std::pair<unsigned, int>> out;
  if (!fmt)
    return out;
  llvm::StringRef s = fmt->getBytes();
  unsigned arg = 0;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] != '%')
      continue;
    if (++i >= s.size())
      break;
    if (s[i] == '%')
      continue;
    bool suppress = false;
    if (s[i] == '*') {
      suppress = true;
      ++i;
    }
    int width = 0;
    bool haveWidth = false;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
      width = width * 10 + (s[i] - '0');
      haveWidth = true;
      ++i;
    }
    while (i < s.size() &&
           (s[i] == 'h' || s[i] == 'l' || s[i] == 'L' || s[i] == 'z' ||
            s[i] == 'j' || s[i] == 't'))
      ++i;
    if (i >= s.size())
      break;
    const char conv = s[i];
    if (!suppress && (conv == 's' || conv == 'S'))
      out.push_back({arg, haveWidth ? width : -1});
    if (!suppress)
      ++arg;
  }
  return out;
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
  std::set<const FunctionDecl *> seen_;
  explicit GlobalWriteFinder(const std::set<std::string> &g,
                             const FunctionDecl *entry = nullptr)
      : globals(g) {
    if (entry)
      seen_.insert(entry->getCanonicalDecl());
  }

  bool synchronized = false;

  bool VisitCallExpr(CallExpr *c) {
    if (!c->getDirectCallee())
      return true;
    // Thread functions frequently delegate the actual mutation to a helper.
    // A name-only check on the entry body would miss that write entirely and
    // report a clean check. Follow same-TU helper bodies with a visited set;
    // declarations without a body stay opaque (no invented effect summary).
    if (const FunctionDecl *callee = c->getDirectCallee()) {
      const FunctionDecl *canon = callee->getCanonicalDecl();
      if (callee->getBody() && !seen_.count(canon)) {
        seen_.insert(canon);
        TraverseStmt(const_cast<Stmt *>(callee->getBody()));
      }
    }
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

/// Inferred arithmetic effect of a tiny helper returning a binary operation
/// on two parameters (e.g. `int add(int a,int b){ return a+b; }`).
struct ArithEffect {
  BinaryOperator::Opcode op = BO_Add;
  int lhs = -1, rhs = -1;
  unsigned width = 0;
};

/// Where an inferred extent came from.  Kept with the extent so a diagnostic
/// can name the object correctly instead of calling every tracked extent a
/// "heap allocation".
enum class ExtentOrigin : unsigned char { Unknown, Heap, Stack, Global };

static const char *extentLabel(ExtentOrigin o) {
  switch (o) {
  case ExtentOrigin::Heap:
    return "heap allocation";
  case ExtentOrigin::Stack:
    return "stack array";
  case ExtentOrigin::Global:
    return "global array";
  default:
    return "object";
  }
}

struct ParamExtent {
  long long bytes = -1;
  ExtentOrigin origin = ExtentOrigin::Unknown;
};

/// An extent concretized at a call site and bound to the callee's *argument
/// slot* rather than to a variable spelling.  This adapts the idea of
/// Checked C function-type bounds (clang/lib/Sema/SemaBounds.cpp,
/// Sema::AbstractForFunctionType / ConcretizeFromFunctionTypeWithArgs and the
/// CheckBoundsDeclAtCallArg call loop) together with Apple's pointer/count
/// pairing (clang/lib/Sema/DynamicCountPointerAssignmentAnalysis.cpp,
/// computeMemberExprKey / TransformDynamicCountWithFunctionArgument): the
/// object slot that carries the pointer also owns its extent, so an explicit
/// cast or a local alias cannot erase it.  bytes < 0 means unknown or that
/// different call sites supplied conflicting extents (fail closed).
struct SlotExtent {
  long long bytes = -1;
  ExtentOrigin origin = ExtentOrigin::Unknown;
};

/// (callee function, parameter slot) identifies an argument slot.  The slot
/// survives casts because the callee side is matched by expression shape and
/// parameter index, never by the parameter's spelling.
using ArgSlotKey = std::pair<std::string, unsigned>;
using IndirectSlotExtents = std::map<ArgSlotKey, SlotExtent>;
using FieldSlotExtents =
    std::map<ArgSlotKey, std::map<const FieldDecl *, SlotExtent>>;

/// A per-call-site fact pairing a pointer argument slot with the integer
/// argument slot that supplies its element count.  Checked C's
/// Sema::ConcretizeFromFunctionTypeWithArgs (clang/lib/Sema/SemaBounds.cpp)
/// concretizes function-type bounds from each call's argument list instead of
/// collapsing all callers to one unknown extent; we do the same here and keep
/// every site's (pointer slot, count slot, actual extent) triple.  `count < 0`
/// or `countSlot == UINT_MAX` marks a site where no companion count could be
/// proven, which disables the pairing for that slot (fail closed).
struct CallSiteExtentFact {
  static constexpr unsigned kNoCountSlot = ~0u;
  unsigned ptrSlot = 0;
  unsigned countSlot = 0;
  long long extentBytes = -1;
  long long count = -1;
  bool exceeds = false; // declared count * element size > actual extent
  bool operator<(const CallSiteExtentFact &o) const {
    if (ptrSlot != o.ptrSlot) return ptrSlot < o.ptrSlot;
    if (countSlot != o.countSlot) return countSlot < o.countSlot;
    if (extentBytes != o.extentBytes) return extentBytes < o.extentBytes;
    if (count != o.count) return count < o.count;
    return exceeds < o.exceeds;
  }
};
using CallSiteFacts = std::map<std::string, std::set<CallSiteExtentFact>>;
/// (callee, pointer slot) -> companion element-count slot, derived only when
/// every recorded call site pairs the same slots and stays within its extent.
using CountPairedParams = std::map<ArgSlotKey, unsigned>;

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
  void setArithEffects(
      const std::map<std::string, ArithEffect> &e) { arithEffects_ = e; }
  void setConstReturnValues(const std::map<std::string, long long> &m) {
    constReturnValues_ = m;
  }
  const std::map<std::string, long long> &constReturnValues() const {
    return constReturnValues_;
  }
  const std::map<std::string, ArithEffect> &arithEffects() const {
    return arithEffects_;
  }
  void setStrict(bool strict) { strict_ = strict; }
  void setRecordArithmeticSites(bool v) { recordArithmeticSites_ = v; }
  std::vector<CheckedArithmeticSite> takeArithmeticSites() {
    return std::move(arithmeticSites_);
  }
  std::vector<CheckedPointerSite> takePointerSites() {
    return std::move(pointerSites_);
  }
  std::vector<CheckedAllocSite> takeAllocSites() {
    return std::move(allocSites_);
  }
  std::vector<CheckedAllocSite> takeFreeSites() {
    return std::move(freeSites_);
  }
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
  void setHandledReallocVars(const std::set<std::string> &s) {
    handledReallocVars_ = s;
  }
  const std::set<std::string> &handledReallocVars() const {
    return handledReallocVars_;
  }
  void setReallocAssignCalls(
      const std::map<const VarDecl *, const CallExpr *> &m) {
    reallocAssignCalls_ = m;
  }
  const std::map<const VarDecl *, const CallExpr *> &reallocAssignCalls()
      const {
    return reallocAssignCalls_;
  }
  void setConstLocals(const std::map<std::string, llvm::APSInt> &m) { constLocals_ = m; }
  const std::map<std::string, llvm::APSInt> &constLocals() const { return constLocals_; }
  void setConstFloatLocals(const std::map<std::string, double> &m) {
    constFloatLocals_ = m;
  }
  const std::map<std::string, double> &constFloatLocals() const {
    return constFloatLocals_;
  }
  const std::set<std::string> &addressTakenVars() const {
    return addressTakenVars_;
  }
  void setAddressTakenVars(const std::set<std::string> &s) {
    addressTakenVars_ = s;
  }
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
  const std::set<std::pair<std::string, unsigned>> &derefParams() const {
    return derefParams_;
  }
  void setDerefParams(const std::set<std::pair<std::string, unsigned>> &s) {
    derefParams_ = s;
  }
  const std::set<std::pair<std::string, unsigned>> &paramWriteSlots() const {
    return paramWriteSlots_;
  }
  void setParamWriteSlots(const std::set<std::pair<std::string, unsigned>> &s) {
    paramWriteSlots_ = s;
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
  const std::map<std::string, long long> &stackArrayExtents() const {
    return stackArrayExtents_;
  }
  void setStackArrayExtents(const std::map<std::string, long long> &m) {
    stackArrayExtents_ = m;
  }
  const std::map<std::string, std::map<unsigned, ParamExtent>> &
  callParamExtents() const {
    return callParamExtents_;
  }
  const std::set<std::pair<std::string, unsigned>> &conflictedParams() const {
    return conflictedParams_;
  }
  void setConflictedParams(
      const std::set<std::pair<std::string, unsigned>> &s) {
    conflictedParams_ = s;
  }
  void setCallParamExtents(
      const std::map<std::string, std::map<unsigned, ParamExtent>> &m) {
    callParamExtents_ = m;
  }
  const CallSiteFacts &callSiteFacts() const { return callSiteFacts_; }
  void setCallSiteFacts(const CallSiteFacts &m) { callSiteFacts_ = m; }
  const IndirectSlotExtents &indirectSlotExtents() const {
    return indirectSlotExtents_;
  }
  void setIndirectSlotExtents(const IndirectSlotExtents &m) {
    indirectSlotExtents_ = m;
  }
  const FieldSlotExtents &fieldSlotExtents() const { return fieldSlotExtents_; }
  void setFieldSlotExtents(const FieldSlotExtents &m) { fieldSlotExtents_ = m; }
  void setReturnedParams(const std::map<std::string, unsigned> &m) {
    returnedParam_ = m;
  }
  const std::map<std::string, unsigned> &returnedParams() const {
    return returnedParam_;
  }
  void setReturnedOutParams(
      const std::map<std::string, std::map<unsigned, unsigned>> &m) {
    returnedOutParam_ = m;
  }
  const std::map<std::string, std::map<unsigned, unsigned>> &
  returnedOutParams() const {
    return returnedOutParam_;
  }
  void setReturnedOutAllocs(
      const std::map<std::string, std::map<unsigned, long long>> &m) {
    returnedOutAlloc_ = m;
  }
  const std::map<std::string, std::map<unsigned, long long>> &
  returnedOutAllocs() const {
    return returnedOutAlloc_;
  }
  void setUnionVars(const std::set<std::string> &s) { unionVars_ = s; }
  const std::set<std::string> &unionVars() const { return unionVars_; }
  void setFnPtrVarTargets(const std::map<std::string, std::string> &m) {
    fnPtrVarTargets_ = m;
  }
  void setFnPtrParamTargets(
      const std::map<std::string, std::map<unsigned, std::set<std::string>>> &m) {
    fnPtrParamTargets_ = m;
  }
  const std::map<std::string, std::string> &fnPtrVarTargets() const {
    return fnPtrVarTargets_;
  }
  const std::map<std::string, std::map<unsigned, std::set<std::string>>> &
  fnPtrParamTargets() const {
    return fnPtrParamTargets_;
  }
  void setGlobalPointsTo(const std::map<std::string, std::set<std::string>> &m) {
    globalPointsTo_ = m;
  }
  const std::map<std::string, std::set<std::string>> &globalPointsTo() const {
    return globalPointsTo_;
  }
  void setReturnedFixedExtents(const std::map<std::string, long long> &m) {
    returnedFixedExtent_ = m;
  }
  const std::map<std::string, long long> &returnedFixedExtents() const {
    return returnedFixedExtent_;
  }
  const std::map<std::string, long long> &returnedAllocExtents() const {
    return returnedAllocExtent_;
  }
  void setReturnedAllocExtents(const std::map<std::string, long long> &m) {
    returnedAllocExtent_ = m;
  }
  const std::map<unsigned, long long> &callReturnExtent() const {
    return callReturnExtent_;
  }
  void setCallReturnExtent(const std::map<unsigned, long long> &m) {
    callReturnExtent_ = m;
  }
  const std::set<std::string> &allGlobals() const { return allGlobals_; }
  const std::map<std::string, int> &threadEntryCounts() const {
    return threadEntryCounts_;
  }
  const std::map<std::string, SourceLocation> &threadEntryLocs() const {
    return threadEntryLocs_;
  }
  const std::map<std::string, int> &threadSharedArgCounts() const {
    return threadSharedArgCounts_;
  }
  const std::map<std::string, SourceLocation> &threadSharedArgLocs() const {
    return threadSharedArgLocs_;
  }
  bool hasLockCalls() const { return tuHasLockCalls_; }
  void setThreadSharedArgFacts(const std::map<std::string, int> &counts,
                               const std::map<std::string, SourceLocation> &locs,
                               bool locks) {
    threadSharedArgCounts_ = counts;
    threadSharedArgLocs_ = locs;
    tuHasLockCalls_ = locks;
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
      return result.Val.getInt().isZero();
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

  /// Mutex arguments are normally `&m`; resolve the underlying declaration
  /// rather than relying on the token spelling of the address expression.
  std::string lockNameOf(const Expr *expr) const {
    if (!expr)
      return std::string();
    const Expr *e = expr->IgnoreParenImpCasts();
    if (const auto *uo = dyn_cast<UnaryOperator>(e))
      if (uo->getOpcode() == UO_AddrOf)
        e = uo->getSubExpr()->IgnoreParenImpCasts();
    return varNameOf(e);
  }

  static bool isMutexLockCall(llvm::StringRef n) {
    return n.contains("mutex_lock") && !n.contains("unlock") &&
           !n.contains("trylock") && !n.contains("timedlock");
  }
  static bool isMutexUnlockCall(llvm::StringRef n) {
    return n.contains("mutex_unlock");
  }

  /// Like varNameOf, but also looks through explicit C-style casts and
  /// no-op pointer offsets. A `(void *)p` alias, and a `p + 0` / `0 + p`
  /// rebinding, must inherit p's release/extent state; without the canonical
  /// form a zero-offset alias escapes the alias and UAF checks.
  std::string castedVarName(const Expr *expr) const {
    if (!expr)
      return std::string();
    return varNameOf(canonicalPointer(expr));
  }

  /// Find the original pointer variable behind an integer-laundering
  /// expression such as `(uintptr_t)p`, `(uintptr_t)p | 2` or a tracked int.
  std::string pointerSourceOf(const Expr *e) const {
    if (!e)
      return std::string();
    e = e->IgnoreParenImpCasts();
    // Macro expansions reach the AST as statement expressions (and sometimes
    // as identity helpers). Peel those wrappers so the provenance integer
    // inside the macro is still visible; do not strip explicit casts here,
    // they are the laundering shape we are looking for.
    if (const auto *se = dyn_cast<StmtExpr>(e)) {
      const CompoundStmt *cs = se->getSubStmt();
      if (cs && cs->size() != 0)
        if (const auto *last = dyn_cast<Expr>(cs->body_back()))
          return pointerSourceOf(last);
    }
    if (const auto *cast = dyn_cast<CastExpr>(e)) {
      if (cast->getType()->isIntegerType())
        return castedVarName(cast->getSubExpr());
      // A pointer cast is transparent only when its operand jumped through an
      // integer: `(int *)(uintptr_t)p`. A plain `(int *)p` alias is handled by
      // the ordinary name/alias paths and must not acquire a new source here.
      if (cast->getType()->isPointerType()) {
        const Expr *sub = cast->getSubExpr()->IgnoreParenImpCasts();
        if (const auto *ice = dyn_cast<CastExpr>(sub))
          if (ice->getType()->isIntegerType())
            return pointerSourceOf(sub);
      }
    }
    if (const auto *call = dyn_cast<CallExpr>(e))
      for (const auto &n : calleeNames(call, call->getDirectCallee())) {
        auto rp = returnedParam_.find(n);
        if (rp != returnedParam_.end() && rp->second < call->getNumArgs()) {
          std::string src = pointerSourceOf(call->getArg(rp->second));
          if (!src.empty())
            return src;
        }
      }
    if (const auto *bo = dyn_cast<BinaryOperator>(e))
      if (bo->isBitwiseOp() || bo->isAdditiveOp()) {
        // Tagged (`u | 2`) and re-based (`u + 8`) integer views of p keep p's
        // provenance; both are integer-laundering forms, not fresh pointers.
        std::string a = pointerSourceOf(bo->getLHS());
        return a.empty() ? pointerSourceOf(bo->getRHS()) : a;
      }
    if (const auto *ref = dyn_cast<DeclRefExpr>(e))
      if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl())) {
        auto it = provenanceSource_.find(vd->getNameAsString());
        if (it != provenanceSource_.end())
          return it->second;
      }
    return std::string();
  }

  /// True when `e` is a pointer produced by a pointer->integer->pointer
  /// round-trip, e.g. the `(int *)(uintptr_t)a` inside the LAUNDER macro.
  /// Plain pointer casts (`(int *)p`) and plain pointer arithmetic do not
  /// qualify, so existing alias/offset rules keep handling those shapes.
  static bool isCastLaunderedPointer(const Expr *e, unsigned depth = 0) {
    if (!e || depth > 8)
      return false;
    const Expr *x = e->IgnoreParenImpCasts();
    while (const auto *ce = dyn_cast<ExplicitCastExpr>(x)) {
      if (ce->getType()->isPointerType()) {
        x = ce->getSubExpr()->IgnoreParenImpCasts();
        continue;
      }
      if (ce->getType()->isIntegerType())
        return true;
      break;
    }
    return false;
  }

  /// Byte extent of the object named by a provenance source, whether it came
  /// from a fixed stack array or from a tracked allocation.
  long long provenanceByteExtent(const std::string &src) const {
    if (src.empty())
      return -1;
    auto sit = stackArrayExtents_.find(src);
    if (sit != stackArrayExtents_.end() && sit->second > 0)
      return sit->second;
    auto hit = heapAllocs_.find(src);
    if (hit != heapAllocs_.end() && hit->second.bytes > 0)
      return hit->second.bytes;
    auto vit = namedVars_.find(src);
    if (vit != namedVars_.end())
      if (const HeapAllocState *st = heapStateOfVar(vit->second))
        if (st->bytes > 0)
          return st->bytes;
    return -1;
  }

  /// True when `e` is an integer expression that carries pointer provenance:
  /// `(uintptr_t)p`, a tracked integer variable (`u` from
  /// `uintptr_t u = (uintptr_t)p`), or a tagged/offset form (`u | 2`,
  /// `u & ~1`, `u + 8`) built from one.  Only the expression *shape* is
  /// decided here; liveness is answered by isReleased()/releasedVars_ at the
  /// release call site, so `(uintptr_t)` comparisons/hashing are unaffected.
  bool isIntegerProvenanceExpr(const Expr *e, unsigned depth = 0) const {
    if (!e || depth > 8)
      return false;
    const Expr *x = e->IgnoreParenImpCasts();
    if (const auto *ce = dyn_cast<CastExpr>(x)) {
      if (ce->getType()->isIntegerType()) {
        const Expr *sub = ce->getSubExpr()->IgnoreParenImpCasts();
        if (sub && sub->getType()->isPointerType())
          return true; // (uintptr_t)p / (intptr_t)p
        return isIntegerProvenanceExpr(sub, depth + 1);
      }
      if (ce->getType()->isPointerType())
        return isIntegerLaunderedPointer(x, depth + 1);
      return false;
    }
    if (const auto *ref = dyn_cast<DeclRefExpr>(x)) {
      const auto *vd = dyn_cast<VarDecl>(ref->getDecl());
      if (vd) {
        const std::string &n = vd->getNameAsString();
        return provenanceInts_.count(n) != 0 || provenanceSource_.count(n) != 0;
      }
    }
    if (const auto *bo = dyn_cast<BinaryOperator>(x))
      return isIntegerProvenanceExpr(bo->getLHS(), depth + 1) ||
             isIntegerProvenanceExpr(bo->getRHS(), depth + 1);
    if (const auto *uo = dyn_cast<UnaryOperator>(x))
      return isIntegerProvenanceExpr(uo->getSubExpr(), depth + 1);
    if (const auto *co = dyn_cast<ConditionalOperator>(x))
      return isIntegerProvenanceExpr(co->getTrueExpr(), depth + 1) ||
             isIntegerProvenanceExpr(co->getFalseExpr(), depth + 1);
    return false;
  }

  /// True when `e` is a pointer produced by casting a provenance integer back
  /// to a pointer: `(int *)u`, `(int *)((uintptr_t)p | 1)`,
  /// `(int *)(void *)(uintptr_t)p`, etc. Casts and parens are stripped in any
  /// order. Combined with isReleased() this catches the F2 stale-alias pattern
  /// where the integer address can be reused for a different allocation.
  bool isIntegerLaunderedPointer(const Expr *e, unsigned depth = 0) const {
    if (!e || depth > 8)
      return false;
    const Expr *x = e->IgnoreParenImpCasts();
    if (const auto *ce = dyn_cast<CastExpr>(x)) {
      if (ce->getType()->isPointerType())
        return isIntegerLaunderedPointer(ce->getSubExpr(), depth + 1);
      if (ce->getType()->isIntegerType())
        return isIntegerProvenanceExpr(x, depth + 1);
      return false;
    }
    if (x->getType()->isIntegerType())
      return isIntegerProvenanceExpr(x, depth + 1);
    return false;
  }

  /// Resolve the *released* pointer source behind an integer-laundered
  /// pointer argument. isReleased() only looks through casts to a variable
  /// name, so an inline tagged form (`(int *)((uintptr_t)p | 1)`) needs this
  /// recursive walk. An empty result means the integer did not come from a
  /// pointer released earlier in this TU.
  std::string staleIntegerAliasSource(const Expr *e, unsigned depth = 0) const {
    if (!e || depth > 8)
      return std::string();
    const Expr *x = e->IgnoreParenImpCasts();
    if (const auto *ce = dyn_cast<CastExpr>(x)) {
      if (ce->getType()->isIntegerType()) {
        std::string src = pointerSourceOf(x); // (uintptr_t)p -> p
        if (!src.empty())
          return releasedVars_.count(src) ? src : std::string();
        // Offset/tagged pointer operand, e.g. `(uintptr_t)(p + 1)`.
        return staleIntegerAliasSource(ce->getSubExpr(), depth + 1);
      }
      if (ce->getType()->isPointerType())
        return staleIntegerAliasSource(ce->getSubExpr(), depth + 1);
      return std::string();
    }
    if (const auto *ref = dyn_cast<DeclRefExpr>(x)) {
      const auto *vd = dyn_cast<VarDecl>(ref->getDecl());
      if (!vd)
        return std::string();
      const std::string &n = vd->getNameAsString();
      // markReleased() propagates the taint to provenance integers as well.
      if (releasedVars_.count(n))
        return n;
      auto it = provenanceSource_.find(n);
      if (it != provenanceSource_.end() && releasedVars_.count(it->second))
        return it->second;
      return std::string();
    }
    if (const auto *bo = dyn_cast<BinaryOperator>(x)) {
      std::string a = staleIntegerAliasSource(bo->getLHS(), depth + 1);
      if (!a.empty())
        return a;
      return staleIntegerAliasSource(bo->getRHS(), depth + 1);
    }
    if (const auto *uo = dyn_cast<UnaryOperator>(x))
      return staleIntegerAliasSource(uo->getSubExpr(), depth + 1);
    if (const auto *co = dyn_cast<ConditionalOperator>(x)) {
      std::string a = staleIntegerAliasSource(co->getTrueExpr(), depth + 1);
      return a.empty() ? staleIntegerAliasSource(co->getFalseExpr(), depth + 1)
                       : a;
    }
    return std::string();
  }

  /// ABI constraint rationale: plain C pointers must stay bare addresses so
  /// they keep their size/layout across the C ABI, the retail executable and
  /// libc. A pointer-borne liveness token (fat pointer) would change that ABI
  /// and cannot be used here. The stale free therefore cannot be caught at
  /// runtime; checkStaleIntegerAliasArgs() rejects its source pattern at
  /// compile time instead.
  void checkStaleIntegerAliasArgs(const CallExpr *call,
                                  const std::string &name,
                                  const FunctionDecl *callee) {
    if (!report_ || !call)
      return;
    std::set<unsigned> slots;
    auto addReleaseApi = [&](const std::string &n) {
      if (n == "free" || n == "cplus_release" ||
          n == "cplus_free_registered" || n == "free_s")
        slots.insert(0);
    };
    addReleaseApi(name);
    for (const auto &target : calleeNames(call, callee)) {
      addReleaseApi(target);
      auto uit = userFrees_.find(target); // wrappers that release a parameter
      if (uit != userFrees_.end())
        for (int idx : uit->second)
          if (idx >= 0)
            slots.insert((unsigned)idx);
    }
    FunctionEffect contract = libcEffect(name);
    for (int idx : contract.freeArgs)
      if (idx >= 0)
        slots.insert((unsigned)idx);
    for (unsigned idx : slots) {
      if (idx >= call->getNumArgs())
        continue;
      const Expr *arg = call->getArg(idx);
      if (!isIntegerLaunderedPointer(arg) ||
          (!isReleased(arg) && staleIntegerAliasSource(arg).empty()))
        continue;
      reportStaleIntegerAlias(call->getBeginLoc());
    }
  }

  /// `&local` where `local` is an automatic variable of the current function
  /// (a static local, global, parameter or `&local.field` does not qualify).
  /// Returning/storing such an address outlives the variable's frame.
  const VarDecl *automaticLocalOfAddress(const Expr *e) const {
    if (!e)
      return nullptr;
    const Expr *x = e->IgnoreParenImpCasts();
    const auto *uo = dyn_cast<UnaryOperator>(x);
    if (!uo || uo->getOpcode() != UO_AddrOf)
      return nullptr;
    const Expr *sub = uo->getSubExpr()->IgnoreParenCasts();
    const auto *ref = dyn_cast<DeclRefExpr>(sub);
    const auto *vd = ref ? dyn_cast<VarDecl>(ref->getDecl()) : nullptr;
    if (!vd || isa<ParmVarDecl>(vd) || !vd->hasLocalStorage())
      return nullptr;
    return vd;
  }

  /// True when an aggregate initializer contains an `&local` element (also
  /// looks into nested init lists such as `struct S s = { { &x } };`).
  bool exprContainsLocalAddress(const Expr *e, unsigned depth = 0) const {
    if (!e || depth > 6)
      return false;
    if (automaticLocalOfAddress(e))
      return true;
    if (const auto *il = dyn_cast<InitListExpr>(e))
      for (unsigned i = 0; i < il->getNumInits(); ++i)
        if (exprContainsLocalAddress(il->getInit(i), depth + 1))
          return true;
    return false;
  }

  /// True when `e` is a plain integer constant with no pointer provenance,
  /// e.g. `0xdeadbeefULL` / `(uintptr_t)0xdeadbeefULL`.  Pointer-derived
  /// integers such as `(uintptr_t)p` are excluded so legitimate pointer stores
  /// into a union are not mistaken for a forged address.
  bool isPlainIntegerConstantExpr(const Expr *e) const {
    if (!e || isIntegerProvenanceExpr(e))
      return false;
    Expr::EvalResult r;
    return e->EvaluateAsInt(r, ctx_) && r.Val.isInt();
  }

  /// If `e` is `u.f` where `u` is a union and `f` is a non-pointer member,
  /// return the union variable name; otherwise empty.
  std::string unionNonPointerMemberVar(const Expr *e) const {
    const Expr *x = e ? e->IgnoreParenImpCasts() : nullptr;
    const auto *me = x ? dyn_cast<MemberExpr>(x) : nullptr;
    if (!me || me->getMemberDecl()->getType()->isPointerType())
      return std::string();
    const Expr *base = me->getBase()->IgnoreParenCasts();
    const auto *ref = dyn_cast<DeclRefExpr>(base);
    const auto *vd = ref ? dyn_cast<VarDecl>(ref->getDecl()) : nullptr;
    if (!vd || !vd->getType()->isUnionType())
      return std::string();
    return vd->getNameAsString();
  }

  /// If `e` is `u.ptr` where `u` is a union whose integer member was forged
  /// with a plain constant, return the member expression for diagnosis.
  const MemberExpr *forgedUnionPointerMember(const Expr *e) const {
    const Expr *x = e ? e->IgnoreParenImpCasts() : nullptr;
    const auto *me = x ? dyn_cast<MemberExpr>(x) : nullptr;
    if (!me || !me->getMemberDecl()->getType()->isPointerType())
      return nullptr;
    const Expr *base = me->getBase()->IgnoreParenCasts();
    const auto *ref = dyn_cast<DeclRefExpr>(base);
    const auto *vd = ref ? dyn_cast<VarDecl>(ref->getDecl()) : nullptr;
    if (!vd || !forgedUnionVars_.count(vd->getNameAsString()))
      return nullptr;
    return me;
  }

  /// Direct union-member deref (`*u.ptr`) or a local pointer copied from such
  /// a member (`int *q = u.ptr; *q`).  Comparisons/hashing of `u.ptr` are
  /// deliberately not matched.
  const Expr *forgedUnionPointerSource(const Expr *e) const {
    if (const MemberExpr *me = forgedUnionPointerMember(e))
      return me;
    const Expr *x = e ? e->IgnoreParenImpCasts() : nullptr;
    if (const auto *ref = x ? dyn_cast<DeclRefExpr>(x) : nullptr)
      if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl()))
        if (forgedPointerVars_.count(vd->getNameAsString()))
          return x;
    return nullptr;
  }

  /// Name of the variable addressed by an out-parameter argument (`&q` -> q).
  std::string varNameOfAddressArg(const Expr *e) const {
    if (!e)
      return std::string();
    const Expr *x = e->IgnoreParenImpCasts();
    if (const auto *uo = dyn_cast<UnaryOperator>(x))
      if (uo->getOpcode() == UO_AddrOf)
        x = uo->getSubExpr()->IgnoreParenImpCasts();
    return castedVarName(x);
  }

  /// Merge two names into one alias group (used by out-params and provenance
  /// round-trips so releasing one invalidates every view of the object).
  void linkAlias(const std::string &a, const std::string &b) {
    if (a.empty() || b.empty() || a == b)
      return;
    auto &ga = aliasGroups_[a];
    auto gb = aliasGroups_[b];
    ga.insert(a);
    ga.insert(b);
    ga.insert(gb.begin(), gb.end());
    for (const auto &name : ga)
      aliasGroups_[name] = ga;
    aliasLocs_[a] = aliasLocs_[b] = currentFnDecl_ ? currentFnDecl_->getBeginLoc()
                                                    : SourceLocation();
  }

  /// `memcpy(&q, &p, sizeof p)` copies pointer bytes, so q and p refer to the
  /// same object.  Propagate the alias/release state instead of treating the
  /// copy as an opaque byte write (integer/pointer-byte laundering UAF).
  void propagatePointerBytes(const CallExpr *call) {
    if (call->getNumArgs() < 3)
      return;
    Expr::EvalResult r;
    if (!call->getArg(2)->EvaluateAsInt(r, ctx_) || !r.Val.isInt() ||
        !fits64(r.Val.getInt()))
      return;
    unsigned long long ptrBytes = ctx_.getTypeSize(ctx_.VoidPtrTy) / 8u;
    if (r.Val.getInt().getZExtValue() != ptrBytes || ptrBytes == 0)
      return;
    auto addressedPointer = [&](unsigned i) -> bool {
      const Expr *x = call->getArg(i)->IgnoreParenImpCasts();
      const auto *uo = dyn_cast<UnaryOperator>(x);
      return uo && uo->getOpcode() == UO_AddrOf &&
             uo->getSubExpr()->IgnoreParenImpCasts()->getType()->isPointerType();
    };
    if (!addressedPointer(0) || !addressedPointer(1))
      return;
    std::string dst = varNameOfAddressArg(call->getArg(0));
    std::string src = varNameOfAddressArg(call->getArg(1));
    if (dst.empty() || src.empty() || dst == src)
      return;
    linkAlias(dst, src);
    if (releasedVars_.count(src))
      markReleased(dst);
  }

  /// Strip casts/parens and no-op pointer offsets (`p + 0`, `0 + p`,
  /// `p - 0`) so nullability/release checks see the underlying pointer.
  const Expr *canonicalPointer(const Expr *e) const {
    if (!e)
      return nullptr;
    e = e->IgnoreParenImpCasts();
    for (unsigned depth = 0; depth < 8; ++depth) {
      const auto *bo = dyn_cast<BinaryOperator>(e);
      if (!bo || (bo->getOpcode() != BO_Add && bo->getOpcode() != BO_Sub))
        break;
      llvm::APSInt k;
      if (knownConst(bo->getRHS(), k) && k.isZero() &&
          bo->getLHS()->getType()->isPointerType()) {
        e = bo->getLHS()->IgnoreParenImpCasts();
        continue;
      }
      if (bo->getOpcode() == BO_Add && knownConst(bo->getLHS(), k) &&
          k.isZero() && bo->getRHS()->getType()->isPointerType()) {
        e = bo->getRHS()->IgnoreParenImpCasts();
        continue;
      }
      break;
    }
    while (const auto *ce = dyn_cast<ExplicitCastExpr>(e))
      e = ce->getSubExpr()->IgnoreParenImpCasts();
    return e;
  }

  /// Resolve an expression to the index of a parameter of the current
  /// function, following local aliases (`int *q = p; q = p;`) and zero-offset
  /// arithmetic.  Returns -1 when the value is not traceable to a parameter.
  int paramIndexOf(const Expr *e) const {
    e = canonicalPointer(e);
    if (!e)
      return -1;
    const auto *ref = dyn_cast<DeclRefExpr>(e);
    if (!ref)
      return -1;
    const auto *vd = dyn_cast<VarDecl>(ref->getDecl());
    if (!vd || !currentFnDecl_)
      return -1;
    if (const auto *pv = dyn_cast<ParmVarDecl>(vd)) {
      if (pv->getDeclContext() != static_cast<const DeclContext *>(currentFnDecl_))
        return -1;
      return (int)pv->getFunctionScopeIndex();
    }
    auto it = localParamAliases_.find(vd->getNameAsString());
    return it == localParamAliases_.end() ? -1 : it->second;
  }

  static bool isFunctionPointerParam(const ParmVarDecl *pv) {
    if (!pv)
      return false;
    QualType t = pv->getType();
    const auto *pt = t->getAs<PointerType>();
    return pt && pt->getPointeeType()->isFunctionProtoType();
  }

  /// Strip parentheses, implicit casts and explicit C-style casts from a
  /// callee expression. Never dereferences a null expression, so a
  /// CallExpr/ArraySubscript/deref callee cannot trip a dyn_cast assertion.
  static const Expr *stripCalleeCasts(const Expr *e) {
    if (!e)
      return nullptr;
    const Expr *x = e->IgnoreParenImpCasts();
    if (!x)
      return e;
    for (unsigned depth = 0; depth < 8; ++depth) {
      const auto *ce = dyn_cast<ExplicitCastExpr>(x);
      if (!ce || !ce->getSubExpr())
        break;
      x = ce->getSubExpr()->IgnoreParenImpCasts();
      if (!x)
        return e;
    }
    return x;
  }

  /// Add named targets bound to a variable slot (local, parameter, struct
  /// field, array or global function-pointer slot).
  void collectVarCalleeTargets(const VarDecl *vd, std::set<std::string> &out,
                               int depth) const {
    if (!vd || depth > 4)
      return;
    const std::string n = vd->getNameAsString();
    if (n.empty())
      return;
    auto add = [&](const std::map<std::string, std::string> &m,
                   const std::string &key) {
      auto it = m.find(key);
      if (it != m.end() && !it->second.empty())
        out.insert(it->second);
    };
    add(fnPtrVarTargets_, currentFunction_ + "::" + n);
    add(fnPtrVarTargets_, n);
    if (const auto *pv = dyn_cast<ParmVarDecl>(vd)) {
      auto pit = fnPtrParamTargets_.find(currentFunction_);
      if (pit != fnPtrParamTargets_.end()) {
        auto sit = pit->second.find(pv->getFunctionScopeIndex());
        if (sit != pit->second.end())
          out.insert(sit->second.begin(), sit->second.end());
      }
    }
  }

  /// Resolve one callee expression to function names, following function
  /// pointers held in variables, fields, arrays, global slots, dereferences,
  /// explicit casts, and functions returning function pointers.
  void resolveCalleeExpr(const Expr *expr, std::set<std::string> &out,
                         int depth) const {
    if (!expr || depth > 4)
      return;
    const Expr *callee = stripCalleeCasts(expr);
    if (!callee)
      return;
    if (const auto *ref = dyn_cast<DeclRefExpr>(callee)) {
      const Decl *d = ref->getDecl();
      if (const auto *fd = dyn_cast_or_null<FunctionDecl>(d))
        out.insert(fd->getNameAsString());
      else
        collectVarCalleeTargets(dyn_cast_or_null<VarDecl>(d), out, depth + 1);
    } else if (const auto *me = dyn_cast<MemberExpr>(callee)) {
      const auto *field = dyn_cast_or_null<FieldDecl>(me->getMemberDecl());
      if (field) {
        auto it = fnPtrVarTargets_.find(field->getNameAsString());
        if (it != fnPtrVarTargets_.end() && !it->second.empty())
          out.insert(it->second);
        const std::string key = varNameOf(me->getBase()) + "." +
                                field->getNameAsString();
        auto it2 = fnPtrVarTargets_.find(key);
        if (it2 != fnPtrVarTargets_.end() && !it2->second.empty())
          out.insert(it2->second);
      }
      resolveCalleeExpr(me->getBase(), out, depth + 1);
    } else if (const auto *ase = dyn_cast<ArraySubscriptExpr>(callee)) {
      const Expr *base = stripCalleeCasts(ase->getBase());
      if (const auto *ref = dyn_cast_or_null<DeclRefExpr>(base))
        collectVarCalleeTargets(dyn_cast_or_null<VarDecl>(ref->getDecl()), out,
                                depth + 1);
      auto it = fnPtrVarTargets_.find(currentFunction_ + "::" +
                                      varNameOf(base));
      if (it != fnPtrVarTargets_.end() && !it->second.empty())
        out.insert(it->second);
    } else if (const auto *uo = dyn_cast<UnaryOperator>(callee)) {
      if (uo->getOpcode() == UO_Deref || uo->getOpcode() == UO_AddrOf)
        resolveCalleeExpr(uo->getSubExpr(), out, depth + 1);
    } else if (const auto *c = dyn_cast<CallExpr>(callee)) {
      // `get()(p)`: the callee is itself a call returning a function pointer.
      if (const FunctionDecl *fd = c->getDirectCallee()) {
        auto rit = returnedFnPtr_.find(fd->getNameAsString());
        if (rit != returnedFnPtr_.end())
          out.insert(rit->second.begin(), rit->second.end());
      }
      std::set<std::string> nested;
      resolveCalleeExpr(c->getCallee(), nested, depth + 1);
      for (const auto &n : nested) {
        auto rit = returnedFnPtr_.find(n);
        if (rit != returnedFnPtr_.end())
          out.insert(rit->second.begin(), rit->second.end());
      }
    }
  }

  /// Possible named targets of an indirect call. Direct calls resolve to
  /// one name; function-pointer variables, fields, arrays, dereferences,
  /// casts and callbacks use the points-to facts from the collection pass.
  std::set<std::string> calleeNames(const CallExpr *call,
                                    const FunctionDecl *direct) const {
    if (direct)
      return {direct->getNameAsString()};
    std::set<std::string> out;
    if (!call || !call->getCallee())
      return out;
    resolveCalleeExpr(call->getCallee(), out, 0);
    return out;
  }

  static bool isReleaseName(const std::string &n) {
    return n == "free" || n == "cplus_release";
  }

  /// C+ exposes a managed realloc as `cplus_realloc`; it has the same
  /// success-only ownership semantics as libc `realloc` and must use the same
  /// state machine (stale source/alias invalidation on success only).
  static bool isReallocName(const std::string &n) {
    return n == "realloc" || n == "cplus_realloc";
  }

  /// Compiler-runtime / rewriter helpers and standard allocators have known
  /// semantics from the side table even without a body in this TU. Their
  /// results carry a proven extent, so the opaque-pointer rule ignores them.
  static bool isKnownRuntimeCallee(const std::string &n) {
    if (n.rfind("cplus_", 0) == 0 || n.rfind("__cplus_", 0) == 0 ||
        n.rfind("__builtin_", 0) == 0)
      return true;
    static const std::set<std::string> kAlloc = {
        "malloc", "calloc", "realloc", "aligned_alloc", "memalign",
        "posix_memalign", "valloc", "strdup", "strndup"};
    return kAlloc.count(n) != 0;
  }

  /// Allocation identity of a variable, created lazily for a tracked heap
  /// allocation and reused across aliases and global stores. Names alone are
  /// not enough: `g = p` must taint p even when the release happens in a
  /// different function or through a `void *` handle.
  std::string allocIdOf(const std::string &name) {
    if (name.empty())
      return std::string();
    auto it = varAllocIdentity_.find(name);
    if (it != varAllocIdentity_.end())
      return it->second;
    if (!heapAllocs_.count(name))
      return std::string();
    const std::string id = "@alloc:" + name;
    varAllocIdentity_[name] = id;
    allocIdentityMembers_[id].insert(name);
    for (const auto &kv : aliasGroups_)
      if (kv.second.count(name))
        allocIdentityMembers_[id].insert(kv.first);
    return id;
  }

  /// Record a global store as both a name-level and allocation-identity
  /// points-to fact so releases through the global reach every alias.
  void registerGlobalPointsTo(const std::string &g, const std::string &src) {
    if (g.empty() || src.empty() || g == src)
      return;
    globalPointsTo_[g].insert(src);
    const std::string id = allocIdOf(src);
    if (!id.empty()) {
      globalAllocPointsTo_[g].insert(id);
      varAllocIdentity_[g] = id;
      allocIdentityMembers_[id].insert(g);
    }
  }

  // ---- Heap-extent tracking -------------------------------------------
  // Only allocations whose byte count is a compile-time constant are tracked.
  // Unknown or computed sizes are deliberately left untracked so that valid C
  // is never rejected on a guess.

  struct HeapAllocState {
    long long bytes = -1; // total byte extent; -1 means unknown
    bool zeroInit = false; // calloc-style zero initialisation
    const VarDecl *decl = nullptr;
    ExtentOrigin origin = ExtentOrigin::Unknown;
    // Set when a pointer is known to originate from a tracked object but an
    // intervening operation (union type punning, for example) destroyed the
    // addressable extent. STRICT fails closed on a dereference of such a
    // value instead of treating it as an opaque ABI pointer.
    bool provenanceLost = false;
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
    std::string baseName = key.substr(0, key.find('.'));
    if (addressTakenVars_.count(baseName))
      return false;
    auto it = constLocals_.find(key);
    if (it == constLocals_.end())
      return false;
    // A reassigned local is no longer constant.
    std::string base = baseName;
    if (assignedVars_.count(base))
      return false;
    out = it->second;
    return true;
  }

  /// Byte size of the pointee of a pointer argument (0 when not statically
  /// known).  Needed to relate an element count to an actual byte extent.
  unsigned long long pointeeSizeOfArg(const Expr *arg) const {
    if (!arg)
      return 0;
    QualType aq = arg->IgnoreParenImpCasts()->getType();
    // At a call site an array argument still has ARRAY type (decay has not
    // happened in the expression's type).  Handle both arrays and pointers.
    QualType t;
    if (const auto *at = aq->getAsArrayTypeUnsafe())
      t = at->getElementType();
    else if (const auto *pt = aq->getAs<PointerType>())
      t = pt->getPointeeType();
    else
      return 0;
    if (t.isNull() || t->isVoidType() || t->isIncompleteType() ||
        t->isDependentType())
      return 0;
    return (unsigned long long)(ctx_.getTypeSize(t) / 8u);
  }

  /// Resolve a `%s` destination to its base object and fixed byte capacity.
  /// Accepts the forms used to hide a fixed buffer from a naive DeclRefExpr
  /// check: `buf`, `&buf[0]`, `s.buf`, and a pointer alias of a stack array.
  /// Unknown forms return nullptr so no bounded-write claim is made.
  const VarDecl *charDestOf(const Expr *arg, unsigned long long &cap,
                            bool allowPointerAlias = true) const {
    cap = 0;
    if (!arg)
      return nullptr;
    const Expr *e = arg->IgnoreParenImpCasts();
    if (const auto *uo = dyn_cast<UnaryOperator>(e)) {
      if (uo->getOpcode() != UO_AddrOf)
        return nullptr;
      e = uo->getSubExpr()->IgnoreParenImpCasts();
    }
    // `&buf[i]` decays to the array object; resolve to its base variable.
    if (const auto *ase = dyn_cast<ArraySubscriptExpr>(e))
      e = ase->getBase()->IgnoreParenCasts();
    const VarDecl *vd = nullptr;
    if (const auto *me = dyn_cast<MemberExpr>(e)) {
      const auto *at = ctx_.getAsConstantArrayType(me->getType());
      if (!at || !at->getElementType()->isCharType())
        return nullptr;
      const auto *ref = dyn_cast<DeclRefExpr>(me->getBase()->IgnoreParenCasts());
      vd = ref ? dyn_cast<VarDecl>(ref->getDecl()) : nullptr;
      if (!vd)
        return nullptr;
      cap = at->getSize().getZExtValue();
      return vd;
    }
    const auto *ref = dyn_cast<DeclRefExpr>(e);
    vd = ref ? dyn_cast<VarDecl>(ref->getDecl()) : nullptr;
    if (!vd)
      return nullptr;
    if (const auto *at = ctx_.getAsConstantArrayType(vd->getType())) {
      if (!at->getElementType()->isCharType())
        return nullptr;
      cap = at->getSize().getZExtValue();
      return vd;
    }
    // Pointer alias of a stack array (`char *p = buf`) keeps the extent.
    // Diagnostics that inspect the array's own initializer must not follow
    // this path: we cannot see a pointee's bytes through a pointer variable.
    if (!allowPointerAlias)
      return nullptr;
    const std::string &n = vd->getNameAsString();
    auto sit = stackArrayExtents_.find(n);
    if (sit != stackArrayExtents_.end() && sit->second > 0) {
      cap = (unsigned long long)sit->second;
      return vd;
    }
    auto hit = heapAllocs_.find(n);
    if (hit != heapAllocs_.end() && hit->second.bytes > 0 &&
        hit->second.origin == ExtentOrigin::Stack) {
      cap = (unsigned long long)hit->second.bytes;
      return vd;
    }
    return nullptr;
  }

  /// An adjacent integer argument that supplies the pointer's element count.
  /// `knownConst` also accepts locals folded to a constant in the collection
  /// pass, so a passed length variable counts when its value is known.
  bool companionCountArg(const CallExpr *call, unsigned ptrSlot,
                         unsigned &countSlot, long long &count) const {
    for (unsigned j = 0; j < call->getNumArgs(); ++j) {
      if (j == ptrSlot || (j != ptrSlot + 1 && j + 1 != ptrSlot))
        continue;
      const Expr *a = call->getArg(j)->IgnoreParenImpCasts();
      if (!a->getType()->isIntegerType())
        continue;
      llvm::APSInt v;
      if (!knownConst(a, v) || !fits64(v))
        continue;
      count = v.isNegative() ? v.getSExtValue() : (long long)v.getZExtValue();
      if (count < 0)
        continue;
      countSlot = j;
      return true;
    }
    return false;
  }

  /// Condition of a NUL-terminated traversal: `s[i]` or `s[i] != '\0'`.
  const ArraySubscriptExpr *nulCondSubscript(const Expr *cond) const {
    if (!cond)
      return nullptr;
    const Expr *e = cond->IgnoreParenImpCasts();
    if (const auto *bo = dyn_cast<BinaryOperator>(e)) {
      if (bo->getOpcode() != BO_NE)
        return nullptr;
      const auto *ase =
          dyn_cast<ArraySubscriptExpr>(bo->getLHS()->IgnoreParenImpCasts());
      const Expr *other = bo->getRHS();
      if (!ase) {
        ase = dyn_cast<ArraySubscriptExpr>(bo->getRHS()->IgnoreParenImpCasts());
        other = bo->getLHS();
      }
      if (!ase || !other)
        return nullptr;
      Expr::EvalResult r;
      if (!(other->IgnoreParenImpCasts()->EvaluateAsInt(r, ctx_) &&
            r.Val.isInt() && r.Val.getInt() == 0))
        return nullptr;
      return ase;
    }
    return dyn_cast<ArraySubscriptExpr>(e);
  }

  bool nulTraversalKey(const ArraySubscriptExpr *ase, const VarDecl *&base,
                       const VarDecl *&idx) const {
    if (!ase)
      return false;
    const auto *bref =
        dyn_cast<DeclRefExpr>(ase->getBase()->IgnoreParenImpCasts());
    const auto *iref =
        dyn_cast<DeclRefExpr>(ase->getIdx()->IgnoreParenImpCasts());
    base = bref ? dyn_cast<VarDecl>(bref->getDecl()) : nullptr;
    idx = iref ? dyn_cast<VarDecl>(iref->getDecl()) : nullptr;
    return base && idx;
  }

  static bool isConstCharParam(const VarDecl *vd) {
    const auto *pvd = dyn_cast_or_null<ParmVarDecl>(vd);
    if (!pvd || !pvd->getType()->isPointerType())
      return false;
    QualType pointee = pvd->getType()->getPointeeType();
    return pointee->isCharType() && pointee.isConstQualified();
  }

  /// True when `cond` holding implies `argc > k`.
  bool argcExceedsFromCond(const Expr *cond, long long k) const {
    if (!cond)
      return false;
    cond = cond->IgnoreParenImpCasts();
    const auto *bo = dyn_cast<BinaryOperator>(cond);
    if (!bo)
      return false;
    if (bo->getOpcode() == BO_LAnd)
      return argcExceedsFromCond(bo->getLHS(), k) ||
             argcExceedsFromCond(bo->getRHS(), k);
    if (bo->getOpcode() == BO_LOr)
      return argcExceedsFromCond(bo->getLHS(), k) &&
             argcExceedsFromCond(bo->getRHS(), k);
    auto isArgc = [](const Expr *e) {
      const auto *ref = dyn_cast<DeclRefExpr>(e->IgnoreParenImpCasts());
      return ref && ref->getDecl() && ref->getDecl()->getName() == "argc";
    };
    long long bound = 0;
    if (isArgc(bo->getLHS()) && evalIndexValue(bo->getRHS(), bound)) {
      if (bo->getOpcode() == BO_GT)
        return bound >= k;
      if (bo->getOpcode() == BO_GE)
        return bound > k;
      return false;
    }
    if (isArgc(bo->getRHS()) && evalIndexValue(bo->getLHS(), bound)) {
      if (bo->getOpcode() == BO_LT)
        return bound >= k;
      if (bo->getOpcode() == BO_LE)
        return bound > k;
      return false;
    }
    return false;
  }

  /// The C standard gives `main`'s `argv` a known extent: exactly `argc + 1`
  /// pointers (entries 0..argc-1 plus the NULL terminator at argv[argc]).  A
  /// constant subscript `argv[k]` is therefore in bounds whenever `k <= argc`.
  /// Require the ordinary dominating guard `argc > k` in a ternary true-branch
  /// or an `if` then-branch before accepting it.
  bool argvIndexGuarded(const ArraySubscriptExpr *sub,
                        const ParmVarDecl *pvd) const {
    if (currentFunction_ != "main" || pvd->getFunctionScopeIndex() != 1)
      return false;
    QualType paramTy = pvd->getType();
    if (!paramTy->isPointerType())
      return false;
    QualType pointee = paramTy->getPointeeType();
    if (!pointee->isPointerType() || !pointee->getPointeeType()->isCharType())
      return false;
    long long k = -1;
    if (!evalIndexValue(sub->getIdx(), k) || k < 0)
      return false;
    DynTypedNode cur = DynTypedNode::create(*sub);
    for (int depth = 0; depth < 64; ++depth) {
      DynTypedNodeList parents = ctx_.getParents(cur);
      if (parents.empty())
        return false;
      const DynTypedNode &parent = *parents.begin();
      const Expr *cond = nullptr;
      const Stmt *branch = nullptr;
      if (const auto *co = parent.get<ConditionalOperator>()) {
        cond = co->getCond();
        branch = co->getTrueExpr();
      } else if (const auto *bc = parent.get<BinaryConditionalOperator>()) {
        cond = bc->getCond();
        branch = bc->getTrueExpr();
      } else if (const auto *ifs = parent.get<IfStmt>()) {
        cond = ifs->getCond();
        branch = ifs->getThen();
      }
      if (cond && branch && cur.get<Stmt>() == branch &&
          argcExceedsFromCond(cond, k))
        return true;
      cur = parent;
    }
    return false;
  }

  /// True when a pointer parameter access has one of the proven contracts: a
  /// companion count parameter that visibly bounds the index, a read-only
  /// NUL-terminator traversal of a `const char *`, or a guarded subscript of
  /// `main`'s `argv`.
  bool hasProvenParamExtent(const ParmVarDecl *pvd,
                            const ArraySubscriptExpr *sub) const {
    if (argvIndexGuarded(sub, pvd))
      return true;
    const ArgSlotKey key{currentFunction_, pvd->getFunctionScopeIndex()};
    auto cit = countPairedParams_.find(key);
    if (cit != countPairedParams_.end()) {
      const auto *iref =
          dyn_cast<DeclRefExpr>(sub->getIdx()->IgnoreParenImpCasts());
      const auto *ivd = iref ? dyn_cast<VarDecl>(iref->getDecl()) : nullptr;
      auto lit = ivd ? loopCountBound_.find(ivd) : loopCountBound_.end();
      return lit != loopCountBound_.end() && lit->second == cit->second;
    }
    const auto *iref =
        dyn_cast<DeclRefExpr>(sub->getIdx()->IgnoreParenImpCasts());
    const VarDecl *ivd = iref ? dyn_cast<VarDecl>(iref->getDecl()) : nullptr;
    return ivd && isConstCharParam(pvd) &&
           nulTraversal_.count({pvd, ivd}) != 0;
  }

  /// Compile-time value of a variable at a compound update (`x += 1`, `x++`).
  /// The collection pass records the initializer constant in constLocals_; a
  /// compound update reads that value *before* writing. Refuse when the
  /// variable was also assigned a non-compound value (initializer would be
  /// stale), when its address was taken, or when it is volatile.
  bool knownConstAtUpdate(const Expr *e, llvm::APSInt &out) const {
    if (!e)
      return false;
    std::string name = varNameOf(e);
    if (name.empty() || addressTakenVars_.count(name) ||
        nonConstantAssignedVars_.count(name))
      return false;
    const Expr *x = e->IgnoreParenCasts();
    if (const auto *ref = dyn_cast<DeclRefExpr>(x))
      if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl()))
        if (vd->getType().isVolatileQualified())
          return false;
    auto it = constLocals_.find(name);
    if (it == constLocals_.end())
      return false;
    out = it->second;
    return true;
  }

  /// Record why a local's initializer constant may no longer describe its
  /// value. Plain assignments and compound updates with a runtime RHS make the
  /// recorded value stale; a compound update with a constant RHS does not.
  void noteAssignedValueSource(const BinaryOperator *op) {
    if (!op || !op->isAssignmentOp())
      return;
    std::string lhs = varNameOf(op->getLHS());
    if (lhs.empty())
      return;
    const auto opc = op->getOpcode();
    bool compound = opc == BO_AddAssign || opc == BO_SubAssign ||
                    opc == BO_MulAssign || opc == BO_DivAssign ||
                    opc == BO_RemAssign || opc == BO_ShlAssign ||
                    opc == BO_ShrAssign;
    if (!compound) {
      nonConstantAssignedVars_.insert(lhs);
    } else {
      llvm::APSInt unused;
      if (!knownConst(op->getRHS(), unused))
        nonConstantAssignedVars_.insert(lhs);
    }
  }

  /// Convert a compile-time integer to a mathematical __int128 using the
  /// expression's own width/signedness. Values wider than 64 bits are unknown.
  bool intConstValue(const Expr *e, QualType ty, __int128 &out) const {
    llvm::APSInt v;
    if (!knownConst(e, v) || !fits64(v))
      return false;
    unsigned w = ctx_.getIntWidth(ty);
    if (w == 0 || w > 64)
      return false;
    unsigned long long bits = v.getZExtValue();
    if (w < 64)
      bits &= ((1ULL << w) - 1);
    if (ty->isSignedIntegerType() && (bits & (1ULL << (w - 1))))
      out = (__int128)bits - ((__int128)1 << w);
    else
      out = (__int128)bits;
    return true;
  }

  /// True when a mathematical integer is representable in `dst`.
  static bool intFits(__int128 v, unsigned bits, bool isSigned) {
    if (bits == 0 || bits > 64)
      return true; // unknown width: do not guess
    if (isSigned) {
      __int128 lo = -((__int128)1 << (bits - 1));
      __int128 hi = ((__int128)1 << (bits - 1)) - 1;
      return v >= lo && v <= hi;
    }
    return v >= 0 && v < (((__int128)1) << bits);
  }

  /// Evaluate a compile-time floating value (literals and macro constants).
  bool floatConstValue(const Expr *e, double &out) const {
    if (!e)
      return false;
    llvm::APFloat f(0.0);
    if (e->EvaluateAsFloat(f, ctx_)) {
      out = f.convertToDouble();
      return true;
    }
    Expr::EvalResult r;
    if (e->EvaluateAsRValue(r, ctx_) && r.Val.isFloat()) {
      out = r.Val.getFloat().convertToDouble();
      return true;
    }
    std::string name = varNameOf(e);
    if (!name.empty() && !addressTakenVars_.count(name) &&
        !nonConstantAssignedVars_.count(name)) {
      auto it = constFloatLocals_.find(name);
      if (it != constFloatLocals_.end()) {
        out = it->second;
        return true;
      }
    }
    return false;
  }

  /// True when a compile-time floating value is outside `dst`'s range.
  bool floatOutOfRange(const Expr *e, QualType dst) const {
    double d = 0;
    if (!floatConstValue(e, d))
      return false;
    if (std::isnan(d))
      return true; // NaN is never a valid integer conversion
    unsigned db = ctx_.getIntWidth(dst);
    if (db == 0 || db > 64)
      return false;
    double lo = 0.0, hiExclusive = 0.0;
    if (dst->isSignedIntegerType()) {
      lo = -std::ldexp(1.0, (int)db - 1);
      hiExclusive = std::ldexp(1.0, (int)db - 1);
    } else {
      hiExclusive = std::ldexp(1.0, (int)db);
    }
    return !(d >= lo && d < hiExclusive);
  }

  /// STRICT: reject a conversion whose source is a provably out-of-range
  /// compile-time constant. Runtime-valued sources are left to the rewriter.
  /// `explicitCast` marks a user-written C-style cast: that cast documents an
  /// intentional value-changing conversion, which is well-defined wrap for an
  /// unsigned integer target (see the exception below). Implicit narrowing
  /// stays rejected.
  void checkProvableConversion(const Expr *value, QualType src, QualType dst,
                               CheckedArithmeticSite::Op kind,
                               bool explicitCast = false) {
    if (!strict_ || !value || !dst->isIntegerType())
      return;
    unsigned db = ctx_.getIntWidth(dst);
    if (db == 0 || db > 64 || dst->isBooleanType() || dst->isEnumeralType())
      return;
    if (isHeaderLocation(
            ctx_.getSourceManager().getExpansionLoc(value->getBeginLoc())))
      return;
    bool unsafe = false;
    if (kind == CheckedArithmeticSite::Op::FloatToInt) {
      unsafe = floatOutOfRange(value, dst);
    } else if (kind == CheckedArithmeticSite::Op::Narrow) {
      unsigned sb = src->isIntegerType() ? ctx_.getIntWidth(src) : 0;
      if (sb == 0 || sb > 64 || db > sb || src->isBooleanType() ||
          src->isEnumeralType())
        return;
      __int128 v = 0;
      if (!intConstValue(value, src, v))
        return;
      if (db < sb) {
        unsafe = !intFits(v, db, dst->isSignedIntegerType());
      } else {
        // db == sb: the rewriter does not instrument same-width conversions,
        // but an implicit unsigned -> signed conversion of a compile-time
        // constant above the signed maximum still silently changes value.
        // Name that provable case in STRICT; values that still fit (idiomatic
        // sign round-trips) stay accepted.
        unsafe = src->isUnsignedIntegerType() &&
                 dst->isSignedIntegerType() && !intFits(v, db, true);
      }
    }
    if (unsafe) {
      // An explicit C-style cast is the programmer's assertion that the
      // value-changing conversion is intended. For an integer -> unsigned
      // target the C standard defines modulo 2^N wrap, so the result is
      // fully defined (e.g. `(uint8_t)300u` is 44, `(unsigned)0x1'0000'0005ULL`
      // is 5); STRICT must not reject such a documented truncation. This
      // exception deliberately does NOT cover implicit narrowing (`char c=300;`
      // stays rejected, matrix case e_narrowing_conversion), float-to-int
      // casts (out-of-range float-to-unsigned is undefined, not wrap), or
      // value-changing conversions to a signed target (signed overflow).
      if (explicitCast && kind == CheckedArithmeticSite::Op::Narrow &&
          dst->isUnsignedIntegerType())
        return;
      report(value->getBeginLoc(),
             std::string("provably unsafe constant conversion to '") +
                 dst.getCanonicalType().getAsString(
                     PrintingPolicy(ctx_.getLangOpts())) +
                 "' in STRICT mode");
    }
  }

  /// A conversion whose operand is itself an instrumented conversion produces
  /// overlapping rewrite spans (wrong helper arity / compile failure). Such
  /// chains cannot be proven safe by STRICT `check`.
  bool isNestedConversion(const Expr *value) const {
    if (!value)
      return false;
    const auto *ce = dyn_cast<ExplicitCastExpr>(value->IgnoreParenImpCasts());
    if (!ce || !ce->getSubExpr())
      return false;
    QualType t = ce->getType(), st = ce->getSubExpr()->getType();
    if (t->isIntegerType() && st->isFloatingType())
      return true;
    unsigned db = ctx_.getIntWidth(t), sb = ctx_.getIntWidth(st);
    return t->isIntegerType() && st->isIntegerType() && db > 0 && sb > 0 &&
           db < sb && !t->isBooleanType() && !st->isBooleanType() &&
           !t->isEnumeralType() && !st->isEnumeralType();
  }

  /// STRICT fail-closed for conversion sites produced by a macro expansion:
  /// the rewriter cannot safely instrument them. Constant-valued operands are
  /// handled by checkProvableConversion instead and stay accepted when safe.
  bool conversionInMacro(const Expr *conv, const Expr *value) const {
    if (!strict_ || !conv)
      return false;
    SourceLocation b = conv->getBeginLoc(), e = conv->getEndLoc();
    if (!b.isValid() || !b.isMacroID())
      if (!e.isValid() || !e.isMacroID())
        return false;
    llvm::APSInt unused;
    if (knownConst(value, unused))
      return false;
    SourceManager &sm = ctx_.getSourceManager();
    // C+/runtime helper macros (`cplus_new_array`, `managed`/`owned`
    // lowering, ...) are compiler-generated: their casts are not user
    // conversions the rewriter must guard, so they must not fail closed.
    llvm::StringRef macro = Lexer::getImmediateMacroName(b, sm, ctx_.getLangOpts());
    if (macro.starts_with("cplus_") || macro.starts_with("__cplus_") ||
        macro.starts_with("CPLUS_") || macro.starts_with("_cplus_"))
      return false;
    return sm.getFileID(sm.getExpansionLoc(b)) == sm.getMainFileID();
  }

  /// Constant-fold integer arithmetic using tracked local/field constants.
  /// Clang will not fold expressions over a non-const local struct, so we do
  /// it ourselves at the source integer width.
  bool evalConstArithmetic(const Expr *e, llvm::APSInt &out) const {
    llvm::APSInt leaf;
    if (knownConst(e, leaf)) {
      // A wrapped unsigned value may be folded by Clang into a signed
      // negative APSInt; work with its unsigned bit pattern instead.
      if (!fits64(leaf))
        return false;
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
    if (w == 0 || w > 64 || !fits64(a) || !fits64(b))
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
      if (v.isNegative() || !fits64(v))
        return false;
      unsigned long long z = v.getZExtValue();
      if (z > (1ULL << 40))
        return false; // absurd; handled as a definitely-failing allocation
      out = (long long)z;
      return true;
    }
    llvm::APSInt v;
    if (!evalConstArithmetic(arg, v) || v.isNegative()) {
      // A helper whose every return is the same constant is still a concrete
      // size: `static int sz(void){ return 4; } malloc(sz())` (S9).
      const Expr *x = arg->IgnoreParenImpCasts();
      if (const auto *call = dyn_cast<CallExpr>(x)) {
        for (const auto &cn : calleeNames(call, call->getDirectCallee())) {
          auto it = constReturnValues_.find(cn);
          if (it != constReturnValues_.end() && it->second >= 0) {
            out = it->second;
            return true;
          }
        }
      }
      return false;
    }
    if (!fits64(v))
      return false;
    unsigned long long z = v.getZExtValue();
    if (z > (1ULL << 40))
      return false;
    out = (long long)z;
    return true;
  }

  /// Header helpers are not part of the translated user source; do not emit
  /// arithmetic diagnostics from included headers.
  bool isHeaderLocation(SourceLocation loc) const {
    if (loc.isInvalid())
      return false;
    PresumedLoc p = ctx_.getSourceManager().getPresumedLoc(loc);
    if (!p.isValid())
      return false;
    std::string f = p.getFilename();
    return !f.empty() && f != filename_ && f != filename_ + ".c";
  }

  /// Signed arithmetic result check with a wider intermediate type so the
  /// overflow is seen before wrapping.
  bool arithmeticOverflow(const Expr *op, long long a, long long b,
                          bool &overflow) const {
    return arithmeticOverflow(ctx_.getIntWidth(op->getType()),
                              dyn_cast<BinaryOperator>(op)->getOpcode(),
                              a, b, overflow);
  }
  bool arithmeticOverflow(unsigned w, BinaryOperator::Opcode opc,
                          long long a, long long b, bool &overflow) const {
    if (w == 0 || w > 64)
      return false;
    __int128 lo = -((__int128)1 << (w - 1));
    __int128 hi = ((__int128)1 << (w - 1)) - 1;
    __int128 r = 0;
    // Compound assignment opcodes must be normalised to their arithmetic
    // counterpart: `v += 1` has the same overflow behaviour as `v = v + 1`,
    // but the switch below originally ignored the *_Assign opcodes so a
    // provably overflowing compound update slipped through `check` and only
    // trapped in the guarded build.
    switch (opc) {
    case BO_Add: case BO_AddAssign: r = (__int128)a + b; break;
    case BO_Sub: case BO_SubAssign: r = (__int128)a - b; break;
    case BO_Mul: case BO_MulAssign: r = (__int128)a * b; break;
    default: return false;
    }
    overflow = r < lo || r > hi;
    return true;
  }

  /// Signed constant overflow for integer widths the long long fast path
  /// cannot represent (e.g. `__int128`). Compute at (width+1) bits and verify
  /// that truncating back to the real width and re-extending reproduces the
  /// result; a mismatch means the mathematical result wrapped. Used only for
  /// fail-closed STRICT diagnostics, so no operand value is ever guessed.
  bool wideConstOverflow(const BinaryOperator *op, bool &overflow) const {
    overflow = false;
    if (!op || !op->getType()->isSignedIntegerType())
      return false;
    unsigned w = ctx_.getIntWidth(op->getType());
    if (w <= 64 || w > 128)
      return false;
    llvm::APSInt a, b;
    if (!knownConst(op->getLHS(), a) || !knownConst(op->getRHS(), b))
      return false;
    if (a.getBitWidth() != w || b.getBitWidth() != w)
      return false;
    a.setIsSigned(true);
    b.setIsSigned(true);
    const auto opc = op->getOpcode();
    if (opc == BO_Div || opc == BO_DivAssign || opc == BO_Rem ||
        opc == BO_RemAssign) {
      // The only signed div/rem overflow is MIN / -1 (MIN % -1 has the same
      // UB in C). Compare against an exact APSInt minimum at the real width.
      llvm::APSInt minVal = llvm::APSInt::getMinValue(w, /*Unsigned=*/false);
      llvm::APSInt minusOne(w, /*isSigned=*/true);
      minusOne.setAllBits(); // two's complement -1
      overflow = (a == minVal && b == minusOne);
      return true;
    }
    llvm::APSInt wide;
    llvm::APSInt wa = a.extend(w + 1), wb = b.extend(w + 1);
    switch (opc) {
    case BO_Add: case BO_AddAssign: wide = wa + wb; break;
    case BO_Sub: case BO_SubAssign: wide = wa - wb; break;
    case BO_Mul: case BO_MulAssign: wide = wa * wb; break;
    default: return false;
    }
    llvm::APSInt back = wide.trunc(w).extend(w + 1);
    overflow = (back != wide);
    return true;
  }

  /// STRICT fail-closed for arithmetic whose operator token comes from a
  /// macro expansion with runtime operands: the rewriter cannot instrument
  /// it. Shared by the `check` diagnostic and the build-mode site recorder
  /// so the two modes agree (no fail-open gap).
  bool arithmeticMacroFailClosed(const BinaryOperator *op) const {
    if (!strict_ || !op)
      return false;
    SourceLocation b = op->getBeginLoc(), o = op->getOperatorLoc();
    if (!b.isValid() || !o.isValid() || (!b.isMacroID() && !o.isMacroID()))
      return false;
    if (isHeaderLocation(b))
      return false;
    if (isConstantMacroExpr(op->getLHS()) &&
        isConstantMacroExpr(op->getRHS()))
      return false; // constant macros fold cleanly, no runtime guard needed
    SourceManager &sm = ctx_.getSourceManager();
    return sm.getFileID(sm.getExpansionLoc(b)) == sm.getMainFileID();
  }

  /// True when an operand is not a macro-derived runtime value. Plain
  /// operands and compile-time constant macros (INT_MAX, INT_MIN, literal
  /// macros, ...) need no runtime guard and must not fail closed.
  bool isConstantMacroExpr(const Expr *e) const {
    if (!e)
      return true;
    const SourceLocation b = e->getBeginLoc();
    if (!b.isValid() || !b.isMacroID())
      return true; // not produced by a macro expansion
    Expr::EvalResult r;
    if (e->IgnoreParenCasts()->EvaluateAsInt(r, ctx_) && r.Val.isInt())
      return true;
    static const std::set<std::string> kLimits = {
        "INT_MAX", "INT_MIN", "UINT_MAX", "LONG_MAX", "LONG_MIN",
        "ULONG_MAX", "LLONG_MAX", "LLONG_MIN", "ULLONG_MAX", "CHAR_MAX",
        "CHAR_MIN", "SHRT_MAX", "SHRT_MIN", "SIZE_MAX"};
    if (const auto *ref = dyn_cast_or_null<DeclRefExpr>(
            e->IgnoreParenImpCasts()))
      if (const auto *nd = dyn_cast_or_null<NamedDecl>(ref->getDecl()))
        if (kLimits.count(nd->getNameAsString()))
          return true;
    return false;
  }

  /// Record a runtime-value signed add/sub/mul so the source instrumenter can
  /// wrap it in a native (non-UBSan) checked call.  Fully constant expressions
  /// are handled by the static check above and must be left untouched.
  void maybeRecordCheckedArithmetic(const BinaryOperator *op) {
    if (!recordArithmeticSites_ || !op ||
        isHeaderLocation(op->getBeginLoc()))
      return;
    if (allocExtentExprLocs_.count(op->getBeginLoc().getRawEncoding()))
      return; // already emitted as a checked allocation extent
    CheckedArithmeticSite::Op kind;
    bool compound = false;
    switch (op->getOpcode()) {
    case BO_Add: kind = CheckedArithmeticSite::Op::Add; break;
    case BO_Sub: kind = CheckedArithmeticSite::Op::Sub; break;
    case BO_Mul: kind = CheckedArithmeticSite::Op::Mul; break;
    case BO_Div: kind = CheckedArithmeticSite::Op::Div; break;
    case BO_Rem: kind = CheckedArithmeticSite::Op::Rem; break;
    case BO_Shl: kind = CheckedArithmeticSite::Op::Shl; break;
    case BO_Shr: kind = CheckedArithmeticSite::Op::Shr; break;
    case BO_AddAssign: kind = CheckedArithmeticSite::Op::Add; compound = true; break;
    case BO_SubAssign: kind = CheckedArithmeticSite::Op::Sub; compound = true; break;
    case BO_MulAssign: kind = CheckedArithmeticSite::Op::Mul; compound = true; break;
    case BO_DivAssign: kind = CheckedArithmeticSite::Op::Div; compound = true; break;
    case BO_RemAssign: kind = CheckedArithmeticSite::Op::Rem; compound = true; break;
    case BO_ShlAssign: kind = CheckedArithmeticSite::Op::Shl; compound = true; break;
    case BO_ShrAssign: kind = CheckedArithmeticSite::Op::Shr; compound = true; break;
    default: return;
    }
    QualType ty = op->getType();
    // Signed overflow is only possible for add/sub/mul; div/rem/shift also
    // need guards on unsigned operands (zero divisor, invalid shift count).
    if (ty->isDependentType())
      return;
    if ((kind == CheckedArithmeticSite::Op::Add ||
         kind == CheckedArithmeticSite::Op::Sub ||
         kind == CheckedArithmeticSite::Op::Mul) &&
        !ty->isSignedIntegerType())
      return;
    if (!ty->isIntegerType())
      return;
    // Pointer subtraction also has a signed integer (ptrdiff_t) result, but
    // its operands are pointers: rewriting it as integer subtraction of the
    // raw addresses would silently change one-past/offset semantics.
    if (!op->getLHS()->getType()->isIntegerType() ||
        !op->getRHS()->getType()->isIntegerType())
      return;
    unsigned bits = ctx_.getIntWidth(ty);
    if (bits == 0 || bits > 64)
      return; // __int128 and wider: no int64_t helper, leave unchanged
    // The helper ABI returns int64_t; values of wider unsigned types do not
    // round-trip reliably through it.  Leave those operations unchanged.
    if (!ty->isSignedIntegerType() && bits > 32)
      return;
    llvm::APSInt unused;
    if (knownConst(op->getLHS(), unused) && knownConst(op->getRHS(), unused))
      return; // compile-time expression: the static check already covered it

    SourceManager &sm = ctx_.getSourceManager();
    SourceLocation b = op->getBeginLoc(), o = op->getOperatorLoc(),
                   e = op->getEndLoc();
    // Macro-generated arithmetic cannot be rewritten safely at the macro
    // expansion point; the backend sanitizer (when enabled) still covers it.
    if (b.isInvalid() || o.isInvalid() || e.isInvalid() || b.isMacroID() ||
        o.isMacroID() || e.isMacroID()) {
      if (arithmeticMacroFailClosed(op))
        report(b, "arithmetic in a macro expansion cannot be instrumented; "
                  "STRICT cannot prove it safe");
      return;
    }
    FileID main = sm.getMainFileID();
    if (sm.getFileID(b) != main || sm.getFileID(o) != main ||
        sm.getFileID(e) != main)
      return;
    SourceLocation afterOp =
        Lexer::getLocForEndOfToken(o, 0, sm, ctx_.getLangOpts());
    SourceLocation afterEnd =
        Lexer::getLocForEndOfToken(e, 0, sm, ctx_.getLangOpts());
    if (afterOp.isInvalid() || afterEnd.isInvalid())
      return;
    size_t begin = sm.getFileOffset(b), opOff = sm.getFileOffset(o);
    size_t end = sm.getFileOffset(afterEnd);
    size_t opLen = sm.getFileOffset(afterOp) - opOff;
    if (begin >= opOff || opOff + opLen > end)
      return;
    PrintingPolicy pp(ctx_.getLangOpts());
    CheckedArithmeticSite site;
    site.kind = kind;
    site.begin = begin;
    site.op = opOff;
    site.opLength = opLen;
    site.end = end;
    site.compound = compound;
    site.bits = bits;
    site.isSigned = ty->isSignedIntegerType();
    site.type = ty.getCanonicalType().getAsString(pp);
    arithmeticSites_.push_back(std::move(site));
  }

  /// A runtime element-count multiplication passed to malloc/calloc can wrap
  /// and under-allocate. Emit an AllocExtent site so the rewriter checks the
  /// product before the allocation.
  void recordAllocExtentSite(const CallExpr *call, unsigned idx) {
    if (!recordArithmeticSites_ || !report_ || !call ||
        call->getNumArgs() <= idx)
      return;
    const Expr *arg = call->getArg(idx)->IgnoreParenImpCasts();
    const auto *bo = dyn_cast<BinaryOperator>(arg);
    if (!bo || bo->getOpcode() != BO_Mul)
      return;
    SourceLocation b = arg->getBeginLoc(), o = bo->getOperatorLoc(),
                   e = arg->getEndLoc();
    if (b.isInvalid() || o.isInvalid() || e.isInvalid() || b.isMacroID() ||
        o.isMacroID() || e.isMacroID() || isHeaderLocation(b))
      return;
    SourceManager &sm = ctx_.getSourceManager();
    FileID main = sm.getMainFileID();
    if (sm.getFileID(b) != main || sm.getFileID(o) != main ||
        sm.getFileID(e) != main)
      return;
    SourceLocation afterOp =
        Lexer::getLocForEndOfToken(o, 0, sm, ctx_.getLangOpts());
    SourceLocation afterEnd =
        Lexer::getLocForEndOfToken(e, 0, sm, ctx_.getLangOpts());
    if (afterOp.isInvalid() || afterEnd.isInvalid())
      return;
    size_t begin = sm.getFileOffset(b), opOff = sm.getFileOffset(o);
    size_t end = sm.getFileOffset(afterEnd);
    size_t opLen = sm.getFileOffset(afterOp) - opOff;
    if (begin >= opOff || opOff + opLen > end)
      return;
    auto textOf = [&](const Expr *x) {
      SourceLocation xb = x->getBeginLoc();
      SourceLocation xa = Lexer::getLocForEndOfToken(
          x->getEndLoc(), 0, sm, ctx_.getLangOpts());
      if (xb.isInvalid() || xa.isInvalid() || xb.isMacroID())
        return std::string();
      return Lexer::getSourceText(CharSourceRange::getCharRange(xb, xa), sm,
                                  ctx_.getLangOpts())
          .str();
    };
    std::string count = textOf(bo->getLHS()), elem = textOf(bo->getRHS());
    if (count.empty() || elem.empty())
      return;
    CheckedArithmeticSite site;
    site.kind = CheckedArithmeticSite::Op::AllocExtent;
    site.begin = begin;
    site.op = opOff;
    site.opLength = opLen;
    site.end = end;
    site.countExpr = count;
    site.elemExpr = elem;
    site.bits = 64;
    site.isSigned = false;
    site.type = "size_t";
    arithmeticSites_.push_back(std::move(site));
    allocExtentExprLocs_.insert(b.getRawEncoding());
  }

  /// Record a checked site for a unary operator (++/--/-) or an implicit
  /// conversion. The rewriter derives the lvalue/form from the source text;
  /// `compound`/`postfix` are only hints.
  bool recordSimpleSite(SourceLocation b, SourceLocation o, SourceLocation e,
                        CheckedArithmeticSite::Op kind, QualType ty,
                        bool postfix = false, const Expr *operand = nullptr) {
    if (!recordArithmeticSites_ || !report_ || !ty->isIntegerType())
      return false;
    SourceManager &sm = ctx_.getSourceManager();
    if (b.isInvalid() || o.isInvalid() || e.isInvalid() || b.isMacroID() ||
        o.isMacroID() || e.isMacroID() || isHeaderLocation(b)) {
      if (strict_ && b.isValid() && b.isMacroID() &&
          !isConstantMacroExpr(operand) &&
          sm.getFileID(sm.getExpansionLoc(b)) == sm.getMainFileID())
        report(b, "arithmetic in a macro expansion cannot be instrumented; "
                  "STRICT cannot prove it safe");
      return false;
    }
    FileID main = sm.getMainFileID();
    if (sm.getFileID(b) != main || sm.getFileID(o) != main ||
        sm.getFileID(e) != main)
      return false;
    SourceLocation afterOp =
        Lexer::getLocForEndOfToken(o, 0, sm, ctx_.getLangOpts());
    SourceLocation afterEnd =
        Lexer::getLocForEndOfToken(e, 0, sm, ctx_.getLangOpts());
    if (afterOp.isInvalid() || afterEnd.isInvalid())
      return false;
    size_t begin = sm.getFileOffset(b), opOff = sm.getFileOffset(o);
    size_t end = sm.getFileOffset(afterEnd);
    size_t opLen = sm.getFileOffset(afterOp) - opOff;
    unsigned bits = ctx_.getIntWidth(ty);
    if (begin > opOff || opOff + opLen > end || bits == 0 || bits > 64)
      return false;
    PrintingPolicy pp(ctx_.getLangOpts());
    CheckedArithmeticSite site;
    site.kind = kind;
    site.begin = begin;
    site.op = opOff;
    site.opLength = opLen;
    site.end = end;
    site.bits = bits;
    site.isSigned = ty->isSignedIntegerType();
    site.type = ty.getCanonicalType().getAsString(pp);
    site.postfix = postfix;
    arithmeticSites_.push_back(std::move(site));
    return true;
  }

  /// Record an integer narrowing or floating-to-integer conversion. Used by
  /// both explicit casts and implicit conversions (initializers, assignment,
  /// call arguments and returns).
  bool recordConversionSite(const Expr *value, QualType src, QualType dst,
                            CheckedArithmeticSite::Op kind) {
    if (!recordArithmeticSites_ || !report_ || !value || !dst->isIntegerType())
      return false;
    unsigned db = ctx_.getIntWidth(dst);
    unsigned sb = kind == CheckedArithmeticSite::Op::FloatToInt
                      ? 0 : ctx_.getIntWidth(src);
    if (db == 0 || db > 64 || dst->isBooleanType() || dst->isEnumeralType())
      return false;
    if (kind == CheckedArithmeticSite::Op::Narrow) {
      if (!src->isIntegerType() || src->isBooleanType() ||
          src->isEnumeralType() || sb == 0 || sb > 64 || db >= sb)
        return false;
    } else if (kind == CheckedArithmeticSite::Op::FloatToInt) {
      if (!src->isFloatingType())
        return false;
    } else {
      return false;
    }
    SourceLocation b = value->getBeginLoc(), e = value->getEndLoc();
    if (b.isInvalid() || e.isInvalid() || b.isMacroID() || e.isMacroID() ||
        isHeaderLocation(b))
      return false;
    SourceManager &sm = ctx_.getSourceManager();
    FileID main = sm.getMainFileID();
    if (sm.getFileID(b) != main || sm.getFileID(e) != main)
      return false;
    SourceLocation after =
        Lexer::getLocForEndOfToken(e, 0, sm, ctx_.getLangOpts());
    if (after.isInvalid() || sm.getFileOffset(b) >= sm.getFileOffset(after))
      return false;
    llvm::APSInt unused;
    if (knownConst(value, unused) && kind == CheckedArithmeticSite::Op::Narrow)
      return false;
    PrintingPolicy pp(ctx_.getLangOpts());
    CheckedArithmeticSite site;
    site.kind = kind;
    site.begin = sm.getFileOffset(b);
    site.op = site.begin; // implicit conversion: no operator token
    site.opLength = 0;
    site.end = sm.getFileOffset(after);
    site.bits = db;
    site.isSigned = dst->isSignedIntegerType();
    site.srcBits = sb;
    site.srcIsSigned = src->isSignedIntegerType();
    site.type = dst.getCanonicalType().getAsString(pp);
    site.srcType = src.getCanonicalType().getAsString(pp);
    arithmeticSites_.push_back(std::move(site));
    return true;
  }

  /// Record explicit integer conversions that can truncate or change sign.
  /// The guard is value-based (extend the result back and compare), mirroring
  /// Clang's EmitIntegerTruncationCheckHelper and GSL's narrow (no sanitizer
  /// runtime, no rejection of merely narrow *types*).
  bool VisitCStyleCastExpr(CStyleCastExpr *cast) {
    if (!cast)
      return true;
    QualType dst = cast->getType();
    const Expr *value = cast->getSubExpr() ? cast->getSubExpr()->IgnoreParenImpCasts()
                                           : nullptr;
    if (!value)
      return true;
    QualType src = value->getType();
    // Only integer-narrowing / float-to-integer casts are instrumented by the
    // rewriter. Pointer casts inside C+/libc helper macros (e.g. the
    // `(T *)cplus_alloc_array(...)` in `cplus_new_array`) are not conversion
    // sites at all, so they must never trigger the macro fail-closed gate.
    bool toFloatInt = dst->isIntegerType() && src->isFloatingType();
    bool toNarrow = false;
    unsigned dbx = 0, sbx = 0;
    if (dst->isIntegerType() && src->isIntegerType() &&
        !dst->isBooleanType() && !src->isBooleanType() &&
        !dst->isEnumeralType() && !src->isEnumeralType()) {
      dbx = ctx_.getIntWidth(dst);
      sbx = ctx_.getIntWidth(src);
      toNarrow = dbx > 0 && sbx > 0 && dbx <= 64 && sbx <= 64 && dbx < sbx;
    }
    if (report_ && strict_ && (toFloatInt || toNarrow)) {
      // Fail closed where the rewriter cannot produce a correct guard.
      if (conversionInMacro(cast, value)) {
        report(cast->getBeginLoc(),
               "conversion in a macro expansion cannot be instrumented; "
               "STRICT cannot prove it safe");
        return true;
      }
      if (isNestedConversion(value)) {
        report(cast->getBeginLoc(),
               "nested conversion chain cannot be instrumented; "
               "STRICT cannot prove it safe");
        return true;
      }
      // Reject provably out-of-range compile-time constants statically, so
      // check and build agree instead of check certifying a trapping program.
      checkProvableConversion(value, src, dst,
                              toFloatInt
                                  ? CheckedArithmeticSite::Op::FloatToInt
                                  : CheckedArithmeticSite::Op::Narrow,
                              /*explicitCast=*/true);
    }
    if (!recordArithmeticSites_ || !report_)
      return true;
    if (dst->isIntegerType() && src->isFloatingType()) {
      recordConversionSite(cast, src, dst,
                           CheckedArithmeticSite::Op::FloatToInt);
      return true;
    }
    if (!dst->isIntegerType() || !src->isIntegerType() ||
        dst->isBooleanType() || src->isBooleanType() ||
        dst->isEnumeralType() || src->isEnumeralType())
      return true;
    unsigned db = ctx_.getIntWidth(dst), sb = ctx_.getIntWidth(src);
    if (db == 0 || sb == 0 || db > 64 || sb > 64)
      return true;
    // Only true truncations are checked.  Same-width signedness changes
    // (e.g. `unsigned u = (unsigned)-1`) are idiomatic C and are left alone.
    if (db >= sb)
      return true;
    SourceLocation b = value->getBeginLoc(), e = value->getEndLoc();
    if (b.isInvalid() || e.isInvalid() || b.isMacroID() || e.isMacroID() ||
        isHeaderLocation(b))
      return true;
    SourceManager &sm = ctx_.getSourceManager();
    FileID main = sm.getMainFileID();
    if (sm.getFileID(b) != main || sm.getFileID(e) != main)
      return true;
    SourceLocation afterEnd =
        Lexer::getLocForEndOfToken(e, 0, sm, ctx_.getLangOpts());
    if (afterEnd.isInvalid() || sm.getFileOffset(b) >= sm.getFileOffset(afterEnd))
      return true;
    llvm::APSInt unused;
    if (knownConst(value, unused))
      return true; // compile-time value: no runtime guard needed
    PrintingPolicy pp(ctx_.getLangOpts());
    CheckedArithmeticSite site;
    site.kind = CheckedArithmeticSite::Op::Narrow;
    site.begin = sm.getFileOffset(b);
    site.end = sm.getFileOffset(afterEnd);
    site.bits = db;
    site.isSigned = dst->isSignedIntegerType();
    site.srcBits = sb;
    site.srcIsSigned = src->isSignedIntegerType();
    site.type = dst.getCanonicalType().getAsString(pp);
    site.srcType = src.getCanonicalType().getAsString(pp);
    arithmeticSites_.push_back(std::move(site));
    return true;
  }

  /// Implicit conversions in initializers, assignments, call arguments and
  /// returns can narrow or truncate just like explicit casts. Explicit casts
  /// are handled by VisitCStyleCastExpr, so skip casts nested in one.
  bool VisitImplicitCastExpr(ImplicitCastExpr *cast) {
    if (!cast)
      return true;
    for (const auto &parent : ctx_.getParents(*cast))
      if (parent.get<ExplicitCastExpr>())
        return true;
    const Expr *sub = cast->getSubExpr();
    if (!sub)
      return true;
    CheckedArithmeticSite::Op kind = CheckedArithmeticSite::Op::Narrow;
    bool isConversion = false;
    if (cast->getCastKind() == CK_IntegralCast) {
      kind = CheckedArithmeticSite::Op::Narrow;
      isConversion = true;
    } else if (cast->getCastKind() == CK_FloatingToIntegral) {
      kind = CheckedArithmeticSite::Op::FloatToInt;
      isConversion = true;
    }
    // Same static rejection as explicit casts: constant narrowing /
    // float-to-int in initializers, assignments, arguments and returns.
    if (isConversion && report_ && strict_)
      checkProvableConversion(sub, sub->getType(), cast->getType(), kind);
    if (!recordArithmeticSites_ || !report_)
      return true;
    if (isConversion)
      recordConversionSite(sub, sub->getType(), cast->getType(), kind);
    return true;
  }

  /// Record pointer + integer arithmetic where the base object has a
  /// provable element count.  Only side-effect-free operands are rewritten so
  /// the guard never evaluates an expression twice.
  void maybeRecordCheckedPointer(const BinaryOperator *op) {
    if (!recordArithmeticSites_ || !report_ || !op) {
      return;
    }
    const auto opc = op->getOpcode();
    if (opc != BO_Add && opc != BO_Sub)
      return;
    Expr *lhs = op->getLHS(), *rhs = op->getRHS();
    const Expr *ptrE = nullptr, *idxE = nullptr;
    if (lhs->getType()->isPointerType() && rhs->getType()->isIntegerType()) {
      ptrE = lhs;
      idxE = rhs;
    } else if (rhs->getType()->isPointerType() &&
               lhs->getType()->isIntegerType()) {
      ptrE = rhs;
      idxE = lhs;
      if (opc == BO_Sub)
        return; // integer - pointer is not valid C
    } else {
      return;
    }
    auto simple = [](const Expr *e) {
      e = e->IgnoreParenImpCasts();
      return isa<DeclRefExpr>(e) || isa<IntegerLiteral>(e);
    };
    if (!simple(ptrE) || !simple(idxE))
      return;
    SourceLocation b = op->getBeginLoc(), e = op->getEndLoc();
    if (b.isInvalid() || e.isInvalid() || b.isMacroID() || e.isMacroID() ||
        isHeaderLocation(b))
      return;
    SourceManager &sm = ctx_.getSourceManager();
    FileID main = sm.getMainFileID();
    if (sm.getFileID(b) != main || sm.getFileID(e) != main)
      return;
    SourceLocation after = Lexer::getLocForEndOfToken(e, 0, sm, ctx_.getLangOpts());
    if (after.isInvalid() || sm.getFileOffset(b) >= sm.getFileOffset(after))
      return;
    long long bytes = -1;
    ExtentOrigin origin = ExtentOrigin::Unknown;
    if (const HeapAllocState *hs = heapStateOf(ptrE)) {
      bytes = hs->bytes;
      origin = hs->origin;
    } else {
      const Expr *x = ptrE->IgnoreParenImpCasts();
      if (const auto *ref = dyn_cast<DeclRefExpr>(x))
        if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl())) {
          auto it = stackArrayExtents_.find(vd->getNameAsString());
          if (it != stackArrayExtents_.end()) {
            bytes = it->second;
            origin = ExtentOrigin::Stack;
          }
        }
    }
    if (bytes <= 0 || origin == ExtentOrigin::Unknown)
      return;
    const auto *pt = ptrE->getType()->getAs<PointerType>();
    if (!pt)
      return;
    unsigned es = ctx_.getTypeSize(pt->getPointeeType()) / 8u;
    if (es == 0 || bytes % es != 0)
      return;
    long long count = bytes / es;
    auto textOf = [&](const Expr *x) {
      SourceLocation xb = x->getBeginLoc(), xe = x->getEndLoc();
      SourceLocation xa = Lexer::getLocForEndOfToken(xe, 0, sm, ctx_.getLangOpts());
      if (xb.isInvalid() || xa.isInvalid() || xb.isMacroID() || xe.isMacroID())
        return std::string();
      return Lexer::getSourceText(CharSourceRange::getCharRange(xb, xa), sm,
                                  ctx_.getLangOpts())
          .str();
    };
    CheckedPointerSite site;
    site.begin = sm.getFileOffset(b);
    site.end = sm.getFileOffset(after);
    site.base = textOf(ptrE);
    site.index = textOf(idxE);
    site.expr = Lexer::getSourceText(CharSourceRange::getCharRange(b, after), sm,
                                     ctx_.getLangOpts())
                    .str();
    site.count = std::to_string(count);
    site.negative = (opc == BO_Sub);
    if (site.base.empty() || site.index.empty() || site.expr.empty())
      return;
    pointerSites_.push_back(std::move(site));
  }

  /// Detect `(int *)constObject` and similar casts that discard const from a
  /// genuinely const object. The named object is returned for diagnostics.
  bool constStrippedTarget(const Expr *e, std::string &name) const {
    if (!e)
      return false;
    const Expr *x = e->IgnoreParenImpCasts();
    bool stripped = false;
    for (unsigned depth = 0; depth < 8; ++depth) {
      const auto *ce = dyn_cast<ExplicitCastExpr>(x);
      if (!ce || !ce->getSubExpr())
        break;
      const auto *from = ce->getSubExpr()->getType()->getAs<PointerType>();
      const auto *to = ce->getType()->getAs<PointerType>();
      if (from && to && from->getPointeeType().isConstQualified() &&
          !to->getPointeeType().isConstQualified())
        stripped = true;
      x = ce->getSubExpr()->IgnoreParenImpCasts();
    }
    if (!stripped)
      return false;
    const auto *ref = dyn_cast_or_null<DeclRefExpr>(x);
    const auto *vd = ref ? dyn_cast_or_null<VarDecl>(ref->getDecl()) : nullptr;
    if (!vd)
      return false;
    QualType obj = vd->getType();
    if (const auto *at = ctx_.getAsConstantArrayType(obj))
      obj = at->getElementType();
    else if (const auto *pt = obj->getAs<PointerType>())
      obj = pt->getPointeeType();
    if (!obj.isConstQualified())
      return false;
    name = vd->getNameAsString();
    return true;
  }

  /// Record an allocator call for runtime side-table registration.  The
  /// pointer ABI is unchanged: the generated C wraps the call in an ordinary
  /// function that returns the same pointer.
  void recordAllocSite(const CallExpr *call, long long bytes) {
    if (!recordArithmeticSites_ || !report_ || !call || bytes <= 0)
      return;
    SourceLocation b = call->getBeginLoc(), e = call->getEndLoc();
    if (b.isInvalid() || e.isInvalid() || b.isMacroID() || e.isMacroID() ||
        isHeaderLocation(b))
      return;
    SourceManager &sm = ctx_.getSourceManager();
    FileID main = sm.getMainFileID();
    if (sm.getFileID(b) != main || sm.getFileID(e) != main)
      return;
    SourceLocation after =
        Lexer::getLocForEndOfToken(e, 0, sm, ctx_.getLangOpts());
    if (after.isInvalid() || sm.getFileOffset(b) >= sm.getFileOffset(after))
      return;
    CheckedAllocSite site;
    site.begin = sm.getFileOffset(b);
    site.end = sm.getFileOffset(after);
    site.bytes = static_cast<unsigned long long>(bytes);
    if (const FunctionDecl *callee = call->getDirectCallee()) {
      std::string cn = callee->getNameAsString();
      site.isRealloc = isReallocName(cn);
      if (site.isRealloc && call->getNumArgs() >= 1) {
        SourceLocation ob = call->getArg(0)->getBeginLoc();
        SourceLocation oe = call->getArg(0)->getEndLoc();
        SourceLocation oa =
            Lexer::getLocForEndOfToken(oe, 0, sm, ctx_.getLangOpts());
        if (ob.isValid() && oa.isValid() && !ob.isMacroID() && !oe.isMacroID())
          site.argText = Lexer::getSourceText(
                             CharSourceRange::getCharRange(ob, oa), sm,
                             ctx_.getLangOpts())
                             .str();
      }
    }
    allocSites_.push_back(std::move(site));
  }

  /// Record a raw `free(p)` call so the instrumenter can route it through the
  /// runtime side table before the real deallocation.
  void recordFreeSite(const CallExpr *call) {
    if (!recordArithmeticSites_ || !report_ || !call ||
        call->getNumArgs() < 1)
      return;
    SourceLocation b = call->getBeginLoc(), e = call->getEndLoc();
    if (b.isInvalid() || e.isInvalid() || b.isMacroID() || e.isMacroID() ||
        isHeaderLocation(b))
      return;
    SourceManager &sm = ctx_.getSourceManager();
    FileID main = sm.getMainFileID();
    if (sm.getFileID(b) != main || sm.getFileID(e) != main)
      return;
    SourceLocation argB = call->getArg(0)->getBeginLoc();
    SourceLocation argE = call->getArg(0)->getEndLoc();
    SourceLocation afterArg =
        Lexer::getLocForEndOfToken(argE, 0, sm, ctx_.getLangOpts());
    SourceLocation after =
        Lexer::getLocForEndOfToken(e, 0, sm, ctx_.getLangOpts());
    if (argB.isInvalid() || afterArg.isInvalid() || after.isInvalid() ||
        sm.getFileOffset(b) >= sm.getFileOffset(after))
      return;
    CheckedAllocSite site;
    site.begin = sm.getFileOffset(b);
    site.end = sm.getFileOffset(after);
    site.argText = Lexer::getSourceText(
        CharSourceRange::getCharRange(argB, afterArg), sm, ctx_.getLangOpts())
                       .str();
    if (site.argText.empty())
      return;
    freeSites_.push_back(std::move(site));
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
        // A branch/loop guard proves a variable non-zero only until that
        // variable is assigned again.  VisitBinaryOperator erases the proof
        // on a definite assignment; without this check a stale proof survives
        // `if (d) { d = zero(); return 100 / d; }` and hides a real div-by-zero.
        if (invalidatedNonZero_.count(vd))
          return false;
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
            if (invalidatedNonZero_.count(vd))
              return false;
            auto it = loopLowerBound_.find(vd);
            if (it != loopLowerBound_.end() && it->second >= 0)
              return true;
          }
      }
    }
    return false;
  }

  /// True when `e` mentions a loop induction variable whose value is supplied
  /// by loopLowerBound_/loopIndexOverride_ rather than being a genuine
  /// compile-time constant. Such a value changes per iteration and must never
  /// be folded into a fixed allocation extent.
  bool dependsOnLoopInduction(const Expr *e, unsigned depth = 0) const {
    if (!e || depth > 12)
      return false;
    if (const auto *ref = dyn_cast<DeclRefExpr>(e))
      if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl()))
        return loopLowerBound_.count(vd) != 0 ||
               loopIndexOverride_.count(vd) != 0;
    for (const Stmt *c : e->children())
      if (const auto *ce = dyn_cast<Expr>(c))
        if (dependsOnLoopInduction(ce, depth + 1))
          return true;
    return false;
  }

  /// Record the byte extent of a heap allocation performed in a variable's
  /// initializer.  Called in both passes so the checking pass sees the same
  /// facts as the collection pass.
  void recordHeapAllocation(VarDecl *vd) {
    if (!vd || !vd->getType()->isPointerType() || !vd->getInit())
      return;
    const Expr *init = stripStmtExpr(vd->getInit());
    const auto *call = dyn_cast<CallExpr>(init);
    if (!call || !call->getDirectCallee() || call->getNumArgs() < 1)
      return;
    std::string callee = call->getDirectCallee()->getNameAsString();
    if (callee != "malloc" && callee != "calloc" && !isReallocName(callee) &&
        callee != "cplus_alloc" && callee != "cplus_zalloc" &&
        callee != "cplus_alloc_array")
      return;
    if (report_ && isReallocName(callee))
      reallocResultVars_.insert(vd->getNameAsString());
    // A size derived from a loop induction variable is NOT a compile-time
    // constant: evalByteCount()/evalConstArithmetic() can fold the induction
    // variable to its lower-bound/override value (e.g. `i` -> 0), which would
    // record a fixed 8-byte extent for `sizeof(int)*(i+2)` and then falsely
    // reject `p[i+1]` at the loop's worst index. Leave the result untracked;
    // the StaticBounds token pass covers the runtime bound.
    unsigned sizeIdx = isReallocName(callee) ? 1 : 0;
    if (call->getNumArgs() > sizeIdx &&
        dependsOnLoopInduction(call->getArg(sizeIdx)))
      return;
    // realloc(ptr, n): the new extent is argument 1, regardless of whether
    // ptr was null.  The runtime transfer helper unregisters the old block.
    if (isReallocName(callee) && call->getNumArgs() >= 2) {
      const Expr *sizeArg = call->getArg(1)->IgnoreParenImpCasts();
      long long n = 0;
      if (evalByteCount(sizeArg, n) && n > 0) {
        HeapAllocState st;
        st.bytes = n;
        st.decl = vd;
        st.origin = ExtentOrigin::Heap;
        heapAllocs_[vd->getNameAsString()] = st;
        identityAllocs_[identityKeyForDecl(vd)] = st;
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
      if (!fits64(hv) || hv.isNegative() ||
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
          strlenSized_[vd] = stringSourceKey(sl->getArg(0));
        return;
      }
    }

    long long bytes = -1;
    if (!evalByteCount(arg, bytes)) {
        return;
    }
    // calloc(count, size) and the lowered cplus_alloc_array(count, sizeof(T))
    // allocate count * size bytes, not just count.
    if ((callee == "calloc" || callee == "cplus_alloc_array") &&
        call->getNumArgs() >= 2) {
      long long elem = 1;
      if (evalByteCount(call->getArg(1)->IgnoreParenImpCasts(), elem) &&
          elem > 0 && bytes <= LLONG_MAX / elem)
        bytes *= elem;
    }
    HeapAllocState st;
    st.bytes = bytes;
    st.zeroInit = (callee == "calloc" || callee == "cplus_zalloc");
    st.decl = vd;
    st.origin = ExtentOrigin::Heap;
    heapAllocs_[vd->getNameAsString()] = st;
    // Identity view: a shadowed declaration with the same spelling must not
    // inherit the other object's extent. The name-keyed entry above still
    // wins for the declaration it actually belongs to (latest assignment).
    identityAllocs_[identityKeyForDecl(vd)] = st;
  }

  /// Materialise a stable view of an argument-slot extent for the duration
  /// of one query (mirrors the cached call-return extent above).
  const HeapAllocState *cacheSlotExtent(const SlotExtent &se) const {
    static thread_local HeapAllocState cached;
    cached.bytes = se.bytes;
    cached.origin = se.origin;
    cached.decl = nullptr;
    return &cached;
  }

  /// Resolve an extent that was concretized at a call site onto an argument
  /// slot reached by the expression: `(*pp)[i]` for a `T **` parameter or
  /// `b->p[i]` for a struct-pointer parameter.  The anchor is the parameter's
  /// slot index and the expression shape, never its spelling, so casts and
  /// local aliases at the use site cannot erase the fact.  Only the reporting
  /// pass consults these facts; the collection pass must not let them alter
  /// interprocedural effects.
  const HeapAllocState *slotExtentOf(const Expr *e) const {
    if (!report_ || currentFunction_.empty())
      return nullptr;
    const Expr *x = e->IgnoreParenImpCasts();
    while (const auto *ce = dyn_cast<ExplicitCastExpr>(x))
      x = ce->getSubExpr()->IgnoreParenImpCasts();

    auto paramKey = [&](const Expr *base, ArgSlotKey &key) {
      const auto *ref = dyn_cast<DeclRefExpr>(base);
      const auto *pv = ref ? dyn_cast<ParmVarDecl>(ref->getDecl()) : nullptr;
      if (!pv)
        return false;
      key = {currentFunction_, pv->getFunctionScopeIndex()};
      return true;
    };

    // Peel one dereference: `pp` is `int **`, `*pp` is the sized `int *`.
    if (const auto *uo = dyn_cast<UnaryOperator>(x))
      if (uo->getOpcode() == UO_Deref) {
        ArgSlotKey key;
        if (paramKey(uo->getSubExpr(), key)) {
          auto it = indirectSlotExtents_.find(key);
          if (it != indirectSlotExtents_.end() && it->second.bytes > 0)
            return cacheSlotExtent(it->second);
        }
      }

    // `b->p`: pair the pointer field with the extent carried by the
    // struct-pointer argument slot (Apple's pointer/count member keying).
    if (const auto *me = dyn_cast<MemberExpr>(x)) {
      ArgSlotKey key;
      if (paramKey(me->getBase(), key)) {
        auto fit = fieldSlotExtents_.find(key);
        if (fit != fieldSlotExtents_.end()) {
          const auto *fd = dyn_cast<FieldDecl>(me->getMemberDecl());
          auto eit = fd ? fit->second.find(fd) : fit->second.end();
          if (eit != fit->second.end() && eit->second.bytes > 0)
            return cacheSlotExtent(eit->second);
        }
      }
    }
    return nullptr;
  }

  /// True when `e` is a plain reference to a heap variable whose recorded
  /// extent still applies.
  const HeapAllocState *heapStateOf(const Expr *e) const {
    if (!e)
      return nullptr;
    e = stripStmtExpr(e);
    const Expr *x = e->IgnoreParenCasts();
    std::string identity = identityKey(e);
    if (!identity.empty()) {
      auto iit = identityAllocs_.find(identity);
      if (iit != identityAllocs_.end()) {
        if (const auto *ref = dyn_cast<DeclRefExpr>(x))
          if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl())) {
            // The name-keyed fact is updated on reassignment; prefer it only
            // when it still belongs to this exact declaration. Otherwise a
            // shadowed binding would borrow the other object's new extent.
            auto nit = heapAllocs_.find(vd->getNameAsString());
            if (nit != heapAllocs_.end() &&
                nit->second.decl == vd->getCanonicalDecl())
              return &nit->second;
          }
        return &iit->second;
      }
    }
    if (const auto *call = dyn_cast<CallExpr>(x)) {
      auto it = callReturnExtent_.find(call->getBeginLoc().getRawEncoding());
      if (it != callReturnExtent_.end()) {
        // Cached per-expression extent for pointer-returning helpers.
        static thread_local HeapAllocState cached;
        cached.bytes = it->second;
        cached.decl = nullptr;
        return &cached;
      }
      // Identity helpers (`return (T *)(void *)p;`) must not launder the
      // argument's provenance. returnedParam_ already summarizes such helpers
      // through casts; reuse it instead of adding a new name-based rule.
      for (const auto &n : calleeNames(call, call->getDirectCallee())) {
        auto rp = returnedParam_.find(n);
        if (rp == returnedParam_.end() || rp->second >= call->getNumArgs())
          continue;
        const Expr *arg = stripStmtExpr(call->getArg(rp->second));
        if (const HeapAllocState *st = heapStateOf(arg))
          return st;
        if (const auto *ref = dyn_cast<DeclRefExpr>(
                arg->IgnoreParenImpCasts()))
          if (const auto *pvd = dyn_cast<VarDecl>(ref->getDecl()))
            if (const HeapAllocState *st = heapStateOfVar(pvd))
              return st;
      }
    }
    // An explicit cast must not silently erase a proven extent.
    std::string name = varNameOf(e);
    if (name.empty())
      name = castedVarName(e);
    if (!name.empty()) {
      auto it = heapAllocs_.find(name);
      if (it != heapAllocs_.end() && it->second.decl) {
        const auto *ref = dyn_cast<DeclRefExpr>(e->IgnoreParenImpCasts());
        const auto *vd = ref ? dyn_cast<VarDecl>(ref->getDecl()) : nullptr;
        if (!vd || it->second.decl == vd ||
            it->second.decl->getCanonicalDecl() == vd->getCanonicalDecl())
          return &it->second;
      }
    }
    // The extent may be bound to an argument slot the pointer merely reaches
    // through (`*pp`, `b->p`) rather than to a variable name.
    return slotExtentOf(e);
  }

  /// Extent of a pointer variable's initializer, including the GNU statement
  /// expression produced by `cplus_new_array`. Used to carry provenance into
  /// conditional aliases and integer round-trips without inventing extents.
  const HeapAllocState *heapStateOfVar(const VarDecl *vd) const {
    if (!vd)
      return nullptr;
    auto it = heapAllocs_.find(vd->getNameAsString());
    if (it != heapAllocs_.end() && it->second.bytes > 0)
      return &it->second;
    if (!vd->getInit())
      return nullptr;
    const Expr *e = stripStmtExpr(vd->getInit());
    while (const auto *ec = dyn_cast<ExplicitCastExpr>(e))
      e = stripStmtExpr(ec->getSubExpr());
    const auto *call = dyn_cast<CallExpr>(e);
    if (!call || !call->getDirectCallee())
      return nullptr;
    long long bytes = -1;
    for (const auto &n : calleeNames(call, call->getDirectCallee())) {
      if (n == "cplus_alloc_array" && call->getNumArgs() >= 2) {
        long long count = -1, size = -1;
        if (evalByteCount(call->getArg(0), count) &&
            evalByteCount(call->getArg(1), size) && count > 0 && size > 0 &&
            count <= LLONG_MAX / size)
          bytes = count * size;
      } else if ((n == "malloc" || n == "calloc" || n == "cplus_alloc" ||
                  n == "cplus_zalloc") &&
                 call->getNumArgs() >= 1) {
        if (evalByteCount(call->getArg(0), bytes) && bytes > 0 &&
            (n == "calloc" || n == "cplus_zalloc") && call->getNumArgs() >= 2) {
          long long elem = 1;
          if (evalByteCount(call->getArg(1), elem) && elem > 0 &&
              bytes <= LLONG_MAX / elem)
            bytes *= elem;
        }
      }
      if (bytes > 0)
        break;
    }
    if (bytes <= 0)
      return nullptr;
    static thread_local HeapAllocState cached;
    cached.bytes = bytes;
    cached.origin = ExtentOrigin::Heap;
    cached.decl = vd;
    return &cached;
  }

  /// Resolve a subscript/offset index to a compile-time value.  The worst
  /// case index of an enclosing counted loop is preferred over a local
  /// constant, because a loop induction variable is not actually constant.
  bool evalIndexValue(const Expr *idx, long long &out) const {
    if (!idx)
      return false;
    // A postfix/prefix increment used as a subscript (`a[i++]`) yields the
    // counter's current value. Resolve it through the enclosing loop's
    // induction override instead of treating it as an opaque expression.
    if (const auto *uo = dyn_cast<UnaryOperator>(idx->IgnoreParenImpCasts()))
      if (uo->isIncrementDecrementOp())
        return evalIndexValue(uo->getSubExpr(), out);
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

  /// Recognise `while (i < N)` / `while (i <= N)` with a constant bound, a
  /// constant starting value and a unit increment in the body. The worst-case
  /// value of the induction variable is then used for subscripts in the body.
  bool parseWhileLoopBound(const WhileStmt *s, const VarDecl *&vd,
                           long long &maxIndex) const {
    if (!s || !s->getCond())
      return false;
    const auto *bo =
        dyn_cast<BinaryOperator>(s->getCond()->IgnoreParenImpCasts());
    if (!bo || (bo->getOpcode() != BO_LT && bo->getOpcode() != BO_LE))
      return false;
    const auto *ref =
        dyn_cast<DeclRefExpr>(bo->getLHS()->IgnoreParenImpCasts());
    const auto *ivd = ref ? dyn_cast<VarDecl>(ref->getDecl()) : nullptr;
    if (!ivd || !ivd->getType()->isIntegerType())
      return false;
    llvm::APSInt bv;
    if (!knownConst(bo->getRHS(), bv) || !fits64(bv))
      return false;
    long long bound =
        bv.isNegative() ? bv.getSExtValue() : (long long)bv.getZExtValue();
    long long lower = 0;
    if (ivd->getInit()) {
      Expr::EvalResult ir;
      if (!ivd->getInit()->EvaluateAsInt(ir, ctx_) || !ir.Val.isInt() ||
          !fits64(ir.Val.getInt()))
        return false;
      lower = ir.Val.getInt().getSExtValue();
    } else {
      auto lit = loopLowerBound_.find(ivd);
      if (lit == loopLowerBound_.end())
        return false;
      lower = lit->second;
    }
    if (bound < 0 || lower < 0 || lower >= bound)
      return false;
    maxIndex = (bo->getOpcode() == BO_LT) ? bound - 1 : bound;
    if (maxIndex < lower || !bodyHasUnitIncrement(s->getBody(), ivd))
      return false;
    vd = ivd;
    return true;
  }

  /// True when the loop body increments `vd` by one (`i++`, `++i`, `i += 1`),
  /// including an increment nested in a subscript's index expression.
  bool bodyHasUnitIncrement(const Stmt *body, const VarDecl *vd,
                            unsigned depth = 0) const {
    if (!body || !vd || depth > 12)
      return false;
    if (const auto *e = dyn_cast<Expr>(body)) {
      const Expr *x = e->IgnoreParenImpCasts();
      if (const auto *uo = dyn_cast<UnaryOperator>(x))
        if (uo->isIncrementOp())
          if (const auto *ref = dyn_cast<DeclRefExpr>(
                  uo->getSubExpr()->IgnoreParenImpCasts()))
            if (ref->getDecl() == vd)
              return true;
      if (const auto *bo = dyn_cast<BinaryOperator>(x))
        if (bo->getOpcode() == BO_AddAssign) {
          Expr::EvalResult sr;
          if (bo->getRHS()->EvaluateAsInt(sr, ctx_) && sr.Val.isInt() &&
              sr.Val.getInt() == 1)
            if (const auto *ref = dyn_cast<DeclRefExpr>(
                    bo->getLHS()->IgnoreParenImpCasts()))
              if (ref->getDecl() == vd)
                return true;
        }
    }
    for (const Stmt *c : body->children())
      if (bodyHasUnitIncrement(c, vd, depth + 1))
        return true;
    return false;
  }

  /// True when an index expression mentions an induction variable whose value
  /// is supplied by loopIndexOverride_ (`a[i++]`, `a[i + 1]`, ...).
  bool indexMentionsLoopOverride(const Expr *e, unsigned depth = 0) const {
    if (!e || depth > 12)
      return false;
    const Expr *x = e->IgnoreParenImpCasts();
    if (const auto *ref = dyn_cast<DeclRefExpr>(x))
      if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl()))
        if (loopIndexOverride_.count(vd) != 0)
          return true;
    for (const Stmt *c : x->children())
      if (const auto *ce = dyn_cast<Expr>(c))
        if (indexMentionsLoopOverride(ce, depth + 1))
          return true;
    return false;
  }

  /// Collect the constant values of every possibly-selected arm of a
  /// conditional index. A literal condition selects one arm; any condition
  /// that is not an integer constant expression keeps both arms so STRICT
  /// fails closed on an out-of-bounds arm.
  void collectConditionalIndexArms(const Expr *e, std::vector<long long> &out,
                                   unsigned depth) const {
    if (!e || depth > 8)
      return;
    const Expr *x = e->IgnoreParenImpCasts();
    if (const auto *co = dyn_cast<ConditionalOperator>(x)) {
      Expr::EvalResult r;
      if (co->getCond() && co->getCond()->EvaluateAsInt(r, ctx_) &&
          r.Val.isInt())
        collectConditionalIndexArms(
            r.Val.getInt().isZero() ? co->getFalseExpr() : co->getTrueExpr(),
            out, depth + 1);
      else {
        collectConditionalIndexArms(co->getTrueExpr(), out, depth + 1);
        collectConditionalIndexArms(co->getFalseExpr(), out, depth + 1);
      }
      return;
    }
    long long v = 0;
    if (evalIndexValue(x, v))
      out.push_back(v);
  }

  static bool indexHasConditional(const Expr *e, unsigned depth = 0) {
    if (!e || depth > 8)
      return false;
    const Expr *x = e->IgnoreParenImpCasts();
    if (isa<ConditionalOperator>(x))
      return true;
    for (const Stmt *child : x->children())
      if (const auto *ce = dyn_cast<Expr>(child))
        if (indexHasConditional(ce, depth + 1))
          return true;
    return false;
  }

  /// STRICT rejects a subscript whose conditional index has a constant arm
  /// outside the addressed object, including nested conditionals.
  void checkConditionalIndexArms(const ArraySubscriptExpr *sub) {
    if (!sub || !indexHasConditional(sub->getIdx()))
      return;
    std::vector<long long> arms;
    collectConditionalIndexArms(sub->getIdx(), arms, 0);
    if (arms.empty())
      return;
    const Expr *base = sub->getBase()->IgnoreParenImpCasts();
    unsigned long long count = 0;
    if (const auto *at = ctx_.getAsConstantArrayType(base->getType()))
      count = at->getSize().getZExtValue();
    else if (const HeapAllocState *st = heapStateOf(base)) {
      const auto *pt = base->getType()->getAs<PointerType>();
      unsigned es =
          pt ? (unsigned)(ctx_.getTypeSize(pt->getPointeeType()) / 8u) : 0;
      if (es && st->bytes > 0)
        count = (unsigned long long)st->bytes / es;
    }
    if (!count)
      return;
    std::string name = varNameOf(base);
    if (name.empty())
      name = "array";
    for (long long v : arms) {
      if (v < 0 || (unsigned long long)v >= count) {
        report(sub->getBeginLoc(),
               "out-of-bounds access: index " + std::to_string(v) +
                   " reaches past array '" + name + "' of length " +
                   std::to_string(count));
        return;
      }
    }
  }

  /// Reject array subscripts / pointer offsets beyond a tracked heap extent.
  void checkHeapSubscript(const ArraySubscriptExpr *sub, SourceLocation loc) {
    const Expr *baseExpr = sub->getBase()->IgnoreParenImpCasts();
    const HeapAllocState *st = heapStateOf(baseExpr);
    unsigned long long fieldOffsetBytes = 0;
    // A downcast member access (`((Large *)small)->payload[3]`) hides the
    // allocation extent behind a MemberExpr. Resolve the underlying object
    // and account for the field's byte offset before comparing extents.
    if (const auto *me = dyn_cast<MemberExpr>(baseExpr)) {
      if (const auto *fd = dyn_cast<FieldDecl>(me->getMemberDecl()))
        fieldOffsetBytes =
            (unsigned long long)(ctx_.getFieldOffset(fd) / 8u);
      if (const auto *ref = dyn_cast<DeclRefExpr>(
              me->getBase()->IgnoreParenImpCasts()))
        st = heapStateOf(ref);
    }
    if (!st)
      return;
    if (st->provenanceLost) {
      warnAt(loc, "pointer provenance lost before this subscript; the "
                  "addressable extent cannot be proven");
      return;
    }
    long long idx = 0;
    if (!evalIndexValue(sub->getIdx(), idx))
      return;
    std::string name = varNameOf(sub->getBase());
    if (name.empty()) {
      // An argument-slot extent is reached through `*pp` or `b->p`, so there
      // is no plain variable spelling to name; describe the slot instead.
      const Expr *b = sub->getBase()->IgnoreParenImpCasts();
      if (const auto *uo = dyn_cast<UnaryOperator>(b))
        if (uo->getOpcode() == UO_Deref)
          name = "*" + varNameOf(uo->getSubExpr());
      if (name.empty()) {
        std::string fk = lvalueKey(b);
        if (!fk.empty())
          name = "field '" + fk + "'";
      }
      if (name.empty())
        name = "argument-derived pointer";
    }
    if (idx < 0) {
      report(loc, std::string("negative array index on ") +
                      extentLabel(st->origin) + " '" + name + "'");
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
         elem->isPointerType() || elem->isRecordType()) &&
        !elem->isDependentType() && !elem->isIncompleteType())
      elemSize = (unsigned long long)(ctx_.getTypeSize(elem) / 8u);
    if (elemSize == 0)
      return;
    unsigned long long off = 0;
    // A wrapped 64-bit product would make an enormous index look in-bounds.
    if (!mulAddFits(fieldOffsetBytes, (unsigned long long)idx, elemSize, off) ||
        off >= (unsigned long long)st->bytes)
      report(loc, "array index " + std::to_string(idx) + " exceeds " +
                      extentLabel(st->origin) + " '" + name + "' of " +
                      std::to_string(st->bytes) + " bytes");
  }

  /// Accumulate a chain of `array +/- K` operations into a base array and a
  /// total constant offset.  `a + 1 + 4` is `a + 5`; a one-level check missed
  /// it and allowed `*p` past the end.
  bool onePastOffset(const Expr *e, const VarDecl *&base, long long &total,
                     unsigned depth) const {
    if (!e || depth > 8)
      return false;
    e = e->IgnoreParenImpCasts();
    if (const auto *ref = dyn_cast<DeclRefExpr>(e)) {
      const auto *vd = dyn_cast<VarDecl>(ref->getDecl());
      if (vd && ctx_.getAsConstantArrayType(vd->getType())) {
        base = vd;
        return true;
      }
      return false;
    }
    const auto *bo = dyn_cast<BinaryOperator>(e);
    if (!bo || (bo->getOpcode() != BO_Add && bo->getOpcode() != BO_Sub))
      return false;
    llvm::APSInt k;
    if (knownConst(bo->getRHS(), k)) {
      long long c = k.getSExtValue();
      if (bo->getOpcode() == BO_Sub)
        c = -c;
      if (onePastOffset(bo->getLHS(), base, total, depth + 1)) {
        total += c;
        return true;
      }
    }
    if (bo->getOpcode() == BO_Add && knownConst(bo->getLHS(), k))
      if (onePastOffset(bo->getRHS(), base, total, depth + 1)) {
        total += k.getSExtValue();
        return true;
      }
    return false;
  }

  /// True when `e` is `array + N` (including chained offsets) with N exactly
  /// one past a fixed array's end.  Dereferencing such a pointer is undefined.
  bool isOnePastEndExpr(const Expr *e) const {
    const VarDecl *base = nullptr;
    long long total = 0;
    if (!onePastOffset(e, base, total, 0) || !base)
      return false;
    const auto *at = ctx_.getAsConstantArrayType(base->getType());
    return at && total == (long long)at->getSize().getZExtValue();
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
    // Chained offsets such as `*(a + 1 + 4)` have no named pointer variable,
    // so the onePastEnd_ set cannot catch them; check the expression itself.
    if (isOnePastEndExpr(sub)) {
      report(loc, "dereference one past the end of array via pointer "
                  "arithmetic");
      return;
    }
    const HeapAllocState *st = heapStateOf(sub);
    if (st) {
      if (st->provenanceLost) {
        warnAt(loc, "pointer provenance lost before this dereference; the "
                    "addressable extent cannot be proven");
        return;
      }
      std::string name = varNameOf(sub);
      if (st->bytes == 0) {
        report(loc, std::string("dereference of zero-size ") +
                        extentLabel(st->origin) + " '" + name +
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
                     elem->isFloatingType() || elem->isEnumeralType() ||
                     elem->isPointerType() || elem->isRecordType()) &&
                    !elem->isDependentType() && !elem->isIncompleteType())
                  es = (unsigned long long)(ctx_.getTypeSize(elem) / 8u);
                if (es != 0) {
                  unsigned long long byteOff = 0;
                  bool exceeds = off < 0;
                  if (!exceeds &&
                      !mulAddFits(0ULL, (unsigned long long)off, es, byteOff))
                    exceeds = true;
                  if (!exceeds &&
                      byteOff >= (unsigned long long)baseSt->bytes)
                    exceeds = true;
                  if (exceeds)
                    report(loc, "pointer offset " + std::to_string(off) +
                                    " exceeds " +
                                    extentLabel(baseSt->origin) + " '" +
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

  /// Canonical identity of a string source. A pointer alias
  /// (`const char *s2 = s`) and identical string literals produce the same
  /// key as the expression passed to `strlen`; a VarDecl pointer (not a
  /// mutable name group) keeps the key stable across the AST walk.
  std::string stringSourceKey(const Expr *e, unsigned depth = 0) const {
    if (!e || depth > 4)
      return std::string();
    e = e->IgnoreParenImpCasts();
    if (const auto *sl = dyn_cast<StringLiteral>(e))
      return "literal:" + sl->getBytes().str();
    if (const auto *bo = dyn_cast<BinaryOperator>(e)) {
      llvm::APSInt k;
      if ((bo->getOpcode() == BO_Add || bo->getOpcode() == BO_Sub) &&
          knownConst(bo->getRHS(), k) && k.isZero())
        return stringSourceKey(bo->getLHS(), depth + 1);
    }
    if (const auto *uo = dyn_cast<UnaryOperator>(e)) {
      if (uo->getOpcode() == UO_AddrOf) {
        const Expr *inner = uo->getSubExpr()->IgnoreParenImpCasts();
        if (const auto *ase = dyn_cast<ArraySubscriptExpr>(inner)) {
          llvm::APSInt k;
          if (knownConst(ase->getIdx(), k) && k.isZero())
            return stringSourceKey(ase->getBase(), depth + 1);
        }
      }
    }
    const auto *ref = dyn_cast<DeclRefExpr>(e);
    const auto *vd = ref ? dyn_cast<VarDecl>(ref->getDecl()) : nullptr;
    if (!vd)
      return std::string();
    // Follow a stable initializer alias, but not a variable that has since
    // been reassigned to a different string.
    if (vd->getInit() && !assignedVars_.count(vd->getNameAsString())) {
      const Expr *init = vd->getInit()->IgnoreParenImpCasts();
      if (isa<DeclRefExpr>(init) || isa<StringLiteral>(init))
        return stringSourceKey(init, depth + 1);
    }
    return "var:" +
           std::to_string(reinterpret_cast<uintptr_t>(vd->getCanonicalDecl()));
  }

  /// `malloc(strlen(src))` followed by strcpy/strcat overflows by the NUL.
  void checkStrcpyNul(const CallExpr *call) {
    if (call->getNumArgs() < 2)
      return;
    // Resolve `dst + 0` and `&dst[0]` through the shared destination logic.
    const StringDestination dst =
        stringDestination(call->getArg(0)->IgnoreParenImpCasts());
    const VarDecl *vd = dst.var;
    if (!vd || dst.offset != 0)
      return;
    auto it = strlenSized_.find(vd);
    if (it == strlenSized_.end())
      return;
    std::string srcKey = stringSourceKey(call->getArg(1));
    if (srcKey.empty() || srcKey == it->second)
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
          // &vd is the address of the variable's own storage (stack, global
          // or TLS), never the heap block pointer held inside vd. This also
          // applies when vd is itself a pointer variable (`free(&ptr)`).
          if (vd) {
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

  /// Mark a name released and propagate the taint to every view of the same
  /// object: simple/cast aliases, struct-field stores, union fields, integer
  /// laundering round-trips and global pointers.  One-level name tracking let
  /// `uintptr_t u=(uintptr_t)p; int *q=(int*)u; free(p); return *q;` slip by.
  void markReleased(const std::string &name) {
    if (name.empty())
      return;
    std::set<std::string> work{name}, seen;
    while (!work.empty()) {
      std::string cur = *work.begin();
      work.erase(work.begin());
      if (!seen.insert(cur).second)
        continue;
      releasedVars_.insert(cur);
      auto ait = aliasGroups_.find(cur);
      if (ait != aliasGroups_.end())
        work.insert(ait->second.begin(), ait->second.end());
      // Allocation identity: every view of the same heap object, including
      // global `void *` handles and values loaded back through them, is
      // dangling once one view is released.
      const std::string id = allocIdOf(cur);
      if (!id.empty()) {
        auto mit = allocIdentityMembers_.find(id);
        if (mit != allocIdentityMembers_.end())
          work.insert(mit->second.begin(), mit->second.end());
        for (const auto &kv : globalAllocPointsTo_)
          if (kv.second.count(id)) {
            work.insert(kv.first);
            auto gpit = globalPointsTo_.find(kv.first);
            if (gpit != globalPointsTo_.end())
              work.insert(gpit->second.begin(), gpit->second.end());
          }
      }
      for (const auto &entry : borrowOwner_)
        if (entry.second == cur || entry.first == cur)
          work.insert(entry.first == cur ? entry.second : entry.first);
      auto git = globalPointsTo_.find(cur);
      if (git != globalPointsTo_.end())
        work.insert(git->second.begin(), git->second.end());
      for (const auto &kv : globalPointsTo_)
        if (kv.second.count(cur))
          work.insert(kv.first);
      auto pit = provenanceSource_.find(cur);
      if (pit != provenanceSource_.end())
        work.insert(pit->second);
      for (const auto &kv : provenanceSource_)
        if (kv.second == cur)
          work.insert(kv.first);
      auto fit = fieldAliases_.find(cur);
      if (fit != fieldAliases_.end())
        work.insert(fit->second.begin(), fit->second.end());
      for (const auto &kv : fieldAliases_)
        if (kv.second.count(cur))
          work.insert(kv.first);
    }
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
    // A fresh value assigned to one view of an object revives all aliases.
    for (const auto &kv : globalPointsTo_)
      if (kv.first == name || kv.second.count(name)) {
        releasedVars_.erase(kv.first);
        for (const auto &n : kv.second)
          releasedVars_.erase(n);
      }
    for (const auto &kv : provenanceSource_)
      if (kv.first == name || kv.second == name) {
        releasedVars_.erase(kv.first);
        releasedVars_.erase(kv.second);
      }
  }

  // Issue #223: branch and loop visitors fork the release state before
  // walking an arm (so a free inside the arm is visible to a later deref or
  // double free in that same arm) and join it at the merge point. A release
  // is definite only when every continuing arm released the owner; a release
  // on any arm makes it a possible release afterwards.
  struct ReleaseSnapshot {
    std::set<std::string> releasedVars, maybeReleasedVars, releasedFields;
  };

  ReleaseSnapshot snapshotReleaseState() const {
    return {releasedVars_, maybeReleasedVars_, releasedFields_};
  }

  void restoreReleaseState(const ReleaseSnapshot &s) {
    releasedVars_ = s.releasedVars;
    maybeReleasedVars_ = s.maybeReleasedVars;
    releasedFields_ = s.releasedFields;
  }

  void joinReleaseStates(const ReleaseSnapshot &before,
                         const std::vector<ReleaseSnapshot> &arms) {
    restoreReleaseState(before);
    for (const auto &arm : arms) {
      for (auto it = releasedVars_.begin(); it != releasedVars_.end();)
        if (!arm.releasedVars.count(*it))
          it = releasedVars_.erase(it);
        else
          ++it;
      for (auto it = releasedFields_.begin(); it != releasedFields_.end();)
        if (!arm.releasedFields.count(*it))
          it = releasedFields_.erase(it);
        else
          ++it;
    }
    for (const auto &arm : arms) {
      maybeReleasedVars_.insert(arm.maybeReleasedVars.begin(),
                                arm.maybeReleasedVars.end());
      maybeReleasedVars_.insert(arm.releasedVars.begin(),
                                arm.releasedVars.end());
    }
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

  /// `cplus_new_array` is a GNU statement expression whose final expression
  /// is the allocation call. Peel that wrapper so provenance can see through
  /// the macro without depending on its spelling.
  static const Expr *stripStmtExpr(const Expr *e) {
    if (!e)
      return nullptr;
    e = e->IgnoreParenImpCasts();
    // Explicit casts are part of the allocation shape (`(int *)malloc(...)`),
    // so peel them as well. IgnoreParenImpCasts alone would hide every C-style
    // allocator cast from recordHeapAllocation().
    while (const auto *ec = dyn_cast<ExplicitCastExpr>(e))
      e = stripStmtExpr(ec->getSubExpr());
    if (const auto *se = dyn_cast<StmtExpr>(e)) {
      const CompoundStmt *cs = se->getSubStmt();
      if (cs && cs->size() != 0)
        if (const auto *last = dyn_cast<Expr>(cs->body_back()))
          return stripStmtExpr(last);
    }
    return e;
  }

  static std::string identityKeyForDecl(const ValueDecl *vd) {
    return vd ? "V" + std::to_string(reinterpret_cast<uintptr_t>(
                    vd->getCanonicalDecl()))
              : std::string();
  }

  /// Identity key for a pointer lvalue. Unlike lvalueKey() this uses canonical
  /// declaration pointers, never source spellings, so shadowing and casts
  /// cannot make two distinct objects share provenance (or split one object
  /// into several unrelated aliases).
  std::string identityKey(const Expr *expr) const {
    if (!expr)
      return std::string();
    const Expr *e = expr->IgnoreParenImpCasts();
    if (const auto *ref = dyn_cast<DeclRefExpr>(e))
      if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl()))
        return "V" + std::to_string(reinterpret_cast<uintptr_t>(
                         vd->getCanonicalDecl()));
    if (const auto *me = dyn_cast<MemberExpr>(e)) {
      std::string base = identityKey(me->getBase());
      const auto *fd = dyn_cast<FieldDecl>(me->getMemberDecl());
      if (base.empty() || !fd)
        return std::string();
      return base + ".F" +
             std::to_string(
                 reinterpret_cast<uintptr_t>(fd->getCanonicalDecl()));
    }
    if (const auto *uo = dyn_cast<UnaryOperator>(e))
      if (uo->getOpcode() == UO_Deref)
        return "*" + identityKey(uo->getSubExpr());
    return std::string();
  }

  bool isReleased(const Expr *expr) const {
    std::string key = lvalueKey(expr);
    std::string name = varNameOf(expr);
    std::string cname = castedVarName(expr);
    if (releasedVars_.count(name) != 0 || releasedVars_.count(cname) != 0 ||
        (!key.empty() && (releasedVars_.count(key) != 0 ||
                          releasedFields_.count(key) != 0)))
      return true;
    // A field alias of an already-released field is also dangling.
    auto it = fieldAliases_.find(key);
    if (it != fieldAliases_.end())
      for (const auto &alias : it->second)
        if (releasedFields_.count(alias) || releasedVars_.count(alias))
          return true;
    // Reverse direction: a local alias loaded from a field/array element
    // (`int *q = a[0]`) is dangling when that element is released.
    for (const auto &kv : fieldAliases_)
      if (kv.second.count(name) &&
          (releasedFields_.count(kv.first) || releasedVars_.count(kv.first)))
        return true;
    // Integer-laundered pointer: `(int *)u` where u came from freed p.
    auto pit = provenanceSource_.find(cname);
    if (pit != provenanceSource_.end() && releasedVars_.count(pit->second))
      return true;
    // Tagged / masked laundering: `(int *)(u & ~(uintptr_t)1)`, `u ^ 1`,
    // `(void *)(uintptr_t)p` etc. The untagging BinaryOperator has no single
    // variable name, so the name-based checks above miss it even though the
    // release-call path (checkStaleIntegerAliasArgs) already recognises it.
    if (!staleIntegerAliasSource(expr).empty())
      return true;
    // A freed value reachable through a global pointer invalidates its aliases
    // and vice versa (s06/s14/s19).
    auto git = globalPointsTo_.find(cname);
    if (git != globalPointsTo_.end())
      for (const auto &n : git->second)
        if (releasedVars_.count(n))
          return true;
    // Union punning: every field view aliases the same bytes.
    size_t dot = key.find('.');
    if (dot != std::string::npos) {
      std::string base = key.substr(0, dot);
      if (unionVars_.count(base))
        for (const auto &field : releasedFields_)
          if (field.rfind(base + ".", 0) == 0)
            return true;
    }
    return false;
  }

  /// True when `e` still denotes the object that was released, rather than a
  /// fresh value.  At an assignment site this distinguishes "the released
  /// pointer itself" (a plain reference, a cast, an integer-laundered view,
  /// an alias field, or pointer arithmetic on any of those) from a genuinely
  /// new value that makes the destination live again.
  bool derivedFromReleasedPointer(const Expr *e, unsigned depth = 0) const {
    if (!e || depth > 6)
      return false;
    if (isReleased(e))
      return true;
    if (!staleIntegerAliasSource(e).empty())
      return true;
    const Expr *canon = canonicalPointer(e);
    if (canon && canon != e && isReleased(canon))
      return true;
    std::string src = pointerSourceOf(e);
    if (!src.empty() && releasedVars_.count(src))
      return true;
    const Expr *x = e->IgnoreParenImpCasts();
    if (const auto *ce = dyn_cast<CastExpr>(x))
      return derivedFromReleasedPointer(ce->getSubExpr(), depth + 1);
    if (const auto *uo = dyn_cast<UnaryOperator>(x)) {
      // `&x` is a fresh pointer to x, never a view of a released value.
      if (uo->getOpcode() == UO_AddrOf)
        return false;
      return derivedFromReleasedPointer(uo->getSubExpr(), depth + 1);
    }
    if (const auto *bo = dyn_cast<BinaryOperator>(x))
      return derivedFromReleasedPointer(bo->getLHS(), depth + 1) ||
             derivedFromReleasedPointer(bo->getRHS(), depth + 1);
    if (const auto *co = dyn_cast<ConditionalOperator>(x))
      return derivedFromReleasedPointer(co->getTrueExpr(), depth + 1) ||
             derivedFromReleasedPointer(co->getFalseExpr(), depth + 1);
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

  /// The stack object an expression ultimately designates, using Clang AST
  /// node structure rather than spelling: `&x`, `s.field`, `a[i]`, and their
  /// casted/parenthesized forms all resolve to the same VarDecl identity.
  const VarDecl *localStackOwner(const Expr *e, unsigned depth = 0,
                                 bool allowPointerBase = false) const {
    if (!e || depth > 8)
      return nullptr;
    e = e->IgnoreParenCasts();
    if (const auto *uo = dyn_cast<UnaryOperator>(e))
      if (uo->getOpcode() == UO_AddrOf)
        return localStackOwner(uo->getSubExpr(), depth + 1, true);
    if (const auto *ase = dyn_cast<ArraySubscriptExpr>(e))
      return localStackOwner(ase->getBase(), depth + 1);
    if (const auto *me = dyn_cast<MemberExpr>(e)) {
      // A function pointer stored in a local struct is a value copied from
      // its initializer, not a borrow of the struct's storage. Returning it
      // preserves the native C ABI and is safe (abi_safety/fnptr_representation).
      QualType mt = me->getType();
      if (const auto *fpt = mt->getAs<PointerType>())
        if (fpt->getPointeeType()->isFunctionProtoType())
          return nullptr;
      return localStackOwner(me->getBase(), depth + 1);
    }
    if (const auto *ref = dyn_cast<DeclRefExpr>(e)) {
      const auto *vd = dyn_cast<VarDecl>(ref->getDecl());
      if (!vd || !vd->hasLocalStorage())
        return nullptr;
      // `self->field` through a pointer parameter/local designates the
      // pointee, not the pointer variable. Only `&p` (address-of) makes the
      // pointer variable itself the dying stack object.
      if (!allowPointerBase && vd->getType()->isPointerType())
        return nullptr;
      return vd;
    }
    return nullptr;
  }

  /// Find the borrow owner carried by a return expression.  Casts (including
  /// C-style, qualifier and void-pointer conversions), parentheses, comma
  /// expressions, conditional arms and helpers summarized as returning a
  /// pointer parameter are all traversed semantically; no token pattern or
  /// variable spelling is used to decide whether a borrow escapes.
  const VarDecl *borrowOwnerInReturn(const Expr *e, const VarDecl **borrowVar,
                                     std::string *borrowName,
                                     unsigned depth = 0) const {
    if (!e || depth > 12)
      return nullptr;
    e = e->IgnoreParenCasts();
    if (const auto *ref = dyn_cast<DeclRefExpr>(e)) {
      const auto *vd = dyn_cast<VarDecl>(ref->getDecl());
      if (!vd)
        return nullptr;
      auto it = returnBorrowOwner_.find(vd);
      if (it == returnBorrowOwner_.end())
        return nullptr;
      if (borrowVar)
        *borrowVar = vd;
      if (borrowName)
        *borrowName = it->second.second; // original source spelling
      return it->second.first;
    }
    if (const auto *co = dyn_cast<ConditionalOperator>(e)) {
      if (const VarDecl *o = borrowOwnerInReturn(co->getTrueExpr(), borrowVar,
                                                 borrowName, depth + 1))
        return o;
      return borrowOwnerInReturn(co->getFalseExpr(), borrowVar, borrowName,
                                 depth + 1);
    }
    if (const auto *bo = dyn_cast<BinaryOperator>(e))
      if (bo->getOpcode() == BO_Comma)
        return borrowOwnerInReturn(bo->getRHS(), borrowVar, borrowName,
                                   depth + 1);
    if (const auto *call = dyn_cast<CallExpr>(e)) {
      for (const auto &cn : calleeNames(call, call->getDirectCallee())) {
        auto rp = returnedParam_.find(cn);
        if (rp != returnedParam_.end() && rp->second < call->getNumArgs())
          if (const VarDecl *o = borrowOwnerInReturn(call->getArg(rp->second),
                                                     borrowVar, borrowName,
                                                     depth + 1))
            return o;
      }
      return nullptr;
    }
    // A directly returned address of a stack object (`return &local;`) is
    // diagnosed by the existing frame-escape check; do not duplicate it here.
    if (isa<UnaryOperator>(e))
      return nullptr;
    return localStackOwner(e);
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
    if (!vd)
      return false;
    // Strip explicit casts as well (`int *q = (int *)realloc(...)`), and
    // resolve indirect realloc through a function pointer (T1_indirect_realloc
    // calls realloc via `void *(*r)(void*,size_t) = realloc`).  Also accept a
    // reassignment `q = realloc(...)` of an already-declared variable, which
    // is the shape used by stdlib/error.hp.
    const Expr *initE = vd->getInit() ? vd->getInit()->IgnoreParenImpCasts()
                                      : nullptr;
    while (const auto *ce = dyn_cast_or_null<ExplicitCastExpr>(initE))
      initE = ce->getSubExpr()->IgnoreParenImpCasts();
    const CallExpr *call = dyn_cast_or_null<CallExpr>(initE);
    if (!call) {
      auto ait = reallocAssignCalls_.find(vd->getCanonicalDecl());
      call = ait == reallocAssignCalls_.end() ? nullptr : ait->second;
    }
    if (!call || call->getNumArgs() < 1)
      return false;
    bool isRealloc = false;
    for (const auto &cn : calleeNames(call, call->getDirectCallee()))
      if (isReallocName(cn)) {
        isRealloc = true;
        break;
      }
    if (!isRealloc)
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
    // Stamp the may-release at the END of the arm: a free inside this very
    // arm is not a second free, and stamping at the arm's first token made
    // the first free look like a release from an earlier branch path.
    unsigned pos = body->getEndLoc().getRawEncoding();
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
        if (!tmp.empty())
          handledReallocVars_.insert(tmp);
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
    // Issue #224: only `==` against a null constant proves non-nullness. An
    // `!=` comparison (or a comparison of two non-null values) proves no such
    // fact, so it must not erase the null state of the named variable.
    auto nullEqualityVar = [&](const Expr *cond) -> std::string {
      const auto *bo = dyn_cast_or_null<BinaryOperator>(
          cond ? cond->IgnoreParenImpCasts() : nullptr);
      if (!bo || bo->getOpcode() != BO_EQ)
        return std::string();
      if (isNullConstant(bo->getRHS()))
        return varNameOf(bo->getLHS());
      if (isNullConstant(bo->getLHS()))
        return varNameOf(bo->getRHS());
      return std::string();
    };
    if (report_ && s->getThen()) {
      std::string n;
      if (negatedGuard)
        n = varNameOf(
            cast<UnaryOperator>(s->getCond()->IgnoreParenImpCasts())
                ->getSubExpr());
      if (n.empty())
        n = nullEqualityVar(s->getCond());
      if (!n.empty()) {
        bool assigns = false;
        std::function<void(const Stmt *)> scan = [&](const Stmt *node) {
          if (!node || assigns)
            return;
          if (const auto *bo = dyn_cast<BinaryOperator>(node))
            // `p = NULL` does not make p non-null; only a fresh value does.
            if (bo->isAssignmentOp() && varNameOf(bo->getLHS()) == n &&
                !isNullConstant(bo->getRHS()->IgnoreParenCasts()))
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
          if (containsReturn(s->getThen())) {
            keepErase = true;
            keepEraseVar = condVar;
          }
        }
      if (containsReturn(s->getThen())) {
        std::string n = nullEqualityVar(s->getCond());
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
    // `if (x == 0) return;` or `if (x == 0 || y == 0) return;` proves the
    // named variables non-zero for all subsequent statements.
    if (report_ && !s->getElse() && containsReturn(s->getThen())) {
      auto fromZeroEquality = [&](const Expr *e) -> const VarDecl * {
        const auto *bo = dyn_cast_or_null<BinaryOperator>(
            e ? e->IgnoreParenCasts() : nullptr);
        // Issue #224: `x != 0` means x IS zero on the continuing path, so it
        // must never place x in nonZeroVars_ (which would mask 1/x).
        if (!bo || bo->getOpcode() != BO_EQ) return nullptr;
        if (!isNullConstant(bo->getRHS()->IgnoreParenCasts())) return nullptr;
        const auto *ref = dyn_cast<DeclRefExpr>(
            bo->getLHS()->IgnoreParenCasts());
        const auto *vd = ref ? dyn_cast<VarDecl>(ref->getDecl()) : nullptr;
        return (vd && vd->getType()->isIntegerType()) ? vd : nullptr;
      };
      const Expr *cond = s->getCond();
      if (const auto *lor = dyn_cast_or_null<BinaryOperator>(
              cond ? cond->IgnoreParenCasts() : nullptr))
        if (lor->getOpcode() == BO_LOr) {
          if (const VarDecl *v = fromZeroEquality(lor->getLHS())) {
            nonZeroVars_.insert(v);
            invalidatedNonZero_.erase(v);
            ifGuardVar = ifGuardVar ? ifGuardVar : v;
          }
          if (const VarDecl *v = fromZeroEquality(lor->getRHS())) {
            nonZeroVars_.insert(v);
            invalidatedNonZero_.erase(v);
            ifGuardVar = ifGuardVar ? ifGuardVar : v;
          }
        }
      if (const VarDecl *v = fromZeroEquality(cond)) {
        nonZeroVars_.insert(v);
        invalidatedNonZero_.erase(v);
        ifGuardVar = ifGuardVar ? ifGuardVar : v;
      }
    }
    if (!ifGuardVar && report_ && s->getCond())
      if (const auto *ref = dyn_cast<DeclRefExpr>(
              s->getCond()->IgnoreParenCasts()))
        if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl()))
          if (vd->getType()->isIntegerType()) {
            ifGuardVar = vd;
            nonZeroVars_.insert(vd);
            invalidatedNonZero_.erase(vd);
          }
    // Lock state on the fall-through path is the state before the branch:
    // an unlock or return inside one arm must not be applied to code after
    // the if. A return inside an arm is still diagnosed while it holds a lock.
    std::map<std::string, unsigned> savedLocks = heldLocks_;
    // Issue #223: walk each arm with release updates enabled (so a free in
    // one arm is visible to a deref/second free in that same arm), then join
    // the arm states at the merge point instead of dropping the update.
    bool r = true;
    if (s->getInit())
      r = TraverseStmt(s->getInit());
    if (s->getCond())
      r &= TraverseStmt(s->getCond());
    ReleaseSnapshot beforeRelease = snapshotReleaseState();
    if (s->getThen())
      r &= TraverseStmt(s->getThen());
    ReleaseSnapshot thenRelease = snapshotReleaseState();
    std::set<std::string> thenNullVars = nullVars_;
    restoreReleaseState(beforeRelease);
    if (s->getElse())
      r &= TraverseStmt(s->getElse());
    ReleaseSnapshot elseRelease = snapshotReleaseState();
    std::set<std::string> elseNullVars = nullVars_;
    std::vector<ReleaseSnapshot> releaseArms;
    if (s->getThen())
      releaseArms.push_back(thenRelease);
    if (s->getElse())
      releaseArms.push_back(elseRelease);
    else
      releaseArms.push_back(beforeRelease); // implicit "condition false" path
    joinReleaseStates(beforeRelease, releaseArms);
    heldLocks_ = savedLocks;
    if (ifGuardVar && !containsReturn(s->getThen()))
      nonZeroVars_.erase(ifGuardVar);
    guardedSnprintf_ = savedGuarded;
    // Issue #224: restore every null fact saved before the branch. Only the
    // variable actually proved non-null on the continuing path may lose its
    // null state; the kept arm may itself assign `p = NULL` (RAII lowering),
    // so the erase happens after the restore.
    nullVars_ = savedNulls;
    // A branch-local free followed by p = NULL makes a later free a safe
    // free(NULL) on that path; do not report it as a double free at the join.
    for (const auto &n : thenNullVars)
      if (!savedNulls.count(n))
        branchNullifiedVars_.insert(n);
    for (const auto &n : elseNullVars)
      if (!savedNulls.count(n))
        branchNullifiedVars_.insert(n);
    if (keepErase && !keepEraseVar.empty())
      nullVars_.erase(keepEraseVar);
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
    // Guard proving a paired pointer's index is bounded by its companion
    // length parameter (`for (i = 0; i < length; i++)`).
    const VarDecl *countInduction = nullptr;
    unsigned countSlot = 0;
    bool haveCountBound =
        report_ && countBoundLoop(s, countInduction, countSlot);
    auto oldCountIt = loopCountBound_.find(countInduction);
    bool hadOldCount = oldCountIt != loopCountBound_.end();
    unsigned savedCount = hadOldCount ? oldCountIt->second : 0;
    if (haveCountBound)
      loopCountBound_[countInduction] = countSlot;
    // A `const char *` traversed only up to its NUL terminator carries an
    // implicit extent contract; scope it to this loop's body.
    auto savedNul = nulTraversal_;
    if (report_) {
      const VarDecl *nb = nullptr, *ni = nullptr;
      if (nulTraversalKey(nulCondSubscript(s->getCond()), nb, ni) &&
          isConstCharParam(nb))
        nulTraversal_.insert({nb, ni});
    }
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
        invalidatedNonZero_.erase(induction);
      }
    }
    auto oldIt = loopIndexOverride_.find(induction);
    bool hadOld = oldIt != loopIndexOverride_.end();
    long long saved = hadOld ? oldIt->second : 0;
    if (haveBound)
      loopIndexOverride_[induction] = worstIndex;
    // Issue #223: release updates in the loop body are visible to a later
    // deref/second free in that body; at the loop exit the release is only
    // possible (the body may run zero times).
    ReleaseSnapshot beforeRelease = snapshotReleaseState();
    bool r = RecursiveASTVisitor::TraverseForStmt(s);
    joinReleaseStates(beforeRelease,
                      {beforeRelease, snapshotReleaseState()});
    if (report_) {
      if (haveCountBound) {
        if (hadOldCount)
          loopCountBound_[countInduction] = savedCount;
        else
          loopCountBound_.erase(countInduction);
      }
      nulTraversal_ = savedNul;
    }
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
            invalidatedNonZero_.erase(vd);
          }
    auto savedNul = nulTraversal_;
    if (report_) {
      const VarDecl *nb = nullptr, *ni = nullptr;
      if (nulTraversalKey(nulCondSubscript(s->getCond()), nb, ni) &&
          isConstCharParam(nb))
        nulTraversal_.insert({nb, ni});
    }
    // A counted `while` loop has no dedicated `inc` slot like `for`, so the
    // induction override must be installed here. Without it `a[i++]` inside
    // `while (i < N)` is opaque to evalIndexValue() and an OOB access escapes.
    const VarDecl *induction = nullptr;
    long long worstIndex = 0;
    bool haveBound = report_ && parseWhileLoopBound(s, induction, worstIndex);
    auto oldIt = loopIndexOverride_.find(induction);
    bool hadOld = oldIt != loopIndexOverride_.end();
    long long saved = hadOld ? oldIt->second : 0;
    if (haveBound)
      loopIndexOverride_[induction] = worstIndex;
    // Issue #223: see TraverseForStmt — body-local release updates stay
    // visible in the body, and become only possible releases after the loop.
    ReleaseSnapshot beforeRelease = snapshotReleaseState();
    bool r = RecursiveASTVisitor::TraverseWhileStmt(s);
    joinReleaseStates(beforeRelease,
                      {beforeRelease, snapshotReleaseState()});
    if (haveBound) {
      if (hadOld)
        loopIndexOverride_[induction] = saved;
      else
        loopIndexOverride_.erase(induction);
    }
    if (report_)
      nulTraversal_ = savedNul;
    if (guardVar)
      nonZeroVars_.erase(guardVar);
    --conditionalDepth_;
    return r;
  }
  bool TraverseDoStmt(DoStmt *s) {
    ++conditionalDepth_;
    if (!report_)
      collectMayRelease(s->getBody());
    ReleaseSnapshot beforeRelease = snapshotReleaseState();
    bool r = RecursiveASTVisitor::TraverseDoStmt(s);
    joinReleaseStates(beforeRelease,
                      {beforeRelease, snapshotReleaseState()});
    --conditionalDepth_;
    return r;
  }
  bool TraverseSwitchStmt(SwitchStmt *s) {
    ++conditionalDepth_;
    ReleaseSnapshot beforeRelease = snapshotReleaseState();
    bool r = RecursiveASTVisitor::TraverseSwitchStmt(s);
    // A case may release an owner; without proof that every case does, the
    // release is only possible after the switch.
    joinReleaseStates(beforeRelease,
                      {beforeRelease, snapshotReleaseState()});
    --conditionalDepth_;
    return r;
  }

  /// Recognise `for (T i = 0; i < countParam; i++)` where the bound is a
  /// parameter (not a constant, so parseLoopBound cannot see it).  This is
  /// the visible guard required to accept a paired pointer parameter.
  bool countBoundLoop(const ForStmt *s, const VarDecl *&induction,
                      unsigned &countSlot) const {
    if (!s || !s->getInit() || !s->getCond() || !s->getInc())
      return false;
    const VarDecl *ivd = nullptr;
    if (const auto *ds = dyn_cast<DeclStmt>(s->getInit()))
      for (const Decl *d : ds->decls())
        if (const auto *v = dyn_cast<VarDecl>(d)) {
          ivd = v;
          break;
        }
    if (!ivd || !ivd->getType()->isIntegerType() || !ivd->getInit())
      return false;
    Expr::EvalResult ir;
    if (!ivd->getInit()->EvaluateAsInt(ir, ctx_) || !ir.Val.isInt() ||
        ir.Val.getInt() != 0)
      return false;
    const auto *bo =
        dyn_cast<BinaryOperator>(s->getCond()->IgnoreParenImpCasts());
    if (!bo || bo->getOpcode() != BO_LT)
      return false;
    const auto *lref =
        dyn_cast<DeclRefExpr>(bo->getLHS()->IgnoreParenImpCasts());
    if (!lref || lref->getDecl() != ivd)
      return false;
    const auto *pref =
        dyn_cast<DeclRefExpr>(bo->getRHS()->IgnoreParenImpCasts());
    const auto *pvd = pref ? dyn_cast<ParmVarDecl>(pref->getDecl()) : nullptr;
    if (!pvd || !pvd->getType()->isIntegerType())
      return false;
    // The AST is built from lowered C, where `i++` has become a
    // CPLUS_INC statement-expression.  Do not require a specific increment
    // spelling: the condition `i < length` by itself bounds every value of
    // the induction variable used inside the loop body.
    const Expr *ie = s->getInc()->IgnoreParenImpCasts();
    if (const auto *uo = dyn_cast<UnaryOperator>(ie)) {
      if (uo->getOpcode() != UO_PostInc && uo->getOpcode() != UO_PreInc)
        return false;
    } else if (const auto *bio = dyn_cast<BinaryOperator>(ie)) {
      Expr::EvalResult sr;
      if (bio->getOpcode() != BO_AddAssign || !bio->getRHS() ||
          !bio->getRHS()->EvaluateAsInt(sr, ctx_) || !sr.Val.isInt() ||
          sr.Val.getInt() != 1)
        return false;
    }
    // Any other increment form (including a lowered statement-expression)
    // is accepted once the condition bounds the index by the count param.
    induction = ivd;
    countSlot = pvd->getFunctionScopeIndex();
    return true;
  }

  /// Derive the safely count-paired slots from the facts gathered by the
  /// collection pass. Called once on the reporting visitor.
  void deriveCountPairedParams() {
    countPairedParams_.clear();
    for (const auto &fn : callSiteFacts_) {
      std::map<unsigned, std::vector<const CallSiteExtentFact *>> bySlot;
      for (const auto &f : fn.second)
        bySlot[f.ptrSlot].push_back(&f);
      for (const auto &kv : bySlot) {
        if (kv.second.empty())
          continue;
        unsigned countSlot = kv.second.front()->countSlot;
        bool ok = countSlot != CallSiteExtentFact::kNoCountSlot;
        for (const auto *f : kv.second) {
          if (f->countSlot != countSlot || f->count < 0 || f->extentBytes <= 0 ||
              f->exceeds) {
            ok = false;
            break;
          }
        }
        if (ok)
          countPairedParams_[{fn.first, kv.first}] = countSlot;
      }
    }
  }

  // ---- Issue #180 — clang::CFG forward/backward dataflow -----------------
  //
  // The recursive AST walk above keeps one "current" lock/release state and
  // restores it at branch joins (see TraverseIfStmt).  That is sound for
  // straight-line code but loses facts for conditional acquisitions and can
  // mistake two frees on mutually exclusive arms for a double free.  This
  // additive pass runs a must/may lattice over Clang's CFG and a backward
  // liveness pass over the same graph.  It only *refines* the existing
  // diagnostics: it never removes a diagnostic the linear walk would emit
  // except a provably-spurious may-release double free, and new findings use
  // their own [CPLUS-CFG] category so existing output stays byte-compatible.
  struct CfgFlowState {
    // Mutex -> held count: must = minimum over all incoming paths,
    // may = maximum over all incoming paths.
    std::map<std::string, unsigned> mustLocks, mayLocks;
    // Heap ownership: must = live on every path, may = live on some path.
    std::set<std::string> mustOwned, mayOwned;
    // Release state: must = freed on every path, may = freed on some path.
    std::set<std::string> mustFreed, mayFreed;
  };

  static bool cfgSame(const CfgFlowState &a, const CfgFlowState &b) {
    return a.mustLocks == b.mustLocks && a.mayLocks == b.mayLocks &&
           a.mustOwned == b.mustOwned && a.mayOwned == b.mayOwned &&
           a.mustFreed == b.mustFreed && a.mayFreed == b.mayFreed;
  }

  static CfgFlowState cfgJoin(const CfgFlowState &a, const CfgFlowState &b) {
    CfgFlowState r;
    for (const auto &kv : a.mustLocks) {
      auto it = b.mustLocks.find(kv.first);
      r.mustLocks[kv.first] =
          std::min(kv.second, it == b.mustLocks.end() ? 0u : it->second);
    }
    for (const auto &kv : b.mustLocks)
      if (!a.mustLocks.count(kv.first))
        r.mustLocks[kv.first] = 0;
    for (const auto &kv : a.mayLocks)
      r.mayLocks[kv.first] = kv.second;
    for (const auto &kv : b.mayLocks)
      r.mayLocks[kv.first] = std::max(r.mayLocks[kv.first], kv.second);
    std::set_intersection(a.mustOwned.begin(), a.mustOwned.end(),
                          b.mustOwned.begin(), b.mustOwned.end(),
                          std::inserter(r.mustOwned, r.mustOwned.begin()));
    std::set_union(a.mayOwned.begin(), a.mayOwned.end(), b.mayOwned.begin(),
                   b.mayOwned.end(),
                   std::inserter(r.mayOwned, r.mayOwned.begin()));
    std::set_intersection(a.mustFreed.begin(), a.mustFreed.end(),
                          b.mustFreed.begin(), b.mustFreed.end(),
                          std::inserter(r.mustFreed, r.mustFreed.begin()));
    std::set_union(a.mayFreed.begin(), a.mayFreed.end(), b.mayFreed.begin(),
                   b.mayFreed.end(),
                   std::inserter(r.mayFreed, r.mayFreed.begin()));
    return r;
  }

  void runCfgSafetyDataflow(const FunctionDecl *fd) {
    if (!report_ || !fd || !fd->doesThisDeclarationHaveABody() || !fd->getBody())
      return;
    SourceManager &sm = ctx_.getSourceManager();
    SourceLocation floc = sm.getExpansionLoc(fd->getBeginLoc());
    if (floc.isValid() && !sm.isWrittenInMainFile(floc))
      return; // header/inline bodies are not C+ source we own

    CFG::BuildOptions opts;
    std::unique_ptr<CFG> cfg = CFG::buildCFG(fd, fd->getBody(), &ctx_, opts);
    if (!cfg)
      return;
    const size_t n = cfg->size();
    if (n == 0)
      return;
    std::vector<const CFGBlock *> blocks(n, nullptr);
    for (const CFGBlock *b : *cfg)
      if (b && b->getBlockID() < n)
        blocks[b->getBlockID()] = b;

    // Statement -> CFG block, so a scope-end statement can be located.
    // Map whole subtrees: `return p` is a block terminator whose value `p`
    // may also be a CFGStmt element, and both must resolve to the same block.
    std::map<const Stmt *, size_t> stmtBlock;
    for (size_t i = 0; i < n; ++i) {
      const CFGBlock *b = blocks[i];
      if (!b)
        continue;
      auto mapTree = [&](const Stmt *root) {
        std::function<void(const Stmt *)> rec = [&](const Stmt *st) {
          if (!st)
            return;
          stmtBlock.emplace(st, i);
          for (const Stmt *c : st->children())
            rec(c);
        };
        rec(root);
      };
      for (const CFGElement &e : *b)
        if (auto se = e.getAs<CFGStmt>())
          mapTree(se->getStmt());
      if (const Stmt *t = b->getTerminatorStmt())
        mapTree(t);
    }

    // Parent-chain helpers.  They use the typed AST, never token spelling.
    auto enclosingCompound = [&](const VarDecl *vd) -> const CompoundStmt * {
      if (!vd)
        return nullptr;
      DynTypedNode cur = DynTypedNode::create(*vd);
      for (unsigned d = 0; d < 16; ++d) {
        DynTypedNodeList ps = ctx_.getParents(cur);
        if (ps.empty())
          return nullptr;
        const DynTypedNode &p = *ps.begin();
        if (const CompoundStmt *cs = p.get<CompoundStmt>())
          return cs;
        if (const Stmt *st = p.get<Stmt>())
          cur = DynTypedNode::create(*st);
        else if (const Decl *dn = p.get<Decl>())
          cur = DynTypedNode::create(*dn);
        else
          return nullptr;
      }
      return nullptr;
    };
    auto insideConditional = [&](const Stmt *s) {
      if (!s)
        return false;
      DynTypedNode cur = DynTypedNode::create(*s);
      for (unsigned d = 0; d < 32; ++d) {
        DynTypedNodeList ps = ctx_.getParents(cur);
        if (ps.empty())
          return false;
        const DynTypedNode &p = *ps.begin();
        if (p.get<IfStmt>() || p.get<SwitchStmt>() ||
            p.get<ConditionalOperator>())
          return true;
        if (const Stmt *st = p.get<Stmt>())
          cur = DynTypedNode::create(*st);
        else if (const Decl *dn = p.get<Decl>())
          cur = DynTypedNode::create(*dn);
        else
          return false;
      }
      return false;
    };
    auto allocatedVarOf = [&](const CallExpr *call) -> std::string {
      if (!call)
        return std::string();
      DynTypedNode cur = DynTypedNode::create(*call);
      for (unsigned d = 0; d < 8; ++d) {
        DynTypedNodeList ps = ctx_.getParents(cur);
        if (ps.empty())
          break;
        const DynTypedNode &p = *ps.begin();
        if (const auto *bo = p.get<BinaryOperator>())
          if (bo->isAssignmentOp() && bo->getRHS() &&
              bo->getRHS()->IgnoreParenImpCasts() == call)
            return varNameOf(bo->getLHS());
        if (const auto *vd = p.get<VarDecl>())
          return vd->getNameAsString();
        if (const Stmt *st = p.get<Stmt>())
          cur = DynTypedNode::create(*st);
        else if (const Decl *dn = p.get<Decl>())
          cur = DynTypedNode::create(*dn);
        else
          break;
      }
      return std::string();
    };
    auto callNameOf = [&](const CallExpr *call) {
      return call && call->getDirectCallee()
                 ? normalizeFortifyName(
                       call->getDirectCallee()->getNameAsString())
                 : std::string();
    };
    auto scopeEndBlock = [&](const CompoundStmt *cs) -> size_t {
      if (!cs)
        return cfg->getExit().getBlockID();
      DynTypedNode cur = DynTypedNode::create(*cs);
      for (unsigned d = 0; d < 16; ++d) {
        DynTypedNodeList ps = ctx_.getParents(cur);
        if (ps.empty())
          break;
        const DynTypedNode &p = *ps.begin();
        if (const CompoundStmt *parent = p.get<CompoundStmt>()) {
          bool found = false;
          for (const Stmt *child : parent->body()) {
            if (!child)
              continue;
            if (found) {
              auto it = stmtBlock.find(child);
              if (it != stmtBlock.end())
                return it->second;
              continue;
            }
            if (child == cs)
              found = true;
          }
          cur = DynTypedNode::create(*parent);
          continue;
        }
        if (const Stmt *st = p.get<Stmt>()) {
          cur = DynTypedNode::create(*st);
          continue;
        }
        break;
      }
      return cfg->getExit().getBlockID();
    };

    // Facts gathered once from the typed AST before the dataflow runs.
    // The visitor's aliasGroups_ is filled by the linear walk that runs
    // later, so build a local alias map here instead.
    std::map<std::string, std::set<std::string>> cfgAliases;
    std::set<std::string> conditionalAllocs, allFreedNames, allUnlockLocks,
        conditionalLocks, returnedNames;
    struct EscapeCandidate {
      std::string ptr;
      const VarDecl *local;
      const CompoundStmt *scope;
      SourceLocation loc;
    };
    std::vector<EscapeCandidate> escapes;
    // An address stored in an outer pointer is only a dangling escape if the
    // pointer is actually used after the owner's block closes. A diagnostic
    // probe that records &local solely to inspect it inside the same block
    // must not be rejected.
    auto usedAfterScope = [&](const std::string &name, const CompoundStmt *cs) {
      bool used = false;
      std::function<void(const Stmt *)> scan = [&](const Stmt *n) {
        if (!n || used)
          return;
        if (const auto *ref = dyn_cast<DeclRefExpr>(n))
          if (varNameOf(ref) == name &&
              ref->getBeginLoc().getRawEncoding() >
                  cs->getEndLoc().getRawEncoding())
            used = true;
        for (const Stmt *c : n->children())
          scan(c);
      };
      scan(fd->getBody());
      return used;
    };
    std::function<void(const Stmt *)> gather = [&](const Stmt *s) {
      if (!s)
        return;
      if (const auto *ds = dyn_cast<DeclStmt>(s))
        for (const Decl *d : ds->decls())
          if (const auto *vd = dyn_cast<VarDecl>(d))
            if (vd->getType()->isPointerType() && vd->getInit()) {
              std::string rhsName = varNameOf(vd->getInit());
              std::string lhsName = vd->getNameAsString();
              if (!rhsName.empty() && lhsName != rhsName) {
                cfgAliases[lhsName].insert(rhsName);
                cfgAliases[rhsName].insert(lhsName);
              }
            }
      if (const auto *ret = dyn_cast<ReturnStmt>(s)) {
        std::string rn = varNameOf(ret->getRetValue());
        if (!rn.empty())
          returnedNames.insert(rn);
      }
      if (const auto *call = dyn_cast<CallExpr>(s)) {
        std::string cn = callNameOf(call);
        if ((cn == "free" || cn == "cplus_release") && call->getNumArgs())
          allFreedNames.insert(castedVarName(call->getArg(0)));
        else if (isMutexUnlockCall(cn) && call->getNumArgs())
          allUnlockLocks.insert(lockNameOf(call->getArg(0)));
        else if (isMutexLockCall(cn) && call->getNumArgs()) {
          std::string m = lockNameOf(call->getArg(0));
          if (!m.empty() && insideConditional(s))
            conditionalLocks.insert(m);
        } else if ((cn == "malloc" || cn == "calloc") &&
                   insideConditional(s)) {
          std::string lhs = allocatedVarOf(call);
          if (!lhs.empty())
            conditionalAllocs.insert(lhs);
        }
      }
      if (const auto *bo = dyn_cast<BinaryOperator>(s))
        if (bo->isAssignmentOp()) {
          std::string lhs = varNameOf(bo->getLHS());
          // `q = p` (through casts) makes q another view of p's allocation.
          std::string rhsName = varNameOf(bo->getRHS());
          const Expr *rhs = bo->getRHS() ? bo->getRHS()->IgnoreParenImpCasts()
                                        : nullptr;
          if (!lhs.empty() && !rhsName.empty() && lhs != rhsName && rhs &&
              rhs->getType()->isPointerType()) {
            cfgAliases[lhs].insert(rhsName);
            cfgAliases[rhsName].insert(lhs);
          }
          const auto *lref = dyn_cast_or_null<DeclRefExpr>(
              bo->getLHS() ? bo->getLHS()->IgnoreParenImpCasts() : nullptr);
          const VarDecl *pvd =
              lref ? dyn_cast<VarDecl>(lref->getDecl()) : nullptr;
          const VarDecl *owner = localStackOwner(bo->getRHS());
          if (!lhs.empty() && pvd && pvd->getType()->isPointerType() &&
              owner && owner->hasLocalStorage()) {
            const CompoundStmt *cs = enclosingCompound(owner);
            // An address taken inside a nested block and stored in a pointer
            // that outlives it is the classic dangling-escape shape.
            if (cs && cs != fd->getBody() && usedAfterScope(lhs, cs))
              escapes.push_back({lhs, owner, cs, bo->getBeginLoc()});
          }
        }
      for (const Stmt *c : s->children())
        gather(c);
    };
    gather(fd->getBody());

    // Local alias closure so `free(q); free(p);` with `q = p` is not
    // mistaken for two exclusive frees.  It is built from this function's
    // AST above; the visitor's aliasGroups_ is only ready after the linear
    // walk, which runs later.
    auto cfgRelatedNames = [&](const std::string &name) {
      std::set<std::string> names{name};
      if (name.empty())
        return names;
      auto it = cfgAliases.find(name);
      if (it != cfgAliases.end())
        names.insert(it->second.begin(), it->second.end());
      return names;
    };
    auto cfgMarkFreed = [&](CfgFlowState &st, const std::string &name) {
      for (const auto &n : cfgRelatedNames(name)) {
        st.mustFreed.insert(n);
        st.mayFreed.insert(n);
        st.mustOwned.erase(n);
        st.mayOwned.erase(n);
      }
    };
    auto cfgIsFreed = [&](const CfgFlowState &st, const std::string &name) {
      for (const auto &n : cfgRelatedNames(name))
        if (st.mustFreed.count(n) || st.mayFreed.count(n))
          return true;
      return false;
    };

    // Forward transfer.  `record` is false in the fixpoint worklist (where a
    // site may look exclusive before every predecessor has been merged) and
    // true in the final pass over the converged states.
    cfgExclusiveFreeSites_.clear();
    std::function<void(const Stmt *, CfgFlowState &, bool,
                       std::set<const Stmt *> &)>
        apply = [&](const Stmt *s, CfgFlowState &st, bool record,
                    std::set<const Stmt *> &processed) {
          if (!s || !processed.insert(s).second)
            return;
          if (const auto *call = dyn_cast<CallExpr>(s)) {
            std::string cn = callNameOf(call);
            if (cn == "malloc" || cn == "calloc") {
              std::string lhs = allocatedVarOf(call);
              if (!lhs.empty())
                for (const auto &n : cfgRelatedNames(lhs)) {
                  st.mustOwned.insert(n);
                  st.mayOwned.insert(n);
                  st.mustFreed.erase(n);
                  st.mayFreed.erase(n);
                }
            } else if (cn == "free" || cn == "cplus_release") {
              if (call->getNumArgs()) {
                std::string name = castedVarName(call->getArg(0));
                if (!name.empty()) {
                  if (record && !cfgIsFreed(st, name))
                    cfgExclusiveFreeSites_[name].insert(
                        call->getBeginLoc().getRawEncoding());
                  cfgMarkFreed(st, name);
                }
              }
            } else if (isMutexLockCall(cn) && call->getNumArgs()) {
              std::string m = lockNameOf(call->getArg(0));
              if (!m.empty()) {
                ++st.mustLocks[m];
                ++st.mayLocks[m];
              }
            } else if (isMutexUnlockCall(cn) && call->getNumArgs()) {
              std::string m = lockNameOf(call->getArg(0));
              if (!m.empty()) {
                if (st.mustLocks[m] > 0)
                  --st.mustLocks[m];
                if (st.mayLocks[m] > 0)
                  --st.mayLocks[m];
              }
            }
          }
          // A CFG block models control flow with terminators and separate
          // blocks.  Recursing into a nested branch here would apply both
          // arms in the predecessor (a conditional lock/alloc would then
          // look definite, and two exclusive frees would look sequential).
          // The ternary condition is the only straight-line part of a
          // ConditionalOperator.
          if (const auto *co = dyn_cast<ConditionalOperator>(s)) {
            apply(co->getCond(), st, record, processed);
            return;
          }
          for (const Stmt *c : s->children()) {
            if (isa<IfStmt>(c) || isa<WhileStmt>(c) || isa<ForStmt>(c) ||
                isa<DoStmt>(c) || isa<SwitchStmt>(c) ||
                isa<ConditionalOperator>(c))
              continue;
            apply(c, st, record, processed);
          }
        };

    // Terminator transfer: an If/loop/Switch terminator owns entire branch
    // subtrees as children, so apply() must not be called on it directly.
    // Only its condition executes straight-line in this block; the arms are
    // modelled by their own CFG blocks.  Return/goto/break/continue carry no
    // branch bodies to merge (a return value is straight-line).
    std::function<void(const Stmt *, CfgFlowState &, bool,
                       std::set<const Stmt *> &)>
        applyTerm = [&](const Stmt *t, CfgFlowState &st, bool record,
                        std::set<const Stmt *> &processed) {
          if (!t)
            return;
          if (const auto *ifs = dyn_cast<IfStmt>(t)) {
            apply(ifs->getCond(), st, record, processed);
            return;
          }
          if (const auto *ws = dyn_cast<WhileStmt>(t)) {
            apply(ws->getCond(), st, record, processed);
            return;
          }
          if (const auto *fs = dyn_cast<ForStmt>(t)) {
            apply(fs->getCond(), st, record, processed);
            return;
          }
          if (const auto *ds = dyn_cast<DoStmt>(t)) {
            apply(ds->getCond(), st, record, processed);
            return;
          }
          if (const auto *ss = dyn_cast<SwitchStmt>(t)) {
            apply(ss->getCond(), st, record, processed);
            return;
          }
          if (isa<GotoStmt>(t) || isa<BreakStmt>(t) || isa<ContinueStmt>(t))
            return;
          apply(t, st, record, processed);
        };

    std::vector<CfgFlowState> inState(n);
    std::vector<bool> reached(n, false);
    std::deque<size_t> work;
    size_t entry = cfg->getEntry().getBlockID();
    if (entry < n) {
      reached[entry] = true;
      work.push_back(entry);
    }
    while (!work.empty()) {
      size_t id = work.front();
      work.pop_front();
      const CFGBlock *b = id < n ? blocks[id] : nullptr;
      if (!b)
        continue;
      CfgFlowState cur = inState[id];
      std::set<const Stmt *> processed;
      for (const CFGElement &e : *b)
        if (auto se = e.getAs<CFGStmt>())
          apply(se->getStmt(), cur, false, processed);
      if (const Stmt *t = b->getTerminatorStmt())
        applyTerm(t, cur, false, processed);
      for (const CFGBlock *succ : b->succs()) {
        if (!succ || succ->getBlockID() >= n)
          continue;
        size_t sid = succ->getBlockID();
        CfgFlowState merged =
            reached[sid] ? cfgJoin(inState[sid], cur) : cur;
        if (!reached[sid] || !cfgSame(merged, inState[sid])) {
          inState[sid] = merged;
          reached[sid] = true;
          work.push_back(sid);
        }
      }
    }

    // Converged forward states: record provably-exclusive free sites and
    // check ownership/locks at the single CFG exit block.
    for (size_t i = 0; i < n; ++i) {
      if (!reached[i] || !blocks[i] || blocks[i] == &cfg->getExit())
        continue;
      CfgFlowState st = inState[i];
      std::set<const Stmt *> processed;
      for (const CFGElement &e : *blocks[i])
        if (auto se = e.getAs<CFGStmt>())
          apply(se->getStmt(), st, true, processed);
      if (const Stmt *t = blocks[i]->getTerminatorStmt())
        applyTerm(t, st, true, processed);
    }

    std::set<std::string> cfgReported_;
    size_t exitId = cfg->getExit().getBlockID();
    if (exitId < n && reached[exitId]) {
      const CfgFlowState &st = inState[exitId];
      for (const auto &kv : st.mayLocks) {
        const std::string &m = kv.first;
        auto mit = st.mustLocks.find(m);
        unsigned must = mit == st.mustLocks.end() ? 0 : mit->second;
        if (!kv.second || must || !conditionalLocks.count(m) ||
            allUnlockLocks.count(m))
          continue;
        if (!cfgReported_.insert("lock|" + m).second)
          continue;
        reportNamed(fd->getEndLoc(), "CPLUS-CFG",
                    "conditional lock leak: mutex '" + m +
                        "' is held on some exit path; unlock it on every path",
                    "unlock the mutex on every path that acquires it", true);
      }
      for (const auto &name : st.mayOwned) {
        if (st.mustOwned.count(name) || !conditionalAllocs.count(name) ||
            allFreedNames.count(name) || returnedNames.count(name))
          continue;
        if (!cfgReported_.insert("leak|" + name).second)
          continue;
        reportNamed(fd->getEndLoc(), "CPLUS-CFG",
                    "conditional memory leak: allocation held by '" + name +
                        "' on some exit path; release it on every path",
                    "free the allocation on the path that owns it", true);
      }
    }

    // Backward liveness: a pointer assigned the address of an inner-block
    // local is an escape only when that value is read after the block ends.
    std::vector<std::set<std::string>> liveIn(n), liveOut(n), uses(n), defs(n);
    for (size_t i = 0; i < n; ++i) {
      const CFGBlock *b = i < n ? blocks[i] : nullptr;
      if (!b)
        continue;
      std::function<void(const Stmt *)> collect = [&](const Stmt *s) {
        if (!s)
          return;
        if (const auto *ref = dyn_cast<DeclRefExpr>(s)) {
          const auto *vd = dyn_cast<VarDecl>(ref->getDecl());
          if (vd && vd->getType()->isPointerType() &&
              !vd->hasGlobalStorage()) {
            bool lhsOfAssign = false;
            DynTypedNode cur = DynTypedNode::create(*ref);
            DynTypedNodeList ps = ctx_.getParents(cur);
            if (!ps.empty())
              if (const auto *bo = ps.begin()->get<BinaryOperator>())
                if (bo->isAssignmentOp() && bo->getLHS() &&
                    bo->getLHS()->IgnoreParenImpCasts() == ref)
                  lhsOfAssign = true;
            if (!lhsOfAssign)
              uses[i].insert(vd->getNameAsString());
          }
        }
        if (const auto *bo = dyn_cast<BinaryOperator>(s))
          if (bo->isAssignmentOp()) {
            std::string lhs = varNameOf(bo->getLHS());
            if (!lhs.empty())
              defs[i].insert(lhs);
          }
        for (const Stmt *c : s->children())
          collect(c);
      };
      for (const CFGElement &e : *b)
        if (auto se = e.getAs<CFGStmt>())
          collect(se->getStmt());
      if (const Stmt *t = b->getTerminatorStmt())
        collect(t);
    }
    bool liveChanged = true;
    for (int iter = 0; liveChanged && iter < 64; ++iter) {
      liveChanged = false;
      for (size_t k = n; k-- > 0;) {
        if (!blocks[k])
          continue;
        std::set<std::string> out;
        for (const CFGBlock *succ : blocks[k]->succs())
          if (succ && succ->getBlockID() < n)
            out.insert(liveIn[succ->getBlockID()].begin(),
                       liveIn[succ->getBlockID()].end());
        std::set<std::string> in = uses[k];
        for (const auto &nm : out)
          if (!defs[k].count(nm))
            in.insert(nm);
        if (in != liveIn[k] || out != liveOut[k]) {
          liveIn[k] = in;
          liveOut[k] = out;
          liveChanged = true;
        }
      }
    }
    for (const EscapeCandidate &cand : escapes) {
      size_t endBlock = scopeEndBlock(cand.scope);
      if (endBlock >= n || !liveIn[endBlock].count(cand.ptr))
        continue;
      const std::string escKey =
          "esc|" + std::to_string(cand.loc.getRawEncoding()) + "|" + cand.ptr;
      if (!cfgReported_.insert(escKey).second)
        continue;
      reportNamed(cand.loc, "CPLUS-CFG",
                  "address of stack memory associated with local variable '" +
                      cand.local->getNameAsString() +
                      "' escapes through pointer '" + cand.ptr + "'",
                  "copy the data or heap-allocate it before leaving the block",
                  true);
    }
  }

  bool TraverseFunctionDecl(FunctionDecl *fd) {
    std::string previousFunction = currentFunction_;
    std::set<std::string> previousNulls = nullVars_;
    std::map<std::string, unsigned> previousLocks = heldLocks_;
    heldLocks_.clear(); // lock state is per function, never inherited
    currentFunction_ = fd->getNameAsString();
    currentFnDecl_ = fd;
    stringPtrs_.clear(); // names are function-scoped, not TU-scoped
    localParamAliases_.clear(); // per-function alias -> parameter slot map
    forgedUnionVars_.clear();    // compile-time union-punning provenance
    unprovenPtrOffset_.clear();
    arrayDerivedPtr_.clear();
    forgedPointerVars_.clear();  // locals copied from a forged union pointer
    structLocalAddrVars_.clear(); // structs initialized with `&local`
    if (report_) {
      nullVars_.clear();
      releasedVars_.clear();
      branchNullifiedVars_.clear();
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
      knownStrLen_.clear();
      reallocResultVars_.clear();

    }
    // Issue #180: run the clang::CFG dataflow passes before the linear AST
    // walk so the may-release double-free guard is seeded for this function.
    runCfgSafetyDataflow(fd);
    // Parameter extents inferred at call sites apply in both passes so
    // nested calls can propagate them further.
    auto pit = callParamExtents_.find(currentFunction_);
    if (pit != callParamExtents_.end())
      for (const auto *pv : fd->parameters()) {
        unsigned slot = pv->getFunctionScopeIndex();
        auto eit = pit->second.find(slot);
        if (eit == pit->second.end())
          continue;
        if (conflictedParams_.count({currentFunction_, slot}) ||
            eit->second.bytes < 0)
          continue; // no single proven extent for this parameter
        HeapAllocState st;
        st.bytes = eit->second.bytes;
        st.origin = eit->second.origin;
        st.decl = pv;
        heapAllocs_[pv->getNameAsString()] = st;
      }
    // Summarize simple pointer-return helpers: `return p;` for a pointer
    // parameter, or `return global_array;` for a fixed-size global array.
    if (!report_ && fd->getBody() && fd->getReturnType()->isPointerType()) {
      unsigned retParam = 0;
      bool hasRetParam = false, ambiguous = false;
      long long fixedExtent = 0;
      bool hasFixed = false;
      long long allocExtent = 0;
      bool hasAlloc = false;
      std::function<void(const Stmt *)> scanRet = [&](const Stmt *n) {
        if (!n) return;
        if (const auto *ret = dyn_cast<ReturnStmt>(n))
          if (const Expr *rv = ret->getRetValue()) {
            const Expr *v = rv->IgnoreParenCasts();
            // `return malloc(N)` / `return calloc(N,S)` is a heap factory:
            // its extent is a property of the returned pointer value.
            if (const auto *call = dyn_cast<CallExpr>(v))
              if (call->getDirectCallee() && call->getNumArgs() >= 1) {
                std::string cn = call->getDirectCallee()->getNameAsString();
                if (cn == "malloc" || cn == "calloc" || isReallocName(cn)) {
                  long long n = -1;
                  const Expr *sz = isReallocName(cn) && call->getNumArgs() >= 2
                                       ? call->getArg(1)
                                       : call->getArg(0);
                  if (evalByteCount(sz->IgnoreParenImpCasts(), n) && n > 0) {
                    if (cn == "calloc" && call->getNumArgs() >= 2) {
                      long long elem = 1;
                      if (evalByteCount(
                              call->getArg(1)->IgnoreParenImpCasts(), elem) &&
                          elem > 0 && n <= LLONG_MAX / elem)
                        n *= elem;
                    }
                    if (cn != "calloc" || n > 0) {
                      if (hasAlloc && allocExtent != n) ambiguous = true;
                      allocExtent = n;
                      hasAlloc = true;
                    }
                  }
                }
              }
            // `return helper(p);` — follow the helper's returned-parameter /
            // returned-extent summary to a fixpoint (S3 returned-alias chain).
            if (const auto *call = dyn_cast<CallExpr>(v)) {
              for (const auto &calleeName : calleeNames(call, call->getDirectCallee())) {
                auto rp = returnedParam_.find(calleeName);
                if (rp != returnedParam_.end() && rp->second < call->getNumArgs()) {
                  int ps = paramIndexOf(call->getArg(rp->second));
                  if (ps >= 0) {
                    unsigned idx = (unsigned)ps;
                    if (hasRetParam && retParam != idx) ambiguous = true;
                    retParam = idx;
                    hasRetParam = true;
                  }
                }
                auto rf = returnedFixedExtent_.find(calleeName);
                if (rf != returnedFixedExtent_.end() && rf->second > 0) {
                  if (hasFixed && fixedExtent != rf->second) ambiguous = true;
                  fixedExtent = rf->second;
                  hasFixed = true;
                }
                auto ra = returnedAllocExtent_.find(calleeName);
                if (ra != returnedAllocExtent_.end() && ra->second > 0) {
                  if (hasAlloc && allocExtent != ra->second) ambiguous = true;
                  allocExtent = ra->second;
                  hasAlloc = true;
                }
              }
            }
            // `return 0;` through a helper that always returns null.
            if (const auto *call = dyn_cast<CallExpr>(v))
              for (const auto &calleeName : calleeNames(call, call->getDirectCallee()))
                if (nullReturningFunctions_.count(calleeName))
                  nullReturningFunctions_.insert(fd->getNameAsString());
            if (const auto *ref = dyn_cast<DeclRefExpr>(v)) {
              const auto *vd = dyn_cast<VarDecl>(ref->getDecl());
              if (vd && !vd->hasLocalStorage() && vd->getType()->isArrayType())
                if (const auto *cat = ctx_.getAsConstantArrayType(vd->getType())) {
                  QualType elem = cat->getElementType();
                  unsigned es = ctx_.getTypeSize(elem) / 8u;
                  if (es) {
                    long long bytes = static_cast<long long>(
                        cat->getSize().getZExtValue() * es);
                    if (hasFixed && fixedExtent != bytes) ambiguous = true;
                    fixedExtent = bytes;
                    hasFixed = true;
                  }
                }
              // `int *p = malloc(N); return p;` — the common heap factory.
              if (vd && vd->hasLocalStorage() && vd->getInit())
                if (const auto *ic = dyn_cast<CallExpr>(
                        vd->getInit()->IgnoreParenCasts())) {
                  const FunctionDecl *idc = ic->getDirectCallee();
                  if (idc && ic->getNumArgs() >= 1) {
                    std::string cn = idc->getNameAsString();
                    if (cn == "malloc" || cn == "calloc") {
                      long long nb = -1;
                      if (evalByteCount(ic->getArg(0)->IgnoreParenImpCasts(),
                                        nb) &&
                          nb > 0) {
                        if (cn == "calloc" && ic->getNumArgs() >= 2) {
                          long long es = 1;
                          if (evalByteCount(
                                  ic->getArg(1)->IgnoreParenImpCasts(), es) &&
                              es > 0 && nb <= LLONG_MAX / es)
                            nb *= es;
                        }
                        if (hasAlloc && allocExtent != nb) ambiguous = true;
                        allocExtent = nb;
                        hasAlloc = true;
                      }
                    }
                  }
                }
              // `vd` is null when the returned DeclRefExpr names a function
              // (e.g. `return free;` in a function returning a fnptr).
              const auto *pvd = vd ? dyn_cast<ParmVarDecl>(vd) : nullptr;
              if (pvd && pvd->getType()->isPointerType()) {
                unsigned idx = pvd->getFunctionScopeIndex();
                if (hasRetParam && retParam != idx) ambiguous = true;
                retParam = idx;
                hasRetParam = true;
              }
            }
          }
        // `*out = p;` transfers p's extent/release state to the caller's
        // out-parameter (S9/S17 out-param alias).
        if (const auto *bo = dyn_cast<BinaryOperator>(n))
          if (bo->isAssignmentOp()) {
            const Expr *lhs = bo->getLHS()->IgnoreParenImpCasts();
            if (const auto *uo = dyn_cast<UnaryOperator>(lhs))
              if (uo->getOpcode() == UO_Deref) {
                int outSlot = paramIndexOf(uo->getSubExpr());
                int srcSlot = paramIndexOf(bo->getRHS());
                if (outSlot >= 0 && srcSlot >= 0)
                  returnedOutParam_[fd->getNameAsString()][(unsigned)outSlot] =
                      (unsigned)srcSlot;
                if (outSlot >= 0) {
                  const Expr *rhs = bo->getRHS()->IgnoreParenImpCasts();
                  if (const auto *ac = dyn_cast<CallExpr>(rhs))
                    if (ac->getDirectCallee()) {
                      std::string cn = ac->getDirectCallee()->getNameAsString();
                      if ((cn == "malloc" || cn == "calloc" ||
                           isReallocName(cn)) &&
                          ac->getNumArgs() >= 1) {
                        long long nb = -1;
                        const Expr *sz = isReallocName(cn) && ac->getNumArgs() >= 2
                                             ? ac->getArg(1)
                                             : ac->getArg(0);
                        if (evalByteCount(sz->IgnoreParenImpCasts(), nb) &&
                            nb > 0)
                          returnedOutAlloc_[fd->getNameAsString()]
                                           [(unsigned)outSlot] = nb;
                      }
                    }
                }
              }
          }
        for (const Stmt *c : n->children()) scanRet(c);
      };
      scanRet(fd->getBody());
      if (!ambiguous) {
        if (hasRetParam) returnedParam_[fd->getNameAsString()] = retParam;
        if (hasFixed) returnedFixedExtent_[fd->getNameAsString()] = fixedExtent;
        if (hasAlloc)
          returnedAllocExtent_[fd->getNameAsString()] = allocExtent;
      }
    }
    // Infer a tiny arithmetic effect for helpers like `int add(int a,int b)
    // { return a + b; }` so callers with constant arguments can be checked
    // without requiring new annotations or interprocedural machinery.
    if (!report_ && fd->getBody() && fd->getNumParams() >= 2) {
      auto paramIndex = [&](const Expr *e) -> int {
        const auto *ref = dyn_cast_or_null<DeclRefExpr>(
            e ? e->IgnoreParenCasts() : nullptr);
        const auto *pvd = ref ? dyn_cast<ParmVarDecl>(ref->getDecl()) : nullptr;
        if (!pvd) return -1;
        for (unsigned i = 0; i < fd->getNumParams(); ++i)
          if (fd->parameters()[i] == pvd) return static_cast<int>(i);
        return -1;
      };
      std::function<void(const Stmt *)> scan = [&](const Stmt *n) {
        if (!n) return;
        if (const auto *ret = dyn_cast<ReturnStmt>(n))
          if (const Expr *rv = ret->getRetValue()) {
            const auto *bo = dyn_cast<BinaryOperator>(rv->IgnoreParenCasts());
            if (bo && (bo->getOpcode() == BO_Add ||
                       bo->getOpcode() == BO_Sub ||
                       bo->getOpcode() == BO_Mul) &&
                bo->getType()->isSignedIntegerType()) {
              int li = paramIndex(bo->getLHS());
              int ri = paramIndex(bo->getRHS());
              if (li >= 0 && ri >= 0 && li != ri) {
                ArithEffect eff;
                eff.op = bo->getOpcode();
                eff.lhs = li;
                eff.rhs = ri;
                eff.width = ctx_.getIntWidth(bo->getType());
                arithEffects_[fd->getNameAsString()] = eff;
              }
            }
            // Compose arithmetic helpers to a fixpoint: `add2(a,b)` that
            // returns `add(a,b)` inherits add's effect (S10).
            if (const auto *call = dyn_cast<CallExpr>(rv->IgnoreParenCasts()))
              for (const auto &cn :
                   calleeNames(call, call->getDirectCallee())) {
                auto it = arithEffects_.find(cn);
                if (it == arithEffects_.end())
                  continue;
                const ArithEffect &he = it->second;
                if (he.lhs >= (int)call->getNumArgs() ||
                    he.rhs >= (int)call->getNumArgs())
                  continue;
                int li = paramIndexOf(call->getArg(he.lhs));
                int ri = paramIndexOf(call->getArg(he.rhs));
                if (li >= 0 && ri >= 0 && li != ri) {
                  ArithEffect eff = he;
                  eff.lhs = li;
                  eff.rhs = ri;
                  arithEffects_[fd->getNameAsString()] = eff;
                }
              }
          }
        for (const Stmt *c : n->children()) scan(c);
      };
      scan(fd->getBody());
    }
    // Constant-return helper summary for allocator size arguments (S9).
    if (!report_ && fd->getBody() && fd->getReturnType()->isIntegerType()) {
      std::set<std::string> retVals;
      bool sawReturn = false;
      std::function<void(const Stmt *)> scanRetConst = [&](const Stmt *n) {
        if (!n)
          return;
        if (const auto *ret = dyn_cast<ReturnStmt>(n))
          if (const Expr *rv = ret->getRetValue()) {
            Expr::EvalResult r;
            if (rv->EvaluateAsInt(r, ctx_) && r.Val.isInt()) {
              sawReturn = true;
              retVals.insert(std::to_string(r.Val.getInt().getZExtValue()));
            }
          }
        for (const Stmt *c : n->children())
          scanRetConst(c);
      };
      scanRetConst(fd->getBody());
      if (sawReturn && retVals.size() == 1) {
        long long v = 0;
        if (llvm::StringRef(*retVals.begin()).getAsInteger(10, v))
          v = -1;
        if (v >= 0)
          constReturnValues_[fd->getNameAsString()] = v;
      }
    }
    bool result = RecursiveASTVisitor::TraverseFunctionDecl(fd);
    if (report_ && fd->getBody() && !heldLocks_.empty())
      for (const auto &held : heldLocks_)
        warnAt(fd->getEndLoc(), "mutex '" + held.first +
                                    "' is never unlocked on this path; add "
                                    "an unlock on every exit");
    currentFunction_ = previousFunction;
    nullVars_ = previousNulls;
    heldLocks_ = previousLocks;
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
    // Portable invalid-pointer sentinels (NULL and (void *)-1, e.g. MAP_FAILED)
    // are compared, never dereferenced; accepting the cast is sound.
    Expr::EvalResult sentinel;
    if (sub->EvaluateAsInt(sentinel, ctx_) && sentinel.Val.isInt()) {
      llvm::APSInt v = sentinel.Val.getInt();
      if (v.isZero() || v.isNegative())
        return true;
    }
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
    if (report_ && strict_ && fd && !isHeaderLocation(fd->getBeginLoc())) {
      // Only symbols the instrumentation/runtime actually generates are
      // reserved. In check mode the lowered text contains no generated helper
      // prelude, so any such name here is user-supplied and can interpose a
      // guard (e.g. a user __cplus_checked_add_i64 silently wrapping an add).
      std::string fn = fd->getNameAsString();
      if (isReservedHelperName(fn))
        report(fd->getBeginLoc(),
               "user declaration of reserved C+ runtime helper '" + fn +
                   "' is not allowed in STRICT mode");
    }
    if (report_ || !fd || !fd->getBody())
      return true;
    // Interprocedural nullability: record pointer parameters that the callee
    // dereferences.  A provably null argument at a call site is then unsafe.
    struct DerefFinder : RecursiveASTVisitor<DerefFinder> {
      const SafetyVisitor &owner;
      std::set<unsigned> &slots;
      DerefFinder(const SafetyVisitor &o, std::set<unsigned> &s)
          : owner(o), slots(s) {}
      unsigned slotOf(const Expr *e) const {
        int s = owner.paramIndexOf(e);
        return s < 0 ? UINT_MAX : (unsigned)s;
      }
      void note(const Expr *e) {
        unsigned s = slotOf(e);
        if (s != UINT_MAX)
          slots.insert(s);
      }
      bool VisitUnaryOperator(UnaryOperator *uo) {
        if (uo->getOpcode() == UO_Deref)
          note(uo->getSubExpr());
        return true;
      }
      bool VisitArraySubscriptExpr(ArraySubscriptExpr *a) {
        note(a->getBase());
        return true;
      }
      bool VisitMemberExpr(MemberExpr *m) {
        if (m->isArrow())
          note(m->getBase());
        return true;
      }
      bool VisitCallExpr(CallExpr *c) {
        // libc APIs dereference their pointer arguments; a local alias of a
        // parameter used in `memcpy(p, ...)` must be recorded (s05).
        static const std::set<std::string> kDeref = {
            "memcpy", "memmove", "memset", "strcpy", "strncpy", "strcat",
            "strncat", "strlen", "strcmp", "strncmp", "memcmp"};
        if (const FunctionDecl *callee = c->getDirectCallee())
          if (kDeref.count(callee->getNameAsString()))
            for (const Expr *a : c->arguments())
              if (a->getType()->isPointerType())
                note(a);
        return true;
      }
    };
    std::set<unsigned> derefSlots;
    DerefFinder df(*this, derefSlots);
    df.TraverseStmt(fd->getBody());
    // Rust-style exclusivity needs WRITE summaries, not just read/deref:
    // `memcpy(p, p, n)` is legal even though both arguments are dereferenced.
    struct WriteDerefFinder : RecursiveASTVisitor<WriteDerefFinder> {
      const SafetyVisitor &owner;
      std::set<unsigned> &slots;
      WriteDerefFinder(const SafetyVisitor &o, std::set<unsigned> &s)
          : owner(o), slots(s) {}
      unsigned slotOf(const Expr *e) const {
        int s = owner.paramIndexOf(e);
        return s < 0 ? UINT_MAX : (unsigned)s;
      }
      void noteWrite(const Expr *e) {
        unsigned s = slotOf(e);
        if (s != UINT_MAX)
          slots.insert(s);
      }
      bool VisitBinaryOperator(BinaryOperator *bo) {
        if (!bo->isAssignmentOp())
          return true;
        const Expr *lhs = bo->getLHS()->IgnoreParenCasts();
        if (const auto *uo = dyn_cast<UnaryOperator>(lhs)) {
          if (uo->getOpcode() == UO_Deref)
            noteWrite(uo->getSubExpr());
        } else if (const auto *as = dyn_cast<ArraySubscriptExpr>(lhs)) {
          noteWrite(as->getBase());
        } else if (const auto *me = dyn_cast<MemberExpr>(lhs)) {
          if (me->isArrow())
            noteWrite(me->getBase());
        }
        return true;
      }
      bool VisitUnaryOperator(UnaryOperator *uo) {
        if (uo->isIncrementDecrementOp())
          noteWrite(uo->getSubExpr()->IgnoreParenCasts());
        return true;
      }
    };
    std::set<unsigned> writeSlots;
    WriteDerefFinder wf(*this, writeSlots);
    wf.TraverseStmt(fd->getBody());
    for (unsigned slot : writeSlots)
      paramWriteSlots_.insert({fd->getNameAsString(), slot});
    // A callee that rejects null on an early path tolerates a null argument;
    // recording it as requiring non-null would be a false positive.
    std::set<unsigned> nullTolerant;
    struct NullGuardFinder : RecursiveASTVisitor<NullGuardFinder> {
      const SafetyVisitor &owner;
      std::set<unsigned> &tolerant;
      NullGuardFinder(const SafetyVisitor &o, std::set<unsigned> &t)
          : owner(o), tolerant(t) {}
      unsigned slotOf(const Expr *e) const {
        int s = owner.paramIndexOf(e);
        return s < 0 ? UINT_MAX : (unsigned)s;
      }
      bool returnsIn(const Stmt *s) const {
        if (!s)
          return false;
        if (isa<ReturnStmt>(s))
          return true;
        for (const Stmt *c : s->children())
          if (returnsIn(c))
            return true;
        return false;
      }
      bool VisitIfStmt(IfStmt *ifs) {
        const Expr *cond = ifs->getCond() ? ifs->getCond()->IgnoreParenCasts()
                                          : nullptr;
        if (const auto *uo = dyn_cast_or_null<UnaryOperator>(cond))
          if (uo->getOpcode() == UO_LNot) {
            unsigned s = slotOf(uo->getSubExpr());
            if (s != UINT_MAX && returnsIn(ifs->getThen()))
              tolerant.insert(s);
          }
        if (const auto *bo = dyn_cast_or_null<BinaryOperator>(cond)) {
          if (bo->getOpcode() == BO_EQ || bo->getOpcode() == BO_NE) {
            const Expr *other = nullptr;
            const Expr *l = bo->getLHS()->IgnoreParenCasts();
            const Expr *rh = bo->getRHS()->IgnoreParenCasts();
            if (const auto *il = dyn_cast<IntegerLiteral>(l))
              if (il->getValue().isZero()) other = bo->getRHS();
            if (const auto *ir = dyn_cast<IntegerLiteral>(rh))
              if (ir->getValue().isZero()) other = bo->getLHS();
            if (other && returnsIn(ifs->getThen()))
              if (unsigned s = slotOf(other); s != UINT_MAX)
                tolerant.insert(s);
          }
        }
        return true;
      }
    };
    NullGuardFinder ng(*this, nullTolerant);
    ng.TraverseStmt(fd->getBody());
    for (unsigned slot : derefSlots)
      if (!nullTolerant.count(slot))
        derefParams_.insert({fd->getNameAsString(), slot});
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
    if (report_ && s->getRetValue() &&
        s->getRetValue()->getType()->isPointerType()) {
      // Lowered C rewrites a VLA into cplus_alloc_array, so the backend's
      // -Wreturn-stack-address no longer sees `return &buf[0]`. Detect a
      // local array ADDRESS in the AST so check and build agree. Returning
      // an element value (`return row[0]`) stays legal.
      // `self`/`this` is the implicit receiver of a lowered C+ method only
      // in .cp sources. In plain C it is an ordinary pointer parameter whose
      // pointee is owned by the caller (builder/fluent APIs), so returning
      // it does not expose a dying stack frame.
      const bool loweredMethodSource =
          filename_.size() >= 3 &&
          filename_.compare(filename_.size() - 3, 3, ".cp") == 0;
      if (loweredMethodSource) {
        if (const auto *ref = dyn_cast<DeclRefExpr>(
                s->getRetValue()->IgnoreParenImpCasts()))
          if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl()))
            if (vd->getNameAsString() == "self" ||
                vd->getNameAsString() == "this")
              reportNamed(s->getBeginLoc(), "CPLUS-LIFETIME",
                          "address of local instance '" +
                              vd->getNameAsString() +
                              "' cannot escape the method",
                          "return a copied value or use an explicit owner",
                          true);
      }
      const VarDecl *owner = localStackOwner(s->getRetValue());
      if (owner && owner->hasLocalStorage() &&
          (owner->getType()->isArrayType() ||
           ctx_.getAsVariableArrayType(owner->getType())))
        reportNamed(s->getBeginLoc(), "CPLUS-LIFETIME",
                    "address of stack memory associated with local variable '" +
                        owner->getNameAsString() + "' cannot escape",
                    "copy the data or heap-allocate it before returning", true);
    }
    if (report_ && !heldLocks_.empty())
      for (const auto &held : heldLocks_)
        warnAt(s->getBeginLoc(), "mutex '" + held.first +
                                     "' is still held on this return path; "
                                     "unlock before returning");
    // Do not wipe the lock set on an early return. The AST visitor keeps
    // walking sibling paths after the return, and VisitIfStmt already
    // restores the fall-through state; clearing here hid real leaks on later
    // returns (e.g. a switch arm that returns while holding the mutex).
    if (report_ && s->getRetValue()) {
      const Expr *value = s->getRetValue()->IgnoreParenImpCasts();
      if (const auto *ref = dyn_cast<DeclRefExpr>(value))
        if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl()))
          if (vd->hasLocalStorage() &&
              structLocalAddrVars_.count(vd->getNameAsString()))
            reportNamed(s->getBeginLoc(), "CPLUS-LIFETIME",
                        "struct result contains address of local",
                        "suggested correction: copy the pointed-to data into "
                        "the result or heap-allocate it before returning",
                        true);
      // Issue #26: a borrow must not survive the frame that owns it.  The
      // expression is walked with Clang AST nodes so casts, conditionals and
      // pointer-returning helpers cannot erase the provenance recorded when
      // the borrow variable was declared.
      if (currentFnDecl_ &&
          currentFnDecl_->getReturnType()->isPointerType()) {
        const VarDecl *borrowVar = nullptr;
        std::string borrowName;
        if (const VarDecl *owner = borrowOwnerInReturn(
                s->getRetValue(), &borrowVar, &borrowName)) {
          std::string bn = borrowName.empty()
                               ? (borrowVar ? borrowVar->getNameAsString()
                                            : owner->getNameAsString())
                               : borrowName;
          reportNamed(s->getBeginLoc(), "CPLUS-BORROW",
                      "borrow '" + bn + "' of local '" +
                          owner->getNameAsString() +
                          "' cannot escape the function",
                      "return the owner instead, or copy the borrowed data",
                      true);
        }
      }
    }
    if (report_ || !s->getRetValue() || !currentFnDecl_ ||
        !currentFnDecl_->getBody())
      return true;
    const Expr *value = s->getRetValue()->IgnoreParenImpCasts();
    // A function returning a function pointer (`return free;`) gives every
    // indirect call through its result a resolved target.
    if (currentFnDecl_->getReturnType()->isPointerType()) {
      if (const auto *ref = dyn_cast<DeclRefExpr>(value))
        if (const auto *fd = dyn_cast_or_null<FunctionDecl>(ref->getDecl()))
          returnedFnPtr_[currentFunction_].insert(fd->getNameAsString());
    }
    // Getter summary: `void *getg(void){ return g; }` exposes a global handle.
    std::string name = varNameOf(value);
    if (!name.empty() && allGlobals_.count(name))
      globalGetterReturns_[currentFunction_].insert(name);
    if (name.empty())
      return true;
    FreeCallFinder finder(name);
    finder.TraverseStmt(currentFnDecl_->getBody());
    if (finder.found)
      functionsReturningFreed_.insert(currentFunction_);
    return true;
  }

  bool VisitVarDecl(VarDecl *vd) {
    if (vd && !vd->getNameAsString().empty())
      namedVars_[vd->getNameAsString()] = vd->getCanonicalDecl();
    if (!report_ && vd->getInit()) {
      // `int (*fn)(const char*, ...) = printf;` — calling through fn must
      // still enforce the literal-format rule.
      const Expr *init = vd->getInit()->IgnoreParenImpCasts();
      if (const auto *ref = dyn_cast<DeclRefExpr>(init))
        if (const auto *fd = dyn_cast<FunctionDecl>(ref->getDecl())) {
          if (isFormatFunctionName(fd->getNameAsString()))
            formatFnAliases_[vd->getNameAsString()] = fd->getNameAsString();
          // Resolve function-pointer variables/fields so indirect calls get
          // the same free/null/deref summaries as direct calls (s02, s21).
          QualType vt = vd->getType();
          const auto *fpt = vt->getAs<PointerType>();
          bool fpVar = fpt && fpt->getPointeeType()->isFunctionProtoType();
          if (!fpVar)
            if (const auto *at = ctx_.getAsConstantArrayType(vt))
              fpVar = at->getElementType()->isPointerType();
          if (fpVar)
            fnPtrVarTargets_[currentFunction_ + "::" + vd->getNameAsString()] =
                fd->getNameAsString();
        }
      // `int *q = p;` / `int *q = p + 0;` makes q an alias of p's slot, so a
      // free or deref through q is attributed to the parameter (S2/S4).
      int slot = paramIndexOf(init);
      if (slot >= 0 && vd->hasLocalStorage())
        localParamAliases_[vd->getNameAsString()] = slot;
      // Aggregate init of a struct with function-pointer fields:
      // `struct ops o = { consume };` resolves o.fn to consume (s21).
      if (const auto *il = dyn_cast<InitListExpr>(init))
        if (const auto *rd = vd->getType()->getAsRecordDecl()) {
          unsigned idx = 0;
          for (const FieldDecl *field : rd->fields()) {
            if (idx >= il->getNumInits())
              break;
            QualType ft = field->getType();
            const auto *fpt = ft->getAs<PointerType>();
            if (fpt && fpt->getPointeeType()->isFunctionProtoType())
              if (const auto *fr = dyn_cast<DeclRefExpr>(
                      il->getInit(idx)->IgnoreParenCasts()))
                if (const auto *fd = dyn_cast<FunctionDecl>(fr->getDecl()))
                  fnPtrVarTargets_[field->getNameAsString()] =
                      fd->getNameAsString();
            ++idx;
          }
        }
    }
    if (!report_ && vd->getType()->isUnionType())
      unionVars_.insert(vd->getNameAsString());
    // A local struct whose aggregate initializer holds `&local` carries a
    // dangling address: returning it from this function escapes the frame.
    if (vd->hasLocalStorage() && vd->getInit() &&
        vd->getType()->isRecordType() &&
        exprContainsLocalAddress(vd->getInit()))
      structLocalAddrVars_.insert(vd->getNameAsString());
    // Thread-local storage has one instance per thread, so it is never a
    // shared global and must not be reported as a data race.
    if (!report_ && vd->hasGlobalStorage() &&
        vd->getTLSKind() == VarDecl::TLS_None) {
      allGlobals_.insert(vd->getNameAsString());
      if (!vd->getType().isVolatileQualified())
        nonVolatileGlobals_.insert(vd->getNameAsString());
    }
    recordHeapAllocation(vd);
    if (report_ && vd->getInit()) {
      const Expr *init = vd->getInit()->IgnoreParenImpCasts();
      if (vd->getType()->isPointerType())
        if (const auto *ref = dyn_cast<DeclRefExpr>(init))
          if (const auto *base = dyn_cast<VarDecl>(ref->getDecl()))
            if (base->getType()->isArrayType())
              arrayDerivedPtr_.insert(vd->getCanonicalDecl());
      // Use the shared provenance-shape predicate so integer locals seeded by
      // `(uintptr_t)p`, tagged/offset forms (`u | 2`, `u + 8`) and further
      // copies are all tracked. The old ad-hoc predicate only handled bitwise
      // operators, so `uintptr_t u = (uintptr_t)p + 8` lost p's provenance and
      // a later `(int *)u` dereference escaped the UAF checks.
      if (isIntegerProvenanceExpr(init)) {
        provenanceInts_.insert(vd->getNameAsString());
        std::string src = pointerSourceOf(init);
        if (!src.empty())
          provenanceSource_[vd->getNameAsString()] = src;
      }
      // `(int *)u` where u carried p's provenance restores the same object:
      // put q in p's alias group so free(p) taints q (integer-laundering UAF).
      // Resolve the source from the whole expression shape, not just a direct
      // DeclRefExpr, so tagged/masked forms such as `(int*)(u & ~(uintptr_t)1)`
      // and re-based forms `(int*)(u + 8)` inherit the release state too.
      if (vd->getType()->isPointerType()) {
        std::string src = pointerSourceOf(init);
        if (src.empty())
          if (const auto *cast = dyn_cast<CastExpr>(
                  init->IgnoreParenImpCasts()))
            if (cast->getType()->isPointerType())
              src = pointerSourceOf(cast->getSubExpr());
        if (src.empty())
          src = staleIntegerAliasSource(init);
        if (!src.empty()) {
          linkAlias(vd->getNameAsString(), src);
          provenanceSource_[vd->getNameAsString()] = src;
          if (releasedVars_.count(src))
            releasedVars_.insert(vd->getNameAsString());
        }
        // Carry the source allocation's extent across an integer round-trip.
        // Tracking identity here (not the spelling) is what makes the extent
        // survive `(int *)(uintptr_t)p` without granting provenance to a
        // pointer forged from an arbitrary integer.
        if (!src.empty()) {
          const HeapAllocState *st = nullptr;
          auto hit = heapAllocs_.find(src);
          if (hit != heapAllocs_.end() && hit->second.bytes > 0)
            st = &hit->second;
          else {
            auto vit = namedVars_.find(src);
            if (vit != namedVars_.end())
              st = heapStateOfVar(vit->second);
          }
          if (st) {
            HeapAllocState copy = *st;
            copy.decl = vd;
            identityAllocs_[identityKeyForDecl(vd)] = copy;
          }
        }
      }
      // Ternary aliases inherit an extent only when both arms describe the
      // same allocation; divergent arms stay unknown and fail closed when a
      // later subscript needs a bound.
      if (vd->getType()->isPointerType()) {
        const auto *co = dyn_cast<ConditionalOperator>(init);
        if (co) {
          auto armState = [&](const Expr *arm) -> const HeapAllocState * {
            if (const HeapAllocState *st = heapStateOf(arm))
              return st;
            const Expr *peeled = stripStmtExpr(arm);
            while (const auto *ec = dyn_cast<ExplicitCastExpr>(peeled))
              peeled = stripStmtExpr(ec->getSubExpr());
            if (const auto *ref = dyn_cast<DeclRefExpr>(
                    peeled->IgnoreParenImpCasts()))
              if (const auto *pvd = dyn_cast<VarDecl>(ref->getDecl()))
                return heapStateOfVar(pvd);
            return nullptr;
          };
          const HeapAllocState *a = armState(co->getTrueExpr());
          const HeapAllocState *b = armState(co->getFalseExpr());
          if (a && b && !a->provenanceLost && !b->provenanceLost &&
              a->bytes > 0 && a->bytes == b->bytes &&
              a->origin == b->origin) {
            HeapAllocState st = *a;
            st.decl = vd;
            identityAllocs_[identityKeyForDecl(vd)] = st;
          }
        }
      }
      // `int *q = p + K` starts q at an interior offset. Keep the allocation
      // identity but shrink the remaining extent, so a later q[i] is checked
      // against what is actually addressable from q.
      if (vd->getType()->isPointerType()) {
        const Expr *base = init->IgnoreParenImpCasts();
        long long offsetBytes = 0;
        while (const auto *bo = dyn_cast<BinaryOperator>(
                   base->IgnoreParenImpCasts())) {
          if (bo->getOpcode() != BO_Add && bo->getOpcode() != BO_Sub)
            break;
          Expr::EvalResult r;
          long long delta = 0;
          if (!bo->getRHS()->EvaluateAsInt(r, ctx_) ||
              r.Val.getInt().getSExtValue() == 0)
            break;
          delta = r.Val.getInt().getSExtValue();
          offsetBytes += (bo->getOpcode() == BO_Sub ? -delta : delta);
          base = bo->getLHS();
        }
        if (offsetBytes > 0) {
          const HeapAllocState *st = heapStateOf(base);
          if (!st)
            if (const auto *ref =
                    dyn_cast<DeclRefExpr>(base->IgnoreParenImpCasts()))
              if (const auto *pvd = dyn_cast<VarDecl>(ref->getDecl()))
                st = heapStateOfVar(pvd);
          QualType pt = base->IgnoreParenImpCasts()->getType();
          if (const auto *ptr = pt->getAs<PointerType>())
            if (!ptr->getPointeeType()->isVoidType() &&
                !ptr->getPointeeType()->isIncompleteType()) {
              long long es = (long long)(ctx_.getTypeSize(
                                            ptr->getPointeeType()) /
                                        8u);
              if (es > 0 && st && st->bytes > offsetBytes * es) {
                HeapAllocState copy = *st;
                copy.bytes -= offsetBytes * es;
                copy.decl = vd;
                identityAllocs_[identityKeyForDecl(vd)] = copy;
              }
            }
        }
      }
    }
    if (!report_) {
      // Record local integer constants for constant-propagated bounds checks.
      if (vd->hasLocalStorage() && vd->getType()->isIntegerType() &&
          ctx_.getIntWidth(vd->getType()) <= 64 && vd->getInit()) {
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
      // Local floating constants: `double d = 1e30;` is a compile-time source
      // for a strict float-to-integer range check even though Clang will not
      // fold a mutable local DeclRefExpr. Volatile/address-taken locals are
      // excluded so runtime writes are never assumed away.
      if (vd->hasLocalStorage() && vd->getType()->isFloatingType() &&
          !vd->getType().isVolatileQualified() && vd->getInit()) {
        double fv = 0;
        if (floatConstValue(vd->getInit(), fv))
          constFloatLocals_[vd->getNameAsString()] = fv;
      }
      // Fixed-size stack arrays have a known byte extent that can be passed
      // to callees through array-to-pointer decay.
      if (vd->hasLocalStorage() && vd->getType()->isArrayType())
        if (const auto *cat = ctx_.getAsConstantArrayType(vd->getType())) {
          QualType elem = cat->getElementType();
          unsigned es = ctx_.getTypeSize(elem) / 8u;
          if (es)
            stackArrayExtents_[vd->getNameAsString()] =
                static_cast<long long>(cat->getSize().getZExtValue() * es);
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
            fits64(av) && fits64(bv) &&
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
    // Track initial NUL-terminated length for fixed char arrays.
    if (conditionalDepth_ == 0 && vd->getInit()) {
      if (const auto *at = ctx_.getAsConstantArrayType(vd->getType()))
        if (at->getElementType()->isCharType())
          if (const auto *sl =
                  dyn_cast<StringLiteral>(vd->getInit()->IgnoreParenCasts()))
            knownStrLen_[vd] = sl->getLength();
    }
    // `int *end = arr + 4;` yields a pointer that is one past the array's
    // last element; dereferencing it is undefined even though forming it is ok.
    if (vd->getInit() && isOnePastEndExpr(vd->getInit()))
      onePastEnd_.insert(vd);
    // Issue #26: record the stack owner behind a pointer local so a later
    // `return` can be checked on the full AST expression instead of a bare
    // identifier.  A pointer local initialized from a genuine stack object
    // (`&x`, `s.field`, `a[i]`, or a non-pointer local array/record) is a borrow;
    // a pointer local initialized from another pointer is an alias and inherits
    // that variable's borrow owner.  Heap results are never borrows.
    if (vd->getType()->isPointerType() && vd->getInit()) {
      const Expr *binit = vd->getInit()->IgnoreParenCasts();
      const VarDecl *owner = localStackOwner(binit);
      bool addressOf = isa<UnaryOperator>(binit) &&
                       cast<UnaryOperator>(binit)->getOpcode() == UO_AddrOf;
      bool stackObject =
          owner && owner->hasLocalStorage() &&
          (addressOf || !owner->getType()->isPointerType());
      const VarDecl *alias = nullptr;
      if (const auto *ref = dyn_cast<DeclRefExpr>(binit))
        alias = dyn_cast<VarDecl>(ref->getDecl());
      if (!alias)
        if (const auto *call = dyn_cast<CallExpr>(binit))
          for (const auto &cn : calleeNames(call, call->getDirectCallee())) {
            auto rp = returnedParam_.find(cn);
            if (rp != returnedParam_.end() && rp->second < call->getNumArgs()) {
              if (const auto *aref = dyn_cast<DeclRefExpr>(
                      call->getArg(rp->second)->IgnoreParenCasts())) {
                alias = dyn_cast<VarDecl>(aref->getDecl());
                if (alias)
                  break;
              }
            }
          }
      if (stackObject)
        returnBorrowOwner_[vd] = {owner, vd->getNameAsString()};
      else if (alias) {
        auto bit = returnBorrowOwner_.find(alias);
        if (bit != returnBorrowOwner_.end())
          returnBorrowOwner_[vd] = bit->second; // keep original source name
      } else if (isa<ConditionalOperator>(binit) ||
                 (isa<BinaryOperator>(binit) &&
                  cast<BinaryOperator>(binit)->getOpcode() == BO_Comma)) {
        // RAII lowering materializes `return c ? a : b;` and `return (z++, b);`
        // as a temporary. The expression walker resolves both shapes.
        const VarDecl *bvar = nullptr;
        std::string bname;
        if (const VarDecl *o =
                borrowOwnerInReturn(binit, &bvar, &bname, 1))
          if (bvar)
            returnBorrowOwner_[vd] = {o, bname};
      }
    }
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
          if (callee == "malloc" || callee == "calloc" || isReallocName(callee) ||
              callee.rfind("cplus_new", 0) == 0)
            allocLocs_[vd->getNameAsString()] = vd->getBeginLoc();
          if (conditionalDepth_ == 0 && callee == "calloc")
            knownStrLen_[vd] = 0;
        }
      }
      if (isa<StringLiteral>(init->IgnoreParenCasts())) {
        stringPtrs_.insert(vd);
      }
      if (const auto *uo = dyn_cast<UnaryOperator>(init->IgnoreParenCasts()))
        if (uo->getOpcode() == UO_AddrOf)
          if (const auto *oref = dyn_cast<DeclRefExpr>(
                  uo->getSubExpr()->IgnoreParenCasts()))
            if (const auto *ovd = dyn_cast<VarDecl>(oref->getDecl()))
              if (ovd->hasLocalStorage() &&
                  !ovd->getType()->isPointerType())
                stackAliasOwner_[vd->getNameAsString()] =
                    ovd->getNameAsString();
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
    // Symmetric aliases: `int *q = s.p;` gives q the field's release state,
    // and a whole-struct copy `struct S t = s;` aliases every field view.
    if (report_ && vd->getInit()) {
      const Expr *init = vd->getInit()->IgnoreParenCasts();
      const std::string fkey = lvalueKey(init);
      if (!fkey.empty() &&
          (fkey.find('.') != std::string::npos ||
           fkey.find('[') != std::string::npos) &&
          vd->getType()->isPointerType()) {
        linkAlias(vd->getNameAsString(), fkey);
        // Read side of a struct-pointer-field alias: `int *d = b.p;` makes
        // d another view of the field's extent. free(b.p) records the field
        // in releasedFields_, and isReleased(d) must see it here.
        fieldAliases_[fkey].insert(vd->getNameAsString());
        fieldAliases_[vd->getNameAsString()].insert(fkey);
        if (releasedFields_.count(fkey) || releasedVars_.count(fkey))
          releasedVars_.insert(vd->getNameAsString());
      }
      // A pointer copied out of a forged union member stays forged so a
      // later dereference of the local is diagnosed too.
      if (vd->getType()->isPointerType() && forgedUnionPointerMember(init))
        forgedPointerVars_.insert(vd->getNameAsString());
      if (const auto *ref = dyn_cast<DeclRefExpr>(init))
        if (const auto *ivd = dyn_cast_or_null<VarDecl>(ref->getDecl()))
          if (vd->getType()->isRecordType() && ivd->getType()->isRecordType()) {
            const auto *rd = vd->getType()->getAsRecordDecl();
            if (rd)
              for (const FieldDecl *field : rd->fields())
                linkAlias(vd->getNameAsString() + "." +
                              field->getNameAsString(),
                          ivd->getNameAsString() + "." +
                              field->getNameAsString());
          }
      // Getter summary: `void *q = getg();` aliases the global's pointees.
      if (vd->getType()->isPointerType())
        if (const auto *call =
                dyn_cast<CallExpr>(vd->getInit()->IgnoreParenCasts())) {
          for (const auto &t : calleeNames(call, call->getDirectCallee())) {
            auto git = globalGetterReturns_.find(t);
            if (git == globalGetterReturns_.end())
              continue;
            for (const auto &g : git->second) {
              const std::string id = allocIdOf(g);
              if (!id.empty()) {
                varAllocIdentity_[vd->getNameAsString()] = id;
                allocIdentityMembers_[id].insert(vd->getNameAsString());
              }
              auto pit = globalPointsTo_.find(g);
              if (pit != globalPointsTo_.end())
                for (const auto &n : pit->second)
                  linkAlias(vd->getNameAsString(), n);
            }
          }
        }
    }
    // A flat cast from a multidimensional array (`int *p = (int *)a;`)
    // keeps the whole byte extent, so later subscripts can be bounds-checked.
    if (report_ && vd->getType()->isPointerType() && vd->getInit()) {
      const Expr *x = vd->getInit();
      for (unsigned depth = 0; depth < 8; ++depth) {
        const auto *ce = dyn_cast_or_null<ExplicitCastExpr>(x);
        if (!ce || !ce->getSubExpr())
          break;
        x = ce->getSubExpr()->IgnoreParenCasts();
      }
      const std::string srcName = varNameOf(x);
      auto sit = stackArrayExtents_.find(srcName);
      if (sit != stackArrayExtents_.end() && sit->second > 0) {
        HeapAllocState st;
        st.bytes = sit->second;
        st.origin = ExtentOrigin::Stack;
        st.decl = vd;
        heapAllocs_[vd->getNameAsString()] = st;
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
    // `T *q = p;` (including explicit casts) creates a simple alias and
    // inherits the release state.  `T *q = identity(p)` does the same when
    // the callee was summarized as returning one of its pointer parameters.
    std::string initName =
        vd->getInit() ? castedVarName(vd->getInit()) : std::string();
    if (initName.empty() && vd->getInit())
      if (const auto *call =
              dyn_cast<CallExpr>(vd->getInit()->IgnoreParenCasts()))
        if (const FunctionDecl *callee = call->getDirectCallee()) {
          auto rit = returnedParam_.find(callee->getNameAsString());
          if (rit != returnedParam_.end() && rit->second < call->getNumArgs())
            initName = castedVarName(call->getArg(rit->second));
        }
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
      // A copy of a pointer with a proven extent keeps that extent.  This is
      // the same propagate-on-copy discipline used by SoftBoundCETS's
      // propagateMetadata, expressed in C+'s compiler-side extent table.
      auto hit = heapAllocs_.find(initName);
      if (hit != heapAllocs_.end() && hit->second.decl) {
        HeapAllocState copied = hit->second;
        copied.decl = vd;
        heapAllocs_[vd->getNameAsString()] = copied;
      }
      auto sit = stackArrayExtents_.find(initName);
      if (sit != stackArrayExtents_.end())
        stackArrayExtents_[vd->getNameAsString()] = sit->second;
    }
    // A heap factory (`return malloc(N)`) gives the call result a proven
    // extent; carry it into the variable that receives the result.
    if (conditionalDepth_ == 0 && vd->getInit())
      if (const auto *call =
              dyn_cast<CallExpr>(vd->getInit()->IgnoreParenCasts())) {
        auto cit =
            callReturnExtent_.find(call->getBeginLoc().getRawEncoding());
        if (cit != callReturnExtent_.end() && cit->second > 0) {
          HeapAllocState st;
          st.bytes = cit->second;
          st.origin = ExtentOrigin::Heap;
          st.decl = vd;
          heapAllocs_[vd->getNameAsString()] = st;
        }
      }
    return true;
  }

  bool TraverseBinaryOperator(BinaryOperator *op) {
    if (!report_ ||
        (op->getOpcode() != BO_LAnd && op->getOpcode() != BO_LOr))
      return RecursiveASTVisitor::TraverseBinaryOperator(op);
    // Issue #225: suppression depends on both the operator and whether the
    // condition is negated. The RHS is reachable with the tested pointer
    // non-null only for `p && ...` and `!p || ...`. For `!p && ...` and
    // `p || ...` the RHS runs precisely when p is NULL, so the null fact must
    // stay visible there (a dereference is a guaranteed null dereference).
    const BinaryOperator::Opcode opc = op->getOpcode();
    std::string condVar = varNameOf(op->getLHS());
    bool isNegated = false;
    if (const auto *uo = dyn_cast<UnaryOperator>(
            op->getLHS()->IgnoreParenImpCasts()))
      if (uo->getOpcode() == UO_LNot) {
        isNegated = true;
        condVar = varNameOf(uo->getSubExpr());
      }
    bool rhsNonNull = !condVar.empty() &&
                      ((isNegated && opc == BO_LOr) ||
                       (!isNegated && opc == BO_LAnd));
    // The condition itself is always traversed with the original facts.
    bool r = TraverseStmt(op->getLHS());
    std::string saved = nonNullBranch_;
    bool removed = rhsNonNull && nullVars_.erase(condVar) > 0;
    if (removed)
      nonNullBranch_ = condVar;
    r &= TraverseStmt(op->getRHS());
    nonNullBranch_ = saved;
    if (removed)
      nullVars_.insert(condVar);
    return r;
  }

  bool TraverseConditionalOperator(ConditionalOperator *op) {
    if (!report_)
      return RecursiveASTVisitor::TraverseConditionalOperator(op);
    // `p ? *p : 0` tests p for non-null, so suppress only the proven-null
    // fact in the true arm. Issue #225: a null test does NOT revive a freed
    // pointer — free(p) leaves a non-null dangling value — so the release
    // state must remain visible in both arms.
    std::string condVar = varNameOf(op->getCond());
    std::string saved = nonNullBranch_;
    bool removed = !condVar.empty() && nullVars_.erase(condVar) > 0;
    if (removed)
      nonNullBranch_ = condVar;
    bool r = TraverseStmt(op->getCond());
    r &= TraverseStmt(op->getTrueExpr());
    nonNullBranch_.clear();
    r &= TraverseStmt(op->getFalseExpr());
    nonNullBranch_ = saved;
    if (removed)
      nullVars_.insert(condVar);
    return r;
  }

  bool VisitBinaryOperator(BinaryOperator *op) {
    noteAssignedValueSource(op);
    if (op->isAssignmentOp()) {
      // `uintptr_t sp; sp = (uintptr_t)p; ... (T *)sp` derives provenance by
      // assignment, not by initializer. VisitVarDecl cannot see this shape.
      if (isIntegerProvenanceExpr(op->getRHS()))
        if (const auto *lref = dyn_cast<DeclRefExpr>(
                op->getLHS()->IgnoreParenImpCasts()))
          if (const auto *lvd = dyn_cast<VarDecl>(lref->getDecl())) {
            provenanceInts_.insert(lvd->getNameAsString());
            std::string src = pointerSourceOf(op->getRHS());
            if (!src.empty())
              provenanceSource_[lvd->getNameAsString()] = src;
          }
    }
    if (op->isAssignmentOp()) {
      const Expr *re = stripStmtExpr(op->getRHS());
      while (const auto *ec = dyn_cast<ExplicitCastExpr>(re))
        re = stripStmtExpr(ec->getSubExpr());
      if (const auto *rc = dyn_cast<CallExpr>(re)) {
        bool isRealloc = false;
        for (const auto &cn : calleeNames(rc, rc->getDirectCallee()))
          if (isReallocName(cn)) {
            isRealloc = true;
            break;
          }
        if (isRealloc)
          if (const auto *lref = dyn_cast<DeclRefExpr>(
                  op->getLHS()->IgnoreParenImpCasts()))
            if (const auto *lvd = dyn_cast<VarDecl>(lref->getDecl()))
              reallocAssignCalls_[lvd->getCanonicalDecl()] = rc;
      }
    }
    // `r.ptr = &local; return r;` stores a frame address in a struct field.
    // The aggregate-initializer check in VisitVarDecl cannot see this form,
    // so record the same obligation when the store is an assignment.
    if (op->isAssignmentOp()) {
      const Expr *lhs = op->getLHS()->IgnoreParenImpCasts();
      if (const auto *me = dyn_cast<MemberExpr>(lhs)) {
        if (const auto *ref = dyn_cast<DeclRefExpr>(
                me->getBase()->IgnoreParenImpCasts()))
          if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl()))
            if (exprContainsLocalAddress(op->getRHS()))
              structLocalAddrVars_.insert(vd->getNameAsString());
      }
    }
    if (report_ && op->isAssignmentOp()) {
      // `q = realloc(...)` (as opposed to an initializer) also makes q a
      // realloc-result variable, so a later `p = q` can transfer its extent.
      const Expr *rhs = op->getRHS()->IgnoreParenImpCasts();
      while (const auto *ce = dyn_cast<ExplicitCastExpr>(rhs))
        rhs = ce->getSubExpr()->IgnoreParenImpCasts();
      if (const auto *call = dyn_cast<CallExpr>(rhs))
        for (const auto &t : calleeNames(call, call->getDirectCallee()))
          if (isReallocName(t)) {
            reallocResultVars_.insert(varNameOf(op->getLHS()));
            break;
          }
      // Provenance of a pointer stored into a struct/union field is keyed by
      // the canonical (base object, field) pair. A later `s.p[i]` reads that
      // same key, so the extent cannot be laundered through member access.
      std::string lhsKey = identityKey(op->getLHS());
      if (!lhsKey.empty()) {
        bool stored = false;
        const auto *lhsMember = dyn_cast<MemberExpr>(
            op->getLHS()->IgnoreParenImpCasts());
        // Writing an integer into one union member and reading a pointer out
        // of another launders provenance. Mark every pointer member of that
        // union as lost regardless of whether the integer expression itself
        // happens to carry a tracked extent.
        if (lhsMember && lhsMember->getBase() &&
            lhsMember->getBase()->getType()->isUnionType() &&
            op->getLHS()->getType()->isIntegerType()) {
          std::string baseKey = identityKey(lhsMember->getBase());
          if (!baseKey.empty())
            if (const auto *rd = lhsMember->getBase()
                                     ->getType()
                                     ->getAsRecordDecl())
              for (const FieldDecl *field : rd->fields()) {
                if (!field->getType()->isPointerType())
                  continue;
                HeapAllocState lost;
                lost.provenanceLost = true;
                identityAllocs_[baseKey + ".F" +
                                std::to_string(reinterpret_cast<uintptr_t>(
                                    field->getCanonicalDecl()))] = lost;
              }
          stored = true;
        }
        const HeapAllocState *rhsState = heapStateOf(op->getRHS());
        if (!rhsState)
          if (const auto *rref = dyn_cast<DeclRefExpr>(
                  stripStmtExpr(op->getRHS())))
            if (const auto *rvd = dyn_cast<VarDecl>(rref->getDecl()))
              rhsState = heapStateOfVar(rvd);
        if (rhsState) {
          HeapAllocState copy = *rhsState;
          copy.decl = nullptr;
          identityAllocs_[lhsKey] = copy;
          stored = true;
        }
        // A freshly lowered `cplus_new_array(T, N)` reaches the AST as a
        // `(T *)cplus_alloc_array(N, sizeof(T))` call; compute its byte extent
        // directly so a field holding it is not treated as an opaque pointer.
        if (!stored) {
          const Expr *ae = stripStmtExpr(op->getRHS());
          while (const auto *ec = dyn_cast<ExplicitCastExpr>(ae))
            ae = stripStmtExpr(ec->getSubExpr());
          if (const auto *call = dyn_cast<CallExpr>(ae)) {
            long long bytes = -1;
            for (const auto &n : calleeNames(call, call->getDirectCallee())) {
              if (n == "cplus_alloc_array" && call->getNumArgs() >= 2) {
                long long count = -1, size = -1;
                if (evalByteCount(call->getArg(0), count) &&
                    evalByteCount(call->getArg(1), size) && count > 0 &&
                    size > 0 && count <= LLONG_MAX / size)
                  bytes = count * size;
              } else if ((n == "malloc" || n == "cplus_alloc" ||
                          n == "cplus_zalloc") &&
                         call->getNumArgs() >= 1) {
                evalByteCount(call->getArg(0), bytes);
              }
              if (bytes > 0)
                break;
            }
            if (bytes > 0) {
              HeapAllocState st;
              st.bytes = bytes;
              st.origin = ExtentOrigin::Heap;
              identityAllocs_[lhsKey] = st;
              stored = true;
            }
          }
        }
      }
    }
    if (report_ && op->isAssignmentOp()) {
      // `p = array + runtime_offset` has no static proof and no dynamic
      // instrumentation. Remember it so a later `*p` fails closed in STRICT.
      const Expr *rhs = op->getRHS()->IgnoreParenImpCasts();
      if (const auto *bo = dyn_cast<BinaryOperator>(rhs))
        if (bo->getOpcode() == BO_Add || bo->getOpcode() == BO_Sub) {
          const Expr *base = bo->getLHS()->IgnoreParenImpCasts();
          const Expr *off = bo->getRHS()->IgnoreParenImpCasts();
          Expr::EvalResult r;
          bool isConst = off->EvaluateAsInt(r, ctx_) && r.Val.isInt();
          if (!isConst) {
            const auto *ref = dyn_cast<DeclRefExpr>(base);
            const auto *vd = ref ? dyn_cast<VarDecl>(ref->getDecl()) : nullptr;
            bool fromArray = vd && vd->getType()->isArrayType();
            if (vd && arrayDerivedPtr_.count(vd->getCanonicalDecl()))
              fromArray = true;
            if (fromArray)
              if (const auto *lref = dyn_cast<DeclRefExpr>(
                      op->getLHS()->IgnoreParenImpCasts()))
                if (const auto *lvd = dyn_cast<VarDecl>(lref->getDecl()))
                  unprovenPtrOffset_.insert(lvd->getCanonicalDecl());
          }
        }
    }
    if (report_ && op->isAssignmentOp()) {
      // Kill guard-derived non-zero / lower-bound facts on a definite
      // assignment; a later division must not reuse a stale `if (d)` proof.
      const Expr *lhse = op->getLHS()->IgnoreParenImpCasts();
      if (const auto *ref = dyn_cast<DeclRefExpr>(lhse))
        if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl())) {
          nonZeroVars_.erase(vd);
          loopLowerBound_.erase(vd);
          invalidatedNonZero_.insert(vd);
        }
    }
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
        // Local alias (`q = p`) and function-pointer binding (`fp = consume`)
        // facts feed the interprocedural effect fixpoint.
        std::string ln = varNameOf(op->getLHS());
        int slot = paramIndexOf(rhs);
        if (!ln.empty() && slot >= 0)
          localParamAliases_[ln] = slot;
        if (const auto *fr = dyn_cast<DeclRefExpr>(rhs))
          if (const auto *fd = dyn_cast<FunctionDecl>(fr->getDecl()))
            if (!ln.empty())
              fnPtrVarTargets_[currentFunction_ + "::" + ln] =
                  fd->getNameAsString();
        // A function pointer assigned into a struct field (`s.cb = consume`)
        // resolves `s.cb(...)` just like an aggregate initializer.
        const Expr *lhse = op->getLHS()->IgnoreParenImpCasts();
        if (const auto *me = dyn_cast<MemberExpr>(lhse))
          if (const auto *fr = dyn_cast_or_null<DeclRefExpr>(rhs))
            if (const auto *fd = dyn_cast_or_null<FunctionDecl>(fr->getDecl())) {
              fnPtrVarTargets_[me->getMemberDecl()->getNameAsString()] =
                  fd->getNameAsString();
              fnPtrVarTargets_[varNameOf(me->getBase()) + "." +
                               me->getMemberDecl()->getNameAsString()] =
                  fd->getNameAsString();
            }
        // Global pointer points-to: `g = p;` records that releasing g also
        // invalidates p (S6 global-alias UAF).
        if (!ln.empty() && allGlobals_.count(ln)) {
          int ps = paramIndexOf(rhs);
          if (ps >= 0)
            globalParamStores_[currentFunction_][(unsigned)ps].insert(ln);
          else {
            std::string rn = castedVarName(rhs);
            if (!rn.empty())
              registerGlobalPointsTo(ln, rn);
          }
        }
        // Struct-field pointer store `s.p = q;` makes the field an alias of q
        // so freeing q invalidates `*s.p`.  Only real field lvalues qualify:
        // recording a plain `p = tmp` here would taint tmp when p is released
        // and survive realloc's rebind clearing (false UAF).  Plain pointer
        // copies are handled by aliasGroups_ in the reporting pass.
        if ((isa<MemberExpr>(op->getLHS()->IgnoreParenImpCasts()) ||
             isa<ArraySubscriptExpr>(op->getLHS()->IgnoreParenImpCasts())) &&
            !lk.empty()) {
          std::string rn = castedVarName(rhs);
          if (!rn.empty()) {
            fieldAliases_[lk].insert(rn);
            fieldAliases_[rn].insert(lk);
            // A global array element (`g[i] = p`) is a global slot too.
            std::string base = varNameOf(op->getLHS());
            if (!base.empty() && allGlobals_.count(base))
              registerGlobalPointsTo(lk, rn);
          }
        }
      }
      return true;
    }
    // Compile-time-only lifetime / provenance facts for the breaker patterns.
    if (op->isAssignmentOp()) {
      // `*out = &local`: the address of a frame-local escapes through a
      // pointer out-parameter of the current function.
      const Expr *lhs = op->getLHS()->IgnoreParenImpCasts();
      if (const auto *uo = dyn_cast<UnaryOperator>(lhs))
        if (uo->getOpcode() == UO_Deref && paramIndexOf(uo->getSubExpr()) >= 0)
          if (const VarDecl *local = automaticLocalOfAddress(op->getRHS()))
            reportNamed(local->getBeginLoc(), "CPLUS-LIFETIME",
                        "address of local escapes through out-parameter",
                        "suggested correction: do not store the address of a "
                        "frame-local through an out-parameter; heap-allocate "
                        "the object or return it by value",
                        true);
      // Writing a non-pointer-derived integer constant into a union integer
      // member forges the provenance of every pointer view of those bytes.
      const std::string uv = unionNonPointerMemberVar(op->getLHS());
      if (!uv.empty() && isPlainIntegerConstantExpr(op->getRHS()))
        forgedUnionVars_.insert(uv);
    }
    // Compile-time arithmetic safety (STRICT fails closed; DEFAULT warns).
    // This is C+ analysis, not UBSan instrumentation.
    if (op->getType()->isIntegerType()) {
      const auto opc = op->getOpcode();
      // Queue a native guard for every supported non-constant operation.
      // Compile-time cases are rejected below and never reach codegen.
      maybeRecordCheckedArithmetic(op);
      // `check` and `build` must agree: a macro-expanded arithmetic op with
      // runtime operands cannot be instrumented, so STRICT rejects it here
      // too instead of certifying a program the guarded build refuses.
      if (arithmeticMacroFailClosed(op))
        report(op->getBeginLoc(),
               "arithmetic in a macro expansion cannot be instrumented; "
               "STRICT cannot prove it safe");
      auto asLL = [&](const Expr *e, long long &out) -> bool {
        llvm::APSInt v;
        if (!knownConst(e, v) || !fits64(v))
          return false; // >64-bit constants use wideConstOverflow below
        // For signed expressions interpret the stored bit pattern as signed
        // even when Clang folded it into an unsigned APSInt.
        out = op->getType()->isSignedIntegerType()
                  ? v.getSExtValue()
                  : (v.isNegative() ? v.getSExtValue() : v.getZExtValue());
        return true;
      };
      if (op->getType()->isSignedIntegerType() &&
          (opc == BO_Add || opc == BO_Sub || opc == BO_Mul ||
           opc == BO_AddAssign || opc == BO_SubAssign ||
           opc == BO_MulAssign)) {
        long long a = 0, b = 0;
        bool haveA = false;
        // A compound assignment reads the LHS's *pre-update* value. The
        // collection pass records that constant in constLocals_, but also
        // marks the variable assigned; asLL() would reject it. Resolve the
        // initializer constant directly for the compound case so a provably
        // constant `x = INT_MAX; x += 1;` is rejected by STRICT check instead
        // of only trapping in the guarded build.
        if (opc == BO_AddAssign || opc == BO_SubAssign ||
            opc == BO_MulAssign) {
          llvm::APSInt v;
          if (knownConstAtUpdate(op->getLHS(), v) && fits64(v)) {
            a = op->getType()->isSignedIntegerType()
                    ? v.getSExtValue()
                    : (v.isNegative() ? v.getSExtValue() : v.getZExtValue());
            haveA = true;
          }
        } else {
          haveA = asLL(op->getLHS(), a);
        }
        if (haveA && asLL(op->getRHS(), b)) {
          bool overflow = false;
          if (arithmeticOverflow(op, a, b, overflow) && overflow) {
            std::string msg = "signed integer overflow in ";
            msg += (opc == BO_Add || opc == BO_AddAssign
                        ? "addition"
                        : opc == BO_Sub || opc == BO_SubAssign
                              ? "subtraction"
                              : "multiplication");
            if (strict_) report(op->getBeginLoc(), msg);
            else warnAt(op->getBeginLoc(), msg);
          }
        }
      }
      // Widths beyond 64 bits (__int128) cannot use the long long path. Do
      // not silently accept a provably overflowing wide constant in STRICT.
      if (strict_ && op->getType()->isSignedIntegerType() &&
          (opc == BO_Add || opc == BO_Sub || opc == BO_Mul ||
           opc == BO_Div || opc == BO_Rem ||
           opc == BO_AddAssign || opc == BO_SubAssign ||
           opc == BO_MulAssign || opc == BO_DivAssign ||
           opc == BO_RemAssign)) {
        bool wideOverflow = false;
        if (wideConstOverflow(op, wideOverflow) && wideOverflow)
          report(op->getBeginLoc(), "signed integer overflow of a compile-time "
                                    "constant wider than 64 bits");
      }
      // Shift counts must be in [0, promoted-left-width). Volatile-backed
      // constants are tracked like other local constants by knownConst.
      if ((opc == BO_Shl || opc == BO_Shr || opc == BO_ShlAssign ||
           opc == BO_ShrAssign) && !isHeaderLocation(op->getBeginLoc())) {
        long long count = 0;
        if (asLL(op->getRHS(), count)) {
          unsigned width = ctx_.getIntWidth(op->getLHS()->getType());
          std::string msg;
          if (count < 0)
            msg = "negative shift count";
          else if (width && (unsigned long long)count >= width)
            msg = "shift count " + std::to_string(count) +
                  " is not less than the width " + std::to_string(width) +
                  " of the left operand";
          if (!msg.empty()) {
            if (strict_) report(op->getBeginLoc(), msg);
            else warnAt(op->getBeginLoc(), msg);
          }
        } else if (strict_) {
          report(op->getBeginLoc(),
                 "shift count cannot be proven within the left operand's "
                 "width");
        } else {
          warnAt(op->getBeginLoc(),
                 "shift count cannot be proven within the left operand's "
                 "width");
        }
      }
      // Signed division overflow (INT_MIN / -1) is undefined even when the
      // divisor is non-zero.
      if ((opc == BO_Div || opc == BO_Rem || opc == BO_DivAssign ||
           opc == BO_RemAssign) && op->getType()->isSignedIntegerType()) {
        long long a = 0, b = 0;
        bool haveA = false;
        // A compound `v /= -1` / `v %= -1` reads the pre-update constant of
        // the LHS, which knownConst() rejects for an assigned variable.
        if (opc == BO_DivAssign || opc == BO_RemAssign) {
          llvm::APSInt v;
          if (knownConstAtUpdate(op->getLHS(), v) && fits64(v)) {
            a = v.getSExtValue();
            haveA = true;
          }
        } else {
          haveA = asLL(op->getLHS(), a);
        }
        if (haveA && asLL(op->getRHS(), b)) {
          unsigned w = ctx_.getIntWidth(op->getType());
          // `1LL << 63` is UB, so compare the 64-bit case against LLONG_MIN
          // directly. The wider case is handled by wideConstOverflow above.
          bool isMin = false;
          if (w > 1 && w < 64)
            isMin = (a == -(1LL << (w - 1)));
          else if (w == 64)
            isMin = (a == LLONG_MIN);
          if (isMin && b == -1) {
            std::string msg = std::string("signed integer overflow in ") +
                              ((opc == BO_Div || opc == BO_DivAssign)
                                   ? "division" : "remainder");
            if (strict_) report(op->getBeginLoc(), msg);
            else warnAt(op->getBeginLoc(), msg);
          }
        }
      }
      if (opc == BO_Div || opc == BO_Rem || opc == BO_DivAssign ||
          opc == BO_RemAssign) {
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
    // Exclusive mutable borrow: two distinct aliases of the same stack
    // object must not both be used to mutate it.
    if (op->isAssignmentOp()) {
      const Expr *lhs = op->getLHS()->IgnoreParenCasts();
      if (const auto *uo = dyn_cast<UnaryOperator>(lhs))
        if (uo->getOpcode() == UO_Deref)
          if (const auto *ref = dyn_cast<DeclRefExpr>(
                  uo->getSubExpr()->IgnoreParenCasts())) {
            std::string pname = ref->getDecl()->getNameAsString();
            auto ait = stackAliasOwner_.find(pname);
            if (ait != stackAliasOwner_.end()) {
              auto prior = aliasWriter_.find(ait->second);
              if (prior != aliasWriter_.end() && prior->second != pname) {
                std::string msg =
                    "exclusive mutable borrow violated: '" + pname +
                    "' and '" + prior->second + "' alias '" + ait->second + "'";
                if (strict_) report(op->getBeginLoc(), msg);
                else warnAt(op->getBeginLoc(), msg);
              } else {
                aliasWriter_[ait->second] = pname;
              }
            }
          }
    }
    // `p = reallocTemp` rebinds only the owner name; stale aliases stay dead.
    // Clear p's released state (the realloc succeeded) and give it the temp's
    // tracked extent so later subscripts are still bounds-checked.
    if (!reallocRebindOwner_.empty() && op->isAssignmentOp() &&
        varNameOf(op->getLHS()) == reallocRebindOwner_ &&
        varNameOf(op->getRHS()) == reallocRebindTemp_) {
      releasedVars_.erase(reallocRebindOwner_);
      std::string srcName = varNameOf(op->getRHS());
      std::string dstName = varNameOf(op->getLHS());
      auto sit = heapAllocs_.find(srcName);
      if (sit != heapAllocs_.end() && sit->second.bytes > 0 &&
          !dstName.empty()) {
        HeapAllocState st = sit->second;
        if (const auto *ref = dyn_cast<DeclRefExpr>(op->getLHS()->IgnoreParenImpCasts()))
          st.decl = dyn_cast<VarDecl>(ref->getDecl());
        heapAllocs_[dstName] = st;
        identityAllocs_[identityKey(op->getLHS())] = st;
      } else if (!dstName.empty()) {
        // The realloc result has no provable extent (e.g. a size that depends
        // on a loop induction variable).  The old block was transferred to the
        // realloc, so the destination's previous extent is stale: erase it
        // rather than bounds-checking the new object against the old size.
        auto stale = heapAllocs_.find(dstName);
        if (stale != heapAllocs_.end())
          heapAllocs_.erase(stale);
      }
      // The declaration's identity fact must follow the name-keyed fact:
      // keeping the old 4-byte realloc extent here falsely rejected the
      // later, larger rebind in realloc_chain.
      identityAllocs_.erase(identityKey(op->getLHS()));
    }
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
              llvm::APSInt iv;
              if (conditionalDepth_ == 0 && op->getOpcode() == BO_Assign &&
                  knownConst(op->getRHS(), iv) && iv == 0)
                knownStrLen_[vd] = 0;
              else
                knownStrLen_.erase(vd);
            }
          }
        }
      } else if (const auto *ase = dyn_cast<ArraySubscriptExpr>(lhs)) {
        const Expr *base = ase->getBase()->IgnoreParenImpCasts();
        if (const auto *ref = dyn_cast<DeclRefExpr>(base)) {
          if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl())) {
            if (heapStateOf(ref))
              heapWritten_.insert(vd);
            llvm::APSInt idxV, rhsV;
            if (conditionalDepth_ == 0 && op->getOpcode() == BO_Assign &&
                knownConst(ase->getIdx(), idxV) && idxV == 0 &&
                knownConst(op->getRHS(), rhsV) && rhsV == 0)
              knownStrLen_[vd] = 0;
            else
              knownStrLen_.erase(vd);
          }
        }
      }
      if (const auto *ref = dyn_cast<DeclRefExpr>(lhs)) {
        if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl())) {
          // Reassignment hands the pointer an extent we cannot prove, except
          // for the verified `p = reallocTemp` rebind, which carries the new
          // allocation's extent (set above).
          auto hit = heapAllocs_.find(vd->getNameAsString());
          bool isReallocRebind =
              !reallocRebindOwner_.empty() &&
              reallocRebindOwner_ == vd->getNameAsString() &&
              varNameOf(op->getRHS()) == reallocRebindTemp_;
          // `p = q` (or `p = q + 0`) where q directly holds this function's
          // realloc result is another spelling of the same rebind: the
          // success check may be written as a separate `if (!q) return;`
          // statement, which detectReallocSuccess() does not pair with the
          // initializer. Transfer q's extent to p only for that proven RHS;
          // an ordinary pointer assignment must never manufacture an extent.
          std::string rhsName = castedVarName(op->getRHS());
          if (!isReallocRebind && !rhsName.empty() &&
              reallocResultVars_.count(rhsName)) {
            auto rhit = heapAllocs_.find(rhsName);
            if (rhit != heapAllocs_.end() && rhit->second.bytes > 0) {
              HeapAllocState st = rhit->second;
              st.decl = vd;
              heapAllocs_[vd->getNameAsString()] = st;
            } else if (hit != heapAllocs_.end()) {
              // New object with an unknown extent: drop p's stale one rather
              // than bounds-checking the new block against the old size.
              heapAllocs_.erase(hit);
              identityAllocs_.erase(identityKeyForDecl(vd));
            }
            reallocResultVars_.insert(vd->getNameAsString());
            isReallocRebind = true;
          }
          if (hit != heapAllocs_.end() && hit->second.decl == vd &&
              !isReallocRebind) {
            heapAllocs_.erase(hit);
            identityAllocs_.erase(identityKeyForDecl(vd));
          }
          knownStrLen_.erase(vd);
          if (isOnePastEndExpr(op->getRHS()))
            onePastEnd_.insert(vd);
          else
            onePastEnd_.erase(vd); // e.g. `p = a + n; p--;` becomes in-bounds
        }
      }
    }
    if (op->isAdditiveOp()) {
      // Native guard for runtime-valued offsets into a provably sized object.
      maybeRecordCheckedPointer(op);
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
      // Integer-laundered pointer arithmetic, e.g. the LAUNDER macro's
      // `(int *)(uintptr_t)a + 9`: the integer round-trip hides the source
      // extent from the ordinary pointer-arithmetic rule above. Recover the
      // source through pointerSourceOf() and fail closed when the offset
      // leaves that object; the back-cast cannot be proven safe.
      if (op->getOpcode() == BO_Add && isCastLaunderedPointer(lhs)) {
        long long off = 0;
        if (evalIndexValue(rhs, off) && off >= 0) {
          const auto *pt = lhs->getType()->getAs<PointerType>();
          unsigned es =
              pt && !pt->getPointeeType()->isVoidType()
                  ? (unsigned)(ctx_.getTypeSize(pt->getPointeeType()) / 8u)
                  : 0;
          long long bytes = provenanceByteExtent(pointerSourceOf(lhs));
          if (es && bytes > 0 && off >= bytes / (long long)es)
            warnAt(op->getBeginLoc(),
                   "pointer created from integer value has unverifiable "
                   "provenance");
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
      // Look through explicit casts: `int *q = (int *)(void *)p` (written here
      // as `q = (int *)(void *)p`) must create the same alias group as `q = p`,
      // otherwise releasing p leaves q's dereference unreported.
      std::string lhsName = castedVarName(op->getLHS());
      std::string rhsName = castedVarName(op->getRHS());
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
      if (lhsVar && lhsVar->getType()->isPointerType())
        branchNullifiedVars_.erase(lhsVar->getNameAsString());
      if (isNullConstant(op->getRHS())) {
        // `p = NULL` (including casts such as `(int *)NULL`) makes p provably
        // null until it is reassigned or guarded.
        if (lhsVar && lhsVar->getType()->isPointerType())
          nullVars_.insert(lhsVar->getNameAsString());
      } else if (lhsVar) {
        nullVars_.erase(lhsVar->getNameAsString());
      }
      const Expr *rhsExpr = op->getRHS()->IgnoreParenImpCasts();
      const Expr *lhsExpr = op->getLHS()->IgnoreParenImpCasts();
      // A fresh value assigned to a released pointer makes it live again.
      // Generalise the old null/`&object` special cases to any value that is
      // not itself derived from a released pointer: `f = fopen(...)`,
      // `p = malloc(n)`, `q = live_pointer`, ...  The RHS guard is essential:
      // `p = p`, `p = (void *)p`, `p = q` with q dangling and integer-launder
      // round-trips must keep the release state or they would launder a UAF.
      // The verified realloc rebind (`p = tmp`) is handled earlier in this
      // visitor and deliberately keeps stale aliases dead, so skip it here.
      bool isReallocRebind = !reallocRebindOwner_.empty() &&
                             varNameOf(lhsExpr) == reallocRebindOwner_ &&
                             varNameOf(rhsExpr) == reallocRebindTemp_;
      // Another valid spelling of the same rebind is `p = q` where q holds
      // this function's realloc result but the success guard was written as
      // a separate statement. q is a fresh block, so assigning it to p must
      // revive p even when the guard-specific rebind flag was not installed.
      if (!isReallocRebind && reallocResultVars_.count(varNameOf(rhsExpr)))
        isReallocRebind = true;
      if (isReallocRebind && !varNameOf(lhsExpr).empty()) {
        clearReleased(varNameOf(lhsExpr));
        maybeReleasedVars_.erase(varNameOf(lhsExpr));
        // free(p) also records a field-style release key; clear it too so
        // isReleased() does not re-derive a dangling state from that key.
        releasedFields_.erase(varNameOf(lhsExpr));
        releasedFields_.erase(lvalueKey(lhsExpr));
      }
      if (!derivedFromReleasedPointer(rhsExpr) && !isReallocRebind) {
        std::string lhsVarName = varNameOf(lhsExpr);
        clearReleased(lhsVarName);
        maybeReleasedVars_.erase(lhsVarName);
        // Field-sensitive release state (`s.p = fresh` after `free(s.p)`).
        std::string lhsKey = lvalueKey(lhsExpr);
        if (!lhsKey.empty()) {
          clearReleased(lhsKey);
          releasedFields_.erase(lhsKey);
        }
        releasedFields_.erase(lhsVarName);
      }
      if (handlers_.count(currentFunction_))
        reportGlobalWrite(op->getLHS(), op->getBeginLoc());

    }
    return true;
  }

  bool VisitUnaryOperator(UnaryOperator *op) {
    if (report_ && op->getOpcode() == UO_Deref) {
      if (const auto *ref = dyn_cast<DeclRefExpr>(
              op->getSubExpr()->IgnoreParenImpCasts()))
        if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl()))
          if (unprovenPtrOffset_.count(vd->getCanonicalDecl()))
            report(op->getBeginLoc(),
                   "dereference of pointer formed with an unproven runtime "
                   "offset; STRICT cannot prove it is in bounds");
    }
    // A local whose address is taken can be written by an output parameter
    // (scanf, read, memcpy, ...), so it must never be folded as a constant.
    if (op->getOpcode() == UO_AddrOf) {
      const Expr *sub = op->getSubExpr()->IgnoreParenImpCasts();
      if (const auto *ref = dyn_cast<DeclRefExpr>(sub))
        if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl()))
          addressTakenVars_.insert(vd->getNameAsString());
    }
    if (!report_)
      return true;
    // A `++`/`--` on a local whose compile-time value sits at the signed
    // boundary provably overflows; STRICT rejects it statically just like the
    // equivalent constant compound assignment.
    if (strict_ && (op->isIncrementOp() || op->isDecrementOp()) &&
        op->getType()->isSignedIntegerType()) {
      llvm::APSInt v;
      if (knownConstAtUpdate(op->getSubExpr(), v) && fits64(v)) {
        unsigned w = ctx_.getIntWidth(op->getType());
        if (w > 1 && w <= 64) {
          __int128 val = v.getSExtValue();
          __int128 max = ((__int128)1 << (w - 1)) - 1;
          __int128 min = -((__int128)1 << (w - 1));
          if ((op->isIncrementOp() && val == max) ||
              (op->isDecrementOp() && val == min))
            report(op->getBeginLoc(),
                   "signed integer overflow in increment/decrement of a "
                   "provably constant value at the type boundary");
        }
      }
    }
    if (recordArithmeticSites_) {
      if (op->isIncrementOp() || op->isDecrementOp())
        recordSimpleSite(op->getBeginLoc(), op->getOperatorLoc(),
                         op->getEndLoc(),
                         op->isIncrementOp() ? CheckedArithmeticSite::Op::Inc
                                             : CheckedArithmeticSite::Op::Dec,
                         op->getType(), op->isPostfix(), op->getSubExpr());
      else if (op->getOpcode() == UO_Minus)
        recordSimpleSite(op->getBeginLoc(), op->getOperatorLoc(),
                         op->getEndLoc(), CheckedArithmeticSite::Op::Neg,
                         op->getType(), false, op->getSubExpr());
    }
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
    // Unary minus on the most-negative signed value overflows.  This is a
    // static-only check: the native-site rewriter only understands binary
    // operators, so emitting a site here would change the generated C.
    if (op->getOpcode() == UO_Minus && op->getType()->isSignedIntegerType()) {
      llvm::APSInt v;
      unsigned w = ctx_.getIntWidth(op->getType());
      __int128 minVal = w < 128 ? -((__int128)1 << (w - 1)) : 0;
      if (knownConst(op->getSubExpr(), v) && w > 1 && w <= 64 &&
          (__int128)v.getSExtValue() == minVal) {
        if (strict_)
          report(op->getBeginLoc(),
                 "signed integer overflow in unary minus of minimum value");
        else
          warnAt(op->getBeginLoc(),
                 "signed integer overflow in unary minus of minimum value");
      }
    }
    if (op->getOpcode() == UO_Deref) {
      const Expr *sub = op->getSubExpr();
      if (const Expr *forged = forgedUnionPointerSource(sub))
        reportNamed(forged->getBeginLoc(), "CPLUS-FFI",
                    "forged pointer from union type punning has unverifiable "
                    "provenance",
                    "suggested correction: store a real pointer into the "
                    "union's pointer member instead of fabricating one from "
                    "an integer constant",
                    true);
      if (isReleased(sub))
        reportUaf(op->getBeginLoc(), varNameOf(sub),
                  "dereference of released pointer '" +
                      (varNameOf(sub).empty() ? lvalueKey(sub) : varNameOf(sub)) +
                      "'");
      else if (isMaybeReleased(sub, op->getBeginLoc()))
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
    // Diagnostics about the C library's own integer-to-pointer tricks (e.g.
    // macOS OSByteOrder.h) are not actionable in C+ source and must not make
    // an otherwise valid program fail under -Werror.
    if (loc.isValid() && ctx_.getSourceManager().isInSystemHeader(loc))
      return;
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

  /// Named diagnostic for the F2 integer-laundered stale alias. STRICT makes
  /// this an error; DEFAULT only warns because the cast may be intentional
  /// (e.g. a tagged handle whose provenance the checker cannot prove).
  void reportStaleIntegerAlias(SourceLocation loc) {
    if (!staleIntegerAliasReported_.insert(loc.getRawEncoding()).second)
      return;
    PresumedLoc ploc = ctx_.getSourceManager().getPresumedLoc(loc);
    Diag d;
    // Freeing through a laundered integer address is UB in every C mode; a
    // tagged handle passed to free is not a legitimate use of the cast.
    d.level = DiagLevel::Error;
    d.message = "[CPLUS-LIFETIME] stale integer-laundered alias passed to free";
    d.help = "suggested correction: keep the owning pointer and release it "
             "exactly once; an integer address may name a different object "
             "after allocation reuse";
    d.file = filename_;
    if (ploc.isValid()) {
      d.line = ploc.getLine();
      d.column = ploc.getColumn();
      std::string diagFile = ploc.getFilename();
      if (diagFile == filename_ + ".c")
        diagFile = filename_;
      if (diagFile != filename_)
        d.file = diagFile;
    }
    diags_.report(std::move(d));
  }

  /// Emit a compile-time-only diagnostic with an explicit category prefix.
  /// STRICT makes the new rejections errors; DEFAULT downgrades to warnings so
  /// unsanitized builds keep their historical behaviour. No runtime change.
  void reportNamed(SourceLocation loc, const std::string &category,
                   const std::string &message,
                   const std::string &help = std::string(),
                   bool alwaysError = false) {
    PresumedLoc ploc = ctx_.getSourceManager().getPresumedLoc(loc);
    Diag d;
    // A few source shapes are UB in every C mode (address of a frame local
    // escaping, forged union pointers, laundered stale aliases). They are
    // errors even in DEFAULT; STRICT still promotes all named diagnostics.
    d.level = (strict_ || alwaysError) ? DiagLevel::Error : DiagLevel::Warning;
    d.message = "[" + category + "] " + message;
    d.help = help;
    d.file = filename_;
    if (ploc.isValid()) {
      d.line = ploc.getLine();
      d.column = ploc.getColumn();
      std::string diagFile = ploc.getFilename();
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

  struct StringDestination {
    const VarDecl *var = nullptr;
    long long offset = 0;
    unsigned long long capacity = 0;
    bool capacityKnown = false;
  };

  StringDestination stringDestination(const Expr *e) const {
    StringDestination dst;
    const Expr *x = e->IgnoreParenImpCasts();
    while (const auto *bo = dyn_cast<BinaryOperator>(x)) {
      if (bo->getOpcode() != BO_Add && bo->getOpcode() != BO_Sub)
        break;
      llvm::APSInt amount;
      if (!knownConst(bo->getRHS(), amount) || !fits64(amount))
        return StringDestination{};
      long long delta = amount.getSExtValue();
      if (bo->getOpcode() == BO_Sub)
        delta = -delta;
      long long elemSize = 1;
      if (const auto *ptr = bo->getLHS()->getType()->getAs<PointerType>())
        if (!ptr->getPointeeType()->isVoidType() &&
            !ptr->getPointeeType()->isIncompleteType())
          elemSize = ctx_.getTypeSize(ptr->getPointeeType()) / 8;
      if (elemSize <= 0 || !addScaledOffset(dst.offset, delta, elemSize))
        return StringDestination{};
      x = bo->getLHS()->IgnoreParenImpCasts();
    }
    if (const auto *uo = dyn_cast<UnaryOperator>(x)) {
      if (uo->getOpcode() != UO_AddrOf)
        return dst;
      const auto *ase = dyn_cast<ArraySubscriptExpr>(
          uo->getSubExpr()->IgnoreParenImpCasts());
      if (!ase)
        return dst;
      llvm::APSInt index;
      if (!knownConst(ase->getIdx(), index) || !fits64(index) ||
          index.isNegative())
        return dst;
      const Expr *base = ase->getBase()->IgnoreParenImpCasts();
      long long elemSize = 0;
      if (const auto *at = ctx_.getAsConstantArrayType(base->getType())) {
        elemSize = ctx_.getTypeSize(at->getElementType()) / 8;
      } else if (const auto *pt = base->getType()->getAs<PointerType>()) {
        // `&heap_buf[k]`: the base is a heap allocation, not a fixed array.
        // Resolve its pointee size here; the VarDecl/capacity resolution
        // below uses heapStateOf() and subtracts the resulting byte offset.
        QualType elem = pt->getPointeeType();
        if (!elem->isVoidType() && !elem->isIncompleteType() &&
            !elem->isDependentType())
          elemSize = ctx_.getTypeSize(elem) / 8;
      }
      if (elemSize <= 0 ||
          !addScaledOffset(dst.offset, index.getSExtValue(), elemSize))
        return StringDestination{};
      x = base;
    }
    const auto *ref = dyn_cast<DeclRefExpr>(x);
    if (!ref)
      return dst;
    dst.var = dyn_cast<VarDecl>(ref->getDecl());
    if (!dst.var || dst.offset < 0)
      return StringDestination{};
    if (const auto *at = ctx_.getAsConstantArrayType(dst.var->getType())) {
      if (!at->getElementType()->isCharType())
        return StringDestination{};
      dst.capacity = at->getSize().getZExtValue();
      dst.capacityKnown = true;
    } else if (const HeapAllocState *hs = heapStateOf(ref)) {
      dst.capacity = hs->bytes;
      dst.capacityKnown = true;
    }
    if (dst.offset > 0 && dst.capacityKnown) {
      if (static_cast<unsigned long long>(dst.offset) >= dst.capacity)
        dst.capacity = 0;
      else
        dst.capacity -= dst.offset;
    }
    return dst;
  }

  const StringLiteral *stringLiteralOf(const Expr *e) const {
    const Expr *x = e->IgnoreParenCasts();
    if (const auto *str = dyn_cast<StringLiteral>(x))
      return str;
    if (const auto *ref = dyn_cast<DeclRefExpr>(x))
      if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl()))
        if (vd->getInit())
          return dyn_cast<StringLiteral>(vd->getInit()->IgnoreParenCasts());
    return nullptr;
  }

  void invalidateStringLength(const StringDestination &dst) {
    if (dst.var)
      knownStrLen_.erase(dst.var);
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
    long long bytes = 0;
    // evalByteCount also understands helper-computed constants
    // (`static size_t sz(void){return 40;}`), which knownConst misses.
    if (!evalByteCount(n, bytes) || bytes <= 0)
      return;
    const StringDestination destination = stringDestination(dst);
    if (op == "copy" && call->getNumArgs() > 1) {
      if (const StringLiteral *str = stringLiteralOf(call->getArg(1))) {
        if (destination.var && destination.offset == 0 &&
            static_cast<unsigned long long>(bytes) == str->getLength() + 1)
          knownStrLen_[destination.var] = str->getLength();
        else
          invalidateStringLength(destination);
      } else {
        invalidateStringLength(destination);
      }
    } else if (op == "string copy") {
      invalidateStringLength(destination);
    }
    if (!destination.var || !destination.capacityKnown)
      return;
    if (static_cast<unsigned long long>(bytes) > destination.capacity)
      report(call->getBeginLoc(), op + " of " + std::to_string(bytes) +
                                      " bytes exceeds destination '" +
                                      destination.var->getNameAsString() + "'");
  }

  void checkBoundedCopy(const CallExpr *call,
                        const std::string &op = "copy") {
    checkBoundedWrite(call, 0, 2, op);
  }

  /// strcat(dst, src): current content + src + NUL must fit the destination.
  void checkStringConcat(const CallExpr *call) {
    if (call->getNumArgs() < 2)
      return;
    const StringDestination dst =
        stringDestination(call->getArg(0)->IgnoreParenImpCasts());
    if (!dst.var)
      return;
    const StringLiteral *src =
        stringLiteralOf(call->getArg(1)->IgnoreParenCasts());
    if (!src) {
      invalidateStringLength(dst);
      return;
    }
    auto used = knownStrLen_.find(dst.var);
    if (used == knownStrLen_.end() || dst.offset != 0) {
      unsigned long long need = src->getLength() + 1;
      if (dst.capacityKnown && need > dst.capacity)
        report(call->getBeginLoc(),
               "string concatenation needs " + std::to_string(need) +
                   " bytes but destination '" + dst.var->getNameAsString() +
                   "' has capacity " + std::to_string(dst.capacity));
      return;
    }
    unsigned long long need = used->second + src->getLength() + 1;
    if (need > dst.capacity)
      report(call->getBeginLoc(),
             "string concatenation needs " + std::to_string(need) +
                 " bytes but destination '" + dst.var->getNameAsString() +
                 "' has capacity " + std::to_string(dst.capacity));
    else
      used->second += src->getLength();
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

  /// Detect strcpy from a constant string into a smaller array or allocation.
  void checkStringCopy(const CallExpr *call) {
    const StringDestination dst =
        stringDestination(call->getArg(0)->IgnoreParenImpCasts());
    if (!dst.var)
      return;
    const StringLiteral *str =
        stringLiteralOf(call->getArg(1)->IgnoreParenCasts());
    if (!str) {
      invalidateStringLength(dst);
      return;
    }
    unsigned long long need = str->getLength() + 1;
    if (dst.capacityKnown && need > dst.capacity)
      report(call->getBeginLoc(), "string copy of " + std::to_string(need - 1) +
                                      " chars plus NUL exceeds destination '" +
                                      dst.var->getNameAsString() + "'");
    if (dst.offset == 0)
      knownStrLen_[dst.var] = str->getLength();
    else
      invalidateStringLength(dst);
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

  /// Apply release/transfer effects for a call, including indirect calls
  /// whose callee is a resolved function pointer. Called before the
  /// early-return for calls with no direct callee so fnptr releases are never
  /// silently skipped (S2/T1).
  void applyReleaseEffects(const CallExpr *call, const std::string &callName,
                             const FunctionDecl *directCallee) {
    if (!report_ || !call)
      return;
    // ---- Resolved / unverified indirect releases -----------------------
    // An indirect call is not invisible: once targets are resolved, a
    // `free`/`cplus_release` target releases its argument exactly like a
    // direct call.  A callee we cannot resolve still receives the pointer,
    // so in STRICT it is an unverified release/transfer and fails closed.
    {
      const std::set<std::string> targets = calleeNames(call, directCallee);
      for (const auto &target : targets) {
        // Direct free/release calls already have their own state machine;
        // this block handles only calls through a resolved indirect callee.
        if (!isReleaseName(target) || call->getNumArgs() < 1 ||
            call->getDirectCallee())
          continue;
        checkInvalidFree(call->getBeginLoc(), call->getArg(0), target);
        const std::string an = castedVarName(call->getArg(0));
        if (an.empty())
          continue;
        if (isReleased(call->getArg(0)) || releasedVars_.count(an))
          report(call->getBeginLoc(), "double free of '" + an +
                                         "' through indirect call to '" +
                                         target + "'");
        else {
          releaseLocs_[an] = call->getBeginLoc();
          markReleased(an);
        }
      }
      // Global handles freed by a callee (`setg(p); freeg(); *p`) and by
      // atexit callbacks taint the allocation the handle points at.
      std::set<std::string> effectTargets = targets;
      if (callName == "atexit" || targets.count("atexit"))
        effectTargets.insert(atexitFunctions_.begin(), atexitFunctions_.end());
      for (const auto &t : effectTargets) {
        auto fit = freedGlobals_.find(t);
        if (fit == freedGlobals_.end())
          continue;
        for (const auto &g : fit->second) {
          bool dangling = releasedVars_.count(g) != 0;
          std::set<std::string> affected{g};
          auto pit = globalPointsTo_.find(g);
          if (pit != globalPointsTo_.end())
            affected.insert(pit->second.begin(), pit->second.end());
          auto ait = globalAllocPointsTo_.find(g);
          if (ait != globalAllocPointsTo_.end())
            for (const auto &id : ait->second) {
              auto mit = allocIdentityMembers_.find(id);
              if (mit != allocIdentityMembers_.end())
                affected.insert(mit->second.begin(), mit->second.end());
            }
          for (const auto &a : affected)
            if (releasedVars_.count(a))
              dangling = true;
          if (dangling)
            report(call->getBeginLoc(),
                   "double free of global handle '" + g + "' via '" + t +
                       "'");
          else
            for (const auto &a : affected)
              markReleased(a);
        }
      }
      // Indirect calls to user helpers with a free-effect summary transfer
      // the obligation to this call site too (S2_fnptr_free). Direct calls
      // keep the existing summary loop below; never apply both.
      if (!call->getDirectCallee()) {
        for (const auto &target : targets) {
          auto uit = userFrees_.find(target);
          if (uit == userFrees_.end())
            continue;
          for (int idx : uit->second) {
            if (idx < 0 || (unsigned)idx >= call->getNumArgs())
              continue;
            const std::string an = castedVarName(call->getArg((unsigned)idx));
            if (an.empty())
              continue;
            if (releasedVars_.count(an) || isReleased(call->getArg((unsigned)idx)))
              report(call->getBeginLoc(),
                     "double free after indirect call to '" + target +
                         "' which frees its argument");
            else
              markReleased(an);
          }
        }
      }
      if (strict_ && !call->getDirectCallee() && targets.empty()) {
        auto trackedHeap = [&](const std::string &n) {
          if (n.empty())
            return false;
          if (heapAllocs_.count(n))
            return true;
          auto it = aliasGroups_.find(n);
          if (it != aliasGroups_.end())
            for (const auto &a : it->second)
              if (heapAllocs_.count(a))
                return true;
          return varAllocIdentity_.count(n) != 0;
        };
        for (const Expr *arg : call->arguments()) {
          if (!arg || !arg->getType()->isPointerType())
            continue;
          const std::string an = castedVarName(arg);
          if (!trackedHeap(an))
            continue;
          report(call->getBeginLoc(),
                 "indirect call passes heap pointer '" + an +
                     "' to an unresolved callee; possible unverified "
                     "release or ownership transfer");
          break;
        }
      }
    }  }


  bool VisitCallExpr(CallExpr *call) {
    const FunctionDecl *callee = call->getDirectCallee();
    std::string rawName = callee ? callee->getNameAsString() : std::string();
    std::string name = normalizeFortifyName(rawName);

    // Lock discipline. State is per function; an unlock without a matching
    // lock and opposite nesting orders across functions are diagnosed in
    // STRICT. This is static analysis only: no runtime concurrency service is
    // introduced and the C ABI is untouched.
    if (report_ && call->getNumArgs() >= 1) {
      const std::string mutex = lockNameOf(call->getArg(0));
      if (!mutex.empty()) {
        if (isMutexLockCall(rawName)) {
          for (const auto &held : heldLocks_) {
            if (held.first == mutex)
              continue;
            auto order = std::make_pair(held.first, mutex);
            auto reverse = std::make_pair(mutex, held.first);
            auto rit = lockOrders_.find(reverse);
            // The opposite order may have been recorded by an earlier function
            // (or by this function in a previous traversal); either direction
            // is evidence of a lock-order inversion.
            if (rit != lockOrders_.end() && rit->second != currentFunction_)
              warnAt(call->getBeginLoc(),
                     "inconsistent lock order: '" + mutex + "' and '" +
                         held.first +
                         "' are acquired in opposite orders by different "
                         "functions; potential deadlock");
            if (!lockOrders_.count(order))
              lockOrders_[order] = currentFunction_;
          }
          ++heldLocks_[mutex];
        } else if (isMutexUnlockCall(rawName)) {
          auto it = heldLocks_.find(mutex);
          if (it == heldLocks_.end())
            warnAt(call->getBeginLoc(),
                   "mutex '" + mutex + "' is unlocked without a matching "
                                        "lock in this function");
          else if (--it->second == 0)
            heldLocks_.erase(it);
        }
      }
    }

    // F2: a stale integer-laundered alias must not reach a release. Checked
    // before any early return so indirect releases and wrappers are covered.
    checkStaleIntegerAliasArgs(call, name, callee);

    // ---- Interprocedural effect fixpoint ---------------------------------
    // Summaries are built in the collector so the STRICT pass sees forward
    // references, two-level relays and resolved function-pointer/callback
    // calls.  `paramIndexOf` follows local aliases and `p + 0`.
    if ((name == "free" || name == "cplus_release") && call->getNumArgs() >= 1) {
      int slot = paramIndexOf(call->getArg(0));
      if (slot >= 0)
        userFrees_[currentFunction_].insert(slot);
      // `free(*pp)` / `free(pp[0])` releases the object the pointer
      // parameter POINTS TO, not the parameter itself. Record that shape
      // separately so a caller passing `&p` can be tainted at the call site.
      const Expr *ae = call->getArg(0)->IgnoreParenImpCasts();
      const Expr *inner = nullptr;
      if (const auto *uo = dyn_cast<UnaryOperator>(ae))
        if (uo->getOpcode() == UO_Deref)
          inner = uo->getSubExpr()->IgnoreParenImpCasts();
      if (!inner)
        if (const auto *ase = dyn_cast<ArraySubscriptExpr>(ae)) {
          Expr::EvalResult r;
          if (ase->getIdx()->EvaluateAsInt(r, ctx_) && r.Val.isInt() &&
              r.Val.getInt().isZero())
            inner = ase->getBase()->IgnoreParenImpCasts();
        }
      if (const auto *ref = inner ? dyn_cast<DeclRefExpr>(inner) : nullptr)
        if (const auto *pv = dyn_cast<ParmVarDecl>(ref->getDecl()))
          if (pv->getDeclContext() ==
              static_cast<const DeclContext *>(currentFnDecl_))
            userFreesPointee_[currentFunction_].insert(
                (int)pv->getFunctionScopeIndex());
    }
    if (!report_ && isReleaseName(name) && call->getNumArgs() >= 1) {
      const std::string g = varNameOf(call->getArg(0));
      if (!g.empty() && allGlobals_.count(g))
        freedGlobals_[currentFunction_].insert(g);
    }
    if (!report_ && name == "atexit" && call->getNumArgs() >= 1) {
      const Expr *a = stripCalleeCasts(call->getArg(0));
      if (const auto *ref = dyn_cast_or_null<DeclRefExpr>(a))
        if (const auto *fd = dyn_cast_or_null<FunctionDecl>(ref->getDecl()))
          atexitFunctions_.insert(fd->getNameAsString());
    }
    if (!report_) {
      std::set<std::string> targets = calleeNames(call, callee);
      std::set<int> addFrees;
      std::set<std::pair<std::string, unsigned>> addDerefs;
      for (const auto &target : targets) {
        // An indirect call resolving to `free`/`cplus_release` is a release
        // of its first argument, just like the direct call.
        if (isReleaseName(target) && call->getNumArgs() >= 1) {
          const std::string g = varNameOf(call->getArg(0));
          if (!g.empty() && allGlobals_.count(g))
            freedGlobals_[currentFunction_].insert(g);
          int ps = paramIndexOf(call->getArg(0));
          if (ps >= 0)
            addFrees.insert(ps);
        }
        auto fit = userFrees_.find(target);
        if (fit != userFrees_.end())
          for (int slot : fit->second) {
            // Issue #226: a summary slot belongs to the callee's declared
            // parameter list. A cast/variadic call can supply fewer arguments
            // than that slot, so never index past this call site's arguments.
            if (slot < 0 || static_cast<unsigned>(slot) >= call->getNumArgs())
              continue;
            int ps = paramIndexOf(call->getArg((unsigned)slot));
            if (ps >= 0)
              addFrees.insert(ps);
          }
        for (const auto &pr : derefParams_)
          if (pr.first == target && pr.second < call->getNumArgs()) {
            int ps = paramIndexOf(call->getArg(pr.second));
            if (ps >= 0)
              addDerefs.insert({currentFunction_, (unsigned)ps});
          }
        // A callback argument binds the callee's function-pointer parameter.
        for (unsigned i = 0; callee && i < call->getNumArgs() &&
                             i < callee->getNumParams(); ++i) {
          if (!isFunctionPointerParam(callee->parameters()[i]))
            continue;
          if (const auto *fr = dyn_cast<DeclRefExpr>(call->getArg(i)->IgnoreParenCasts()))
            if (const auto *fd = dyn_cast<FunctionDecl>(fr->getDecl()))
              fnPtrParamTargets_[target][i].insert(fd->getNameAsString());
        }
        // Global points-to through a helper: `void store(int *p){g=p;}` used
        // as `store(q)` makes g point at q.
        auto git = globalParamStores_.find(target);
        if (git != globalParamStores_.end())
          for (const auto &kv : git->second) {
            if (kv.first >= call->getNumArgs())
              continue;
            std::string an = castedVarName(call->getArg(kv.first));
            if (!an.empty())
              for (const auto &g : kv.second)
                registerGlobalPointsTo(g, an);
          }
      }
      userFrees_[currentFunction_].insert(addFrees.begin(), addFrees.end());
      derefParams_.insert(addDerefs.begin(), addDerefs.end());
    }
    // FORTIFY builtins insert extra object-size/flag arguments before the
    // format string.
    unsigned fmtShift = 0;
    if (rawName.rfind("__builtin___", 0) == 0) {
      if (rawName.find("nprintf_chk") != std::string::npos) // sn/vsnprintf
        fmtShift = 2;
      else if (rawName.find("printf_chk") != std::string::npos) // printf
        fmtShift = 1;
    }
    // Register every constant-sized allocator call, including allocations
    // assigned through out-parameters rather than a simple initializer.
    if (name == "malloc" || name == "calloc" || isReallocName(name)) {
      unsigned argIdx = isReallocName(name) ? 1 : 0;
      if (call->getNumArgs() > argIdx) {
        long long n = -1;
        if (evalByteCount(call->getArg(argIdx)->IgnoreParenImpCasts(), n) &&
            n > 0) {
          if (name == "calloc" && call->getNumArgs() >= 2) {
            long long es = 1;
            if (evalByteCount(call->getArg(1)->IgnoreParenImpCasts(), es) &&
                es > 0 && n <= LLONG_MAX / es)
              n *= es;
          }
          recordAllocSite(call, n);
        } else {
          // Runtime element count: check the multiplication for wraparound.
          recordAllocExtentSite(call, argIdx);
        }
      }
    }
    if (name == "free")
      recordFreeSite(call);
    // A released pointer must not cross a function boundary: the callee may
    // dereference it and no single-TU summary is needed to see the risk.
    // free/realloc are handled by their own release/double-free checks.
    if (report_ && name != "free" && name != "cplus_release" &&
        !isReallocName(name) && name != "cplus_check_live") {
      for (const Expr *arg : call->arguments()) {
        std::string an = castedVarName(arg);
        if (an.empty())
          continue;
        if (releasedVars_.count(an) || isReleased(arg)) {
          std::string msg = "use-after-free: released pointer '" + an +
                            "' passed to '" + name + "'";
          if (strict_)
            report(call->getBeginLoc(), msg);
          else
            warnAt(call->getBeginLoc(), msg);
          break;
        }
      }
    }
    // Interprocedural nullability: a provably null argument crossing into a
    // callee that dereferences that parameter is unsafe even though the
    // dereference happens in the callee's body.
    if (report_) {
      bool reported = false;
      for (const auto &target : calleeNames(call, callee)) {
        for (unsigned i = 0; i < call->getNumArgs() && !reported; ++i) {
          if (!derefParams_.count({target, i}))
            continue;
          // Canonicalize `p + 0` / `(T *)p` so a null pointer laundered
          // through a zero offset is still recognized at the boundary (s20).
          const Expr *arg = canonicalPointer(call->getArg(i));
          std::string an = castedVarName(arg);
          bool isNull = isNullConstant(arg) || (!an.empty() && nullVars_.count(an));
          llvm::APSInt cv;
          if (!isNull && knownConst(arg, cv))
            isNull = cv.isZero();
          if (!isNull)
            continue;
          std::string msg = "null pointer argument " + std::to_string(i) +
                            " passed to '" + target + "'";
          if (strict_)
            report(call->getBeginLoc(), msg);
          else
            warnAt(call->getBeginLoc(), msg);
          reported = true;
        }
      }
    }
    // Rust-style exclusive mutable borrow: two `&x` arguments to a callee
    // that WRITES through both parameter slots alias the same object.  In
    // Rust this is rejected by the borrow checker; plain C permits it, but
    // STRICT can diagnose the provable case without changing ABI.
    if (report_) {
      for (const auto &target : calleeNames(call, callee)) {
        std::map<std::string, unsigned> borrowed;
        bool reported = false;
        for (unsigned i = 0; i < call->getNumArgs() && !reported; ++i) {
          if (!paramWriteSlots_.count({target, i}))
            continue;
          const Expr *arg = call->getArg(i)->IgnoreParenImpCasts();
          const auto *uo = dyn_cast<UnaryOperator>(arg);
          if (!uo || uo->getOpcode() != UO_AddrOf)
            continue;
          const auto *ref = dyn_cast<DeclRefExpr>(
              uo->getSubExpr()->IgnoreParenImpCasts());
          if (!ref)
            continue;
          std::string owner = ref->getDecl()->getNameAsString();
          auto prior = borrowed.find(owner);
          if (prior != borrowed.end()) {
            std::string msg = "exclusive mutable borrow violated: arguments " +
                              std::to_string(prior->second) + " and " +
                              std::to_string(i) + " both borrow '&" + owner +
                              "' passed to '" + target + "'";
            // STRICT rejects; DEFAULT only warns (Rust-like exclusivity is
            // intentionally opt-in for ordinary C compatibility).
            if (strict_)
              report(call->getBeginLoc(), msg);
            else
              warnAt(call->getBeginLoc(), msg);
            reported = true;
          } else {
            borrowed[owner] = i;
          }
        }
      }
    }
    // A callee that writes through a parameter must not receive a pointer
    // produced by casting away const from a real const object (UB store).
    if (report_) {
      FunctionEffect ce = libcEffect(name);
      for (unsigned i = 0; i < call->getNumArgs(); ++i) {
        std::string constObj;
        if (!constStrippedTarget(call->getArg(i), constObj))
          continue;
        bool writes = false;
        for (int w : ce.writeArgs)
          if (w == (int)i)
            writes = true;
        for (const auto &t : calleeNames(call, callee))
          if (derefParams_.count({t, i}))
            writes = true;
        if (writes) {
          std::string msg = "cast discards const qualifier of '" + constObj +
                            "' passed to a function that writes through it";
          if (strict_) report(call->getBeginLoc(), msg);
          else warnAt(call->getBeginLoc(), msg);
        }
      }
    }
    // Must run before the early return below: indirect calls have no direct
    // callee and would otherwise skip every release/effect check.
    applyReleaseEffects(call, name, call->getDirectCallee());
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
      if (!callee && name.empty()) {
        // A function pointer held in a struct field, global, array slot or
        // parameter still resolves to the libc format function it targets.
        // Without this, `ops.log(fmt)` / `tbl[0](fmt)` bypass the literal
        // format rule even though the call graph is fully known.
        for (const auto &t : calleeNames(call, nullptr))
          if (isFormatFunctionName(t)) {
            name = t;
            break;
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
                 name == "cplus_snprintf_s" || name == "vsnprintf")
          fmtIdx = 2;
        else if (name == "vsprintf" || name == "vfprintf")
          fmtIdx = 1;
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
            // A non-literal format from a local/global buffer is CWE-134 in
            // every C mode; an fnptr alias to printf is not a way around it.
            // A wrapper that forwards its own FORMAT PARAMETER (e.g. a
            // logging helper calling vsnprintf) is the legitimate exception.
            const Expr *bare = fmt->IgnoreParenImpCasts();
            bool forwarded = false;
            if (const auto *ref = dyn_cast<DeclRefExpr>(bare))
              if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl())) {
                std::string vn = vd->getNameAsString();
                forwarded = isa<ParmVarDecl>(vd) ||
                            vn.find("format") != std::string::npos ||
                            vn.find("fmt") != std::string::npos;
              }
            if (forwarded)
              warnAt(call->getBeginLoc(), msg);
            else
              report(call->getBeginLoc(), msg);
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
                unsigned long long cap = 0;
                const VarDecl *vd = charDestOf(call->getArg(a), cap);
                if (vd && cap) {
                  std::string msg = "unbounded '%s' in " + name +
                                    " writes into fixed-size array '" +
                                    vd->getNameAsString() +
                                    "'; add a field width";
                  if (strict_) report(call->getBeginLoc(), msg);
                  else warnAt(call->getBeginLoc(), msg);
                }
              }
             if (isScanf)
               for (const auto &spec : scanfStringWidths(lit)) {
                 if (spec.second <= 0)
                   continue; // unbounded case reported above
                 unsigned pos = fmtIdx + 1 + spec.first;
                 if (pos >= call->getNumArgs())
                   continue;
                 unsigned long long cap = 0;
                 const VarDecl *vd = charDestOf(call->getArg(pos), cap);
                 if (!vd || !cap)
                   continue;
                 if ((unsigned long long)spec.second + 1 > cap) {
                   std::string msg = "scanf field width " +
                                     std::to_string(spec.second) + " for '%s' "
                                     "exceeds destination '" +
                                     vd->getNameAsString() + "' of " +
                                     std::to_string(cap) + " bytes";
                   if (strict_) report(call->getBeginLoc(), msg);
                   else warnAt(call->getBeginLoc(), msg);
                 }
               }
            if (isPrintf) {
              // A fixed char array whose initializer has no NUL byte cannot be
              // passed to %s without an over-read.
              for (unsigned a = fmtIdx + 1; a < call->getNumArgs(); ++a) {
                unsigned long long cap = 0;
                const VarDecl *vd =
                    charDestOf(call->getArg(a), cap, /*allowPointerAlias=*/false);
                // Only inspect the initializer of an actual array object; a
                // struct field's bytes are nested and cannot be read from the
                // base variable's flat initializer list.
                if (!vd || !vd->getType()->isArrayType() || !vd->getInit())
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
                  terminated = sl->getLength() < cap;
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

    if (callee) {
      // Record allocation extents passed as arguments so the callee can
      // bounds-check its parameter accesses.
      for (unsigned i = 0; i < call->getNumArgs(); ++i) {
        std::string an = castedVarName(call->getArg(i));
        long long ext = -1;
        ExtentOrigin origin = ExtentOrigin::Unknown;
        auto hit = heapAllocs_.find(an);
        if (!an.empty() && hit != heapAllocs_.end()) {
          ext = hit->second.bytes;
          origin = hit->second.origin == ExtentOrigin::Unknown
                       ? ExtentOrigin::Heap
                       : hit->second.origin;
        }
        auto sit = stackArrayExtents_.find(an);
        if (!an.empty() && sit != stackArrayExtents_.end()) {
          ext = sit->second;
          origin = ExtentOrigin::Stack;
        }
        if (ext < 0)
          continue;
        // Keep this call site's pointer/count pairing instead of collapsing
        // differing extents into a single "unknown" marker.  Mirrors
        // Checked C's ConcretizeFromFunctionTypeWithArgs: bounds are
        // concretized per call, and only a site whose declared count exceeds
        // its actual extent is a genuine conflict.
        {
          CallSiteExtentFact f;
          f.ptrSlot = i;
          f.extentBytes = ext;
          unsigned long long es = pointeeSizeOfArg(call->getArg(i));
          unsigned cslot = 0;
          long long cnt = -1;
          if (es && companionCountArg(call, i, cslot, cnt)) {
            f.countSlot = cslot;
            f.count = cnt;
            f.exceeds = (unsigned long long)cnt * es > (unsigned long long)ext;
            if (f.exceeds)
              conflictedParams_.insert({name, i});
          } else {
            // A site with a known extent but no provable companion count
            // disables pairing for this slot (sentinel count slot).
            f.countSlot = CallSiteExtentFact::kNoCountSlot;
          }
          callSiteFacts_[name].insert(f);
        }
        auto &slot = callParamExtents_[name][i];
        // A second, different extent makes the parameter unverifiable:
        // keep the "unknown" marker instead of last-writer-wins.
        if (slot.bytes >= 0 && slot.bytes != ext) {
          conflictedParams_.insert({name, i});
          slot.bytes = -1;
        } else if (slot.bytes < 0 || !conflictedParams_.count({name, i})) {
          slot.bytes = ext;
          slot.origin = origin;
        }
      }
      // Concretize extents that reach the callee through an argument *slot*
      // instead of a variable name (`&p` into `T **pp`, or `&s` whose pointer
      // field holds a sized object).  This is the call-side counterpart of
      // Checked C's Sema::ConcretizeFromFunctionTypeWithArgs and Apple's
      // pointer/count pairing keyed on the member expression; the callee side
      // anchors on the parameter slot and expression shape, so casts and
      // aliases at the use site cannot erase the extent.
      auto namedExtent = [&](const std::string &n, ExtentOrigin &origin) {
        if (n.empty())
          return -1LL;
        auto hit = heapAllocs_.find(n);
        if (hit != heapAllocs_.end() && hit->second.decl &&
            hit->second.bytes > 0) {
          origin = hit->second.origin == ExtentOrigin::Unknown
                       ? ExtentOrigin::Heap
                       : hit->second.origin;
          return hit->second.bytes;
        }
        auto sit = stackArrayExtents_.find(n);
        if (sit != stackArrayExtents_.end() && sit->second > 0) {
          origin = ExtentOrigin::Stack;
          return sit->second;
        }
        return -1LL;
      };
      auto mergeSlot = [](SlotExtent &slot, long long ext, ExtentOrigin origin) {
        if (slot.bytes >= 0 && slot.bytes != ext) {
          slot.bytes = -1; // different call sites: no single valid extent
          return;
        }
        if (slot.bytes < 0) {
          slot.bytes = ext;
          slot.origin = origin;
        }
      };
      for (unsigned i = 0; i < call->getNumArgs(); ++i) {
        const Expr *arg = call->getArg(i)->IgnoreParenImpCasts();
        while (const auto *ce = dyn_cast<ExplicitCastExpr>(arg))
          arg = ce->getSubExpr()->IgnoreParenImpCasts();
        const auto *uo = dyn_cast<UnaryOperator>(arg);
        if (!uo || uo->getOpcode() != UO_AddrOf)
          continue;
        const Expr *pointee = uo->getSubExpr()->IgnoreParenImpCasts();
        ExtentOrigin origin = ExtentOrigin::Unknown;
        long long ext = namedExtent(castedVarName(pointee), origin);
        if (ext > 0) {
          mergeSlot(indirectSlotExtents_[{name, i}], ext, origin);
          continue;
        }
        // `&s`: pair each pointer field of s whose stored value is a sized
        // object (tracked via fieldAliases_) with that object's extent.
        const auto *ref = dyn_cast<DeclRefExpr>(pointee);
        const auto *svd = ref ? dyn_cast<VarDecl>(ref->getDecl()) : nullptr;
        if (!svd)
          continue;
        const auto *rd = svd->getType()->getAsRecordDecl();
        if (!rd)
          continue;
        if (const RecordDecl *def = rd->getDefinition())
          rd = def;
        for (const FieldDecl *fd : rd->fields()) {
          if (!fd->getType()->isPointerType())
            continue;
          std::string fk =
              svd->getNameAsString() + "." + fd->getNameAsString();
          auto fit = fieldAliases_.find(fk);
          if (fit == fieldAliases_.end())
            continue;
          for (const auto &alias : fit->second) {
            ExtentOrigin fo = ExtentOrigin::Unknown;
            long long fext = namedExtent(alias, fo);
            if (fext > 0)
              mergeSlot(fieldSlotExtents_[{name, i}][fd], fext, fo);
          }
        }
      }
      // Record the extent of a call that returns one of its pointer arguments
      // or a fixed global array, so chained calls retain bounds metadata.
      auto rp = returnedParam_.find(name);
      if (rp != returnedParam_.end() && rp->second < call->getNumArgs()) {
        const Expr *arg = call->getArg(rp->second)->IgnoreParenCasts();
        auto hit = heapAllocs_.find(varNameOf(arg));
        auto sit = stackArrayExtents_.find(varNameOf(arg));
        auto ait = callReturnExtent_.find(
            arg->getBeginLoc().getRawEncoding());
        long long ext = -1;
        if (hit != heapAllocs_.end()) ext = hit->second.bytes;
        else if (sit != stackArrayExtents_.end()) ext = sit->second;
        else if (ait != callReturnExtent_.end()) ext = ait->second;
        if (ext > 0)
          callReturnExtent_[call->getBeginLoc().getRawEncoding()] = ext;
      }
      auto rf = returnedFixedExtent_.find(name);
      if (rf != returnedFixedExtent_.end() && rf->second > 0)
        callReturnExtent_[call->getBeginLoc().getRawEncoding()] = rf->second;
      auto ra = returnedAllocExtent_.find(name);
      if (ra != returnedAllocExtent_.end() && ra->second > 0)
        callReturnExtent_[call->getBeginLoc().getRawEncoding()] = ra->second;
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
        if (!fkey.empty()) {
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
        // Issue #180: the clang::CFG forward pass proves whether another
        // free of this owner can reach this site.  A free on a mutually
        // exclusive arm (typically followed by `goto cleanup`) is not a
        // double free; the linear may-release heuristic cannot see that.
        const unsigned freeSite = call->getBeginLoc().getRawEncoding();
        // A single CFG free site proves no exclusivity: it must not hide a
        // release established by an interprocedural summary (`rel(a);
        // free(a);` where rel frees its parameter). Only the
        // mutually-exclusive-arms pattern has two distinct sites.
        auto eit = cfgExclusiveFreeSites_.find(pname);
        bool exclusiveFree = eit != cfgExclusiveFreeSites_.end() &&
                             eit->second.size() > 1 &&
                             eit->second.count(freeSite) != 0;
        // A branch arm may have freed p and then set p = NULL; the later
        // free is free(NULL) on that path, not a double free.
        bool maybeNullOnBranch = branchNullifiedVars_.count(pname) != 0;
        if (isAllocatorFree && releasedVars_.count(pname) &&
            !exclusiveFree && !maybeNullOnBranch)
          report(call->getBeginLoc(), "double free of '" + pname + "'");
        else if (isAllocatorFree && !exclusiveFree && !maybeNullOnBranch &&
                 maybeReleasedVars_.count(pname) &&
                 !releasedVars_.count(pname) && [&] {
                   auto lit = maybeReleaseLines_.find(pname);
                   unsigned here = call->getBeginLoc().getRawEncoding();
                   return lit != maybeReleaseLines_.end() && here &&
                          lit->second && lit->second < here;
                 }())
          report(call->getBeginLoc(),
                 "double free of '" + pname + "' on some branch path");
        else {
          std::string fkey = lvalueKey(call->getArg(0));
          if (!fkey.empty())
            releasedFields_.insert(fkey);
          releaseLocs_[pname] = call->getBeginLoc();
          markReleased(pname);
        }
        // A nullification remembered from an earlier branch is consumed by
        // this release; keeping it would suppress a genuine second free.
        branchNullifiedVars_.erase(pname);
      }
    // Constant arguments to an inferred arithmetic helper (e.g.
    // `safe_add(INT_MAX, 1)`) are checked at the call site.
    if (const FunctionDecl *callee = call->getDirectCallee()) {
      auto eit = arithEffects_.find(callee->getNameAsString());
      if (eit != arithEffects_.end()) {
        const ArithEffect &eff = eit->second;
        if (call->getNumArgs() > static_cast<unsigned>(std::max(eff.lhs, eff.rhs))) {
          auto argLL = [&](unsigned idx, long long &out) -> bool {
            llvm::APSInt v;
            if (idx >= call->getNumArgs() ||
                !knownConst(call->getArg(idx), v))
              return false;
            out = call->getArg(idx)->getType()->isSignedIntegerType()
                      ? v.getSExtValue()
                      : (v.isNegative() ? v.getSExtValue() : v.getZExtValue());
            return true;
          };
          long long a = 0, b = 0;
          bool overflow = false;
          if (argLL(eff.lhs, a) && argLL(eff.rhs, b) &&
              arithmeticOverflow(eff.width, eff.op, a, b, overflow) &&
              overflow) {
            std::string msg = "signed integer overflow in call to '" +
                              callee->getNameAsString() + "'";
            if (strict_) report(call->getBeginLoc(), msg);
            else warnAt(call->getBeginLoc(), msg);
          }
        }
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
      // Enforce the precise v2 argument-index metadata.  Previously only the
      // legacy `nullableArg` scalar was consumed, so freeEffects /
      // writeEffects / ownershipTransfer were parsed but had no effect (S11).
      bool isAllocatorFreeApi =
          baseName == "free" || baseName == "cplus_release" ||
          isReallocName(baseName);
      if (strict_) {
        for (int idx : contract.freeArgs) {
          if (isAllocatorFreeApi || idx < 0 || (unsigned)idx >= call->getNumArgs())
            continue; // free/realloc already have their own state machine
          std::string an = castedVarName(call->getArg(idx));
          if (an.empty())
            continue;
          if (isReleased(call->getArg(idx)) || releasedVars_.count(an))
            report(call->getBeginLoc(), "double free: '" + baseName +
                                            "' frees argument " +
                                            std::to_string(idx));
          else
            markReleased(an);
        }
        for (int idx : contract.ownershipTransferArgs) {
          // A realloc-style transfer is "success-only": the old pointer stays
          // LIVE on failure (`if (!q) { free(p); return; }`) and becomes stale
          // only after a successful rebind (`p = q`).  Releasing it here at
          // the call site produced false UAFs / double frees.  The branch-aware
          // realloc state machine (detectReallocSuccess / TraverseIfStmt)
          // handles it correctly, including indirect calls via fn pointers.
          if (isAllocatorFreeApi || idx < 0 ||
              (unsigned)idx >= call->getNumArgs())
            continue;
          std::string an = castedVarName(call->getArg(idx));
          if (!an.empty())
            markReleased(an); // caller relinquishes the owned pointer
        }
        for (int idx : contract.writeArgs)
          if (idx >= 0 && (unsigned)idx < call->getNumArgs()) {
            const Expr *a = call->getArg(idx)->IgnoreParenCasts();
            if (const auto *ref = dyn_cast<DeclRefExpr>(a))
              if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl()))
                heapWritten_.insert(vd);
          }
      }
      // Keep the legacy nullableArg policy (all pointer args) unchanged; the
      // precise nullableArgIndices mean "accepts NULL" and must not be
      // reinterpreted as a violation.
      if (contract.nullableArg && !contract.frees)
        for (unsigned i = 0; i < call->getNumArgs(); ++i)
          if (isNullConstant(call->getArg(i)->IgnoreParenCasts()) ||
              nullVarOf(call->getArg(i))) {
            report(call->getBeginLoc(),
                   "null pointer argument " + std::to_string(i) + " passed to " +
                       baseName + " (" + contract.category + ")");
            break;
          }
      bool reallocFromNull = isReallocName(baseName) && call->getNumArgs() >= 1 &&
                             isNullConstant(call->getArg(0)->IgnoreParenCasts());
      std::string reallocResult;
      for (const auto &kv : reallocAssignCalls_)
        if (kv.second == call) {
          reallocResult = kv.first->getNameAsString();
          break;
        }
      if (isReallocName(baseName) && !reallocFromNull &&
          handledReallocs_.count(call->getBeginLoc().getRawEncoding()) == 0 &&
          !handledReallocVars_.count(reallocResult)) {
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
           isReallocName(baseName)) && call->getNumArgs() >= 1) {
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
        unsigned sizeIdx = isReallocName(baseName) ? 1 : 0;
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
           isReallocName(baseName)) && call->getNumArgs() >= 1) {
        unsigned sizeIdx = isReallocName(baseName) ? 1 : 0;
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
        propagatePointerBytes(call);
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
      // transfers that obligation to the call site.  Resolved function-pointer
      // and callback targets share the same summary (S2/S15/S21).
      for (const auto &target : calleeNames(call, callee)) {
        auto uit = userFrees_.find(target);
        if (uit == userFrees_.end() || uit->second.empty())
          continue;
        for (int idx : uit->second) {
          if (idx < 0 || (unsigned)idx >= call->getNumArgs())
            continue;
          // castedVarName looks through `(int *)(void *)p` so a free-relay
          // reached through a laundering cast is still a double free (s18).
          std::string an = castedVarName(call->getArg(idx));
          if (an.empty())
            continue;
          if (releasedVars_.count(an))
            report(call->getBeginLoc(), "double free after call to '" + target +
                                            "' which frees its argument");
          else
            markReleased(an);
        }
      }
      // Pointer-to-pointer release (`sneak_free(&p)` where the callee does
      // `free(*pp)`): the caller's pointee, not the address expression,
      // becomes dangling. Keep the same double-free policy as direct relays.
      for (const auto &target : calleeNames(call, callee)) {
        auto pit = userFreesPointee_.find(target);
        if (pit == userFreesPointee_.end() || pit->second.empty())
          continue;
        for (int idx : pit->second) {
          if (idx < 0 || (unsigned)idx >= call->getNumArgs())
            continue;
          const Expr *arg = call->getArg((unsigned)idx)->IgnoreParenImpCasts();
          const auto *addr = dyn_cast<UnaryOperator>(arg);
          if (!addr || addr->getOpcode() != UO_AddrOf)
            continue;
          const auto *ref = dyn_cast<DeclRefExpr>(
              addr->getSubExpr()->IgnoreParenImpCasts());
          if (!ref)
            continue;
          const auto *vd = dyn_cast<VarDecl>(ref->getDecl());
          if (!vd)
            continue;
          const std::string pn = vd->getNameAsString();
          if (pn.empty())
            continue;
          if (releasedVars_.count(pn) || isReleased(addr->getSubExpr()))
            report(call->getBeginLoc(),
                   "double free after call to '" + target +
                       "' which frees the pointee of its argument");
          else
            markReleased(pn);
        }
      }
      // Out-parameter summaries: `make(&q)` where make does `*out = malloc(N)`
      // or `*out = p` gives q the allocation extent / release state of p
      // (S3/S9/S17).
      for (const auto &target : calleeNames(call, callee)) {
        auto oit = returnedOutParam_.find(target);
        if (oit != returnedOutParam_.end())
          for (const auto &kv : oit->second) {
            if (kv.first >= call->getNumArgs() || kv.second >= call->getNumArgs())
              continue;
            std::string dst = varNameOfAddressArg(call->getArg(kv.first));
            std::string src = castedVarName(call->getArg(kv.second));
            if (dst.empty() || src.empty() || dst == src)
              continue;
            linkAlias(dst, src);
            if (releasedVars_.count(src))
              markReleased(dst);
          }
        auto ait = returnedOutAlloc_.find(target);
        if (ait != returnedOutAlloc_.end())
          for (const auto &kv : ait->second) {
            if (kv.first >= call->getNumArgs() || kv.second <= 0)
              continue;
            std::string dst = varNameOfAddressArg(call->getArg(kv.first));
            if (dst.empty())
              continue;
            HeapAllocState st;
            st.bytes = kv.second;
            st.origin = ExtentOrigin::Heap;
            heapAllocs_[dst] = st;
          }
      }
    }
    if (!report_ && (name.find("lock") != std::string::npos ||
                     name.find("mutex") != std::string::npos))
      tuHasLockCalls_ = true;
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
      // Only a literal `&local` is a stack object.  A local POINTER VARIABLE
      // holding a heap address is not a stack borrow; flagging it broke
      // valid pthread programs.
      const bool literalAddressOfLocal =
          isa<UnaryOperator>(arg) &&
          cast<UnaryOperator>(arg)->getOpcode() == UO_AddrOf;
      if (const auto *ref = dyn_cast<DeclRefExpr>(borrowed)) {
        if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl())) {
          if (vd->hasLocalStorage() && literalAddressOfLocal)
            warnAt(call->getBeginLoc(),
                   "stack borrow passed to thread: '" +
                       vd->getNameAsString() + "'");
          // Two threads receiving the same shared heap object without any
          // lock in the translation unit is a real data race. This checker
          // is deliberately conservative: a lock anywhere suppresses it.
          std::string argName = vd->getNameAsString();
          if (name == "pthread_create" && !report_ &&
              !literalAddressOfLocal) {
            ++threadSharedArgCounts_[argName];
            threadSharedArgLocs_[argName] = call->getBeginLoc();
          } else if (name == "pthread_create" && report_ &&
                     !literalAddressOfLocal &&
                     threadSharedArgCounts_[argName] >= 2 &&
                     !tuHasLockCalls_) {
            reportNamed(call->getBeginLoc(), "CPLUS-BORROW",
                        "unsynchronized shared state passed to multiple "
                        "threads: '" + argName + "'",
                        "suggested correction: guard the shared object with "
                        "a mutex or give each thread its own object",
                        true);
          }
        }
      }
    }
    return true;
  }

  bool VisitArraySubscriptExpr(ArraySubscriptExpr *subscript) {
    if (!report_)
      return true;
    // A runtime index into an array whose constant-index element was already
    // released may select the stale slot.  Fail closed in STRICT, warn in
    // DEFAULT; this is deliberately conservative (SoftBoundCETS/Clang SA
    // would use symbolic element state, which is not yet modelled).
    long long probe = 0;
    std::string baseName = varNameOf(subscript->getBase());
    if (!baseName.empty() && !evalIndexValue(subscript->getIdx(), probe)) {
      for (const auto &key : releasedFields_) {
        if (key.rfind(baseName + "[", 0) == 0) {
          std::string msg =
              "possible use-after-free: element of '" + baseName +
              "' was released and the index is not statically known";
          if (strict_)
            report(subscript->getBeginLoc(), msg);
          else
            warnAt(subscript->getBeginLoc(), msg);
          break;
        }
      }
    }
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
    // A conditional index is safe only when every possibly-selected constant
    // arm is in bounds; STRICT fails closed on a runtime condition with an
    // out-of-bounds constant arm (including nested conditionals).
    checkConditionalIndexArms(subscript);
    // A subscript through a pointer parameter with no proven extent is not
    // safe by default. STRICT rejects it; DEFAULT warns. One-past pointer
    // formation remains legal because this only checks actual subscripts.
    const Expr *subBase = subscript->getBase()->IgnoreParenCasts();
    if (const auto *pref = dyn_cast<DeclRefExpr>(subBase))
      if (const auto *pvd = dyn_cast<ParmVarDecl>(pref->getDecl()))
        if (pvd->getType()->isPointerType() && !heapStateOf(subBase) &&
            !hasProvenParamExtent(pvd, subscript)) {
          std::string msg = "pointer parameter '" +
                            pvd->getNameAsString() +
                            "' has unverifiable extent; subscript cannot be "
                            "proven safe";
          if (strict_) report(subscript->getBeginLoc(), msg);
          else warnAt(subscript->getBeginLoc(), msg);
        }
    // An opaque handle from an unresolved external call has no proven
    // extent either. Stack/heap allocations, returned aliases and known
    // factories are already tracked above and stay accepted.
    if (const auto *pref = dyn_cast<DeclRefExpr>(subBase))
      if (const auto *pvd = dyn_cast<VarDecl>(pref->getDecl()))
        if (pvd->getType()->isPointerType() && pvd->getInit() &&
            !heapStateOf(subBase) &&
            !stackArrayExtents_.count(pvd->getNameAsString()) &&
            !aliasGroups_.count(pvd->getNameAsString())) {
          const Expr *init = pvd->getInit()->IgnoreParenImpCasts();
          if (const auto *call = dyn_cast<CallExpr>(init)) {
            bool proven = false;
            for (const auto &t : calleeNames(call, call->getDirectCallee()))
              if (isKnownRuntimeCallee(t) || returnedParam_.count(t) ||
                  returnedAllocExtent_.count(t) || returnedFixedExtent_.count(t) ||
                  constReturnValues_.count(t))
                proven = true;
            if (callReturnExtent_.count(call->getBeginLoc().getRawEncoding()))
              proven = true;
            if (!proven && strict_)
              report(subscript->getBeginLoc(),
                     "pointer '" + pvd->getNameAsString() +
                         "' from an unresolved call has unverifiable extent; "
                         "subscript cannot be proven safe");
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
          if (!evalIndexValue(subscript->getIdx(), idx)) {
            // A helper returning a constant (`int idx(void){return 9;}`) is a
            // compile-time index for bounds purposes (S9-style summary).
            const Expr *ie = subscript->getIdx()->IgnoreParenImpCasts();
            if (const auto *ic = dyn_cast_or_null<CallExpr>(ie)) {
              long long cv = 0;
              for (const auto &t : calleeNames(ic, ic->getDirectCallee())) {
                auto cit = constReturnValues_.find(t);
                if (cit != constReturnValues_.end()) {
                  cv = cit->second;
                  idx = cv;
                  break;
                }
              }
            }
          }
          if (idx < 0)
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
                   "flexible array index " + std::to_string(idx) + " exceeds " +
                       extentLabel(hs->origin) + " of " +
                       std::to_string(hs->bytes) + " bytes (including "
                       "struct prefix)");
          return true;
        }
      }
    }
    // Struct downcast: a pointer to a smaller allocation is cast to a larger
    // struct and a non-FAM field is indexed. FAMs are handled above.
    if (const auto *me = dyn_cast<MemberExpr>(
            subscript->getBase()->IgnoreParenImpCasts())) {
      const auto *fd = dyn_cast<FieldDecl>(me->getMemberDecl());
      if (fd && !ctx_.getAsIncompleteArrayType(fd->getType())) {
        const auto *ref = dyn_cast<DeclRefExpr>(
            me->getBase()->IgnoreParenImpCasts());
        const HeapAllocState *hs = ref ? heapStateOf(ref) : nullptr;
        long long idx = 0;
        if (hs && hs->bytes > 0 && evalIndexValue(subscript->getIdx(), idx) &&
            idx >= 0) {
          QualType ft = fd->getType();
          const auto *cat = ctx_.getAsConstantArrayType(ft);
          QualType elem = cat ? cat->getElementType() : ft;
          unsigned long long es = ctx_.getTypeSize(elem) / 8u;
          unsigned long long field =
              (unsigned long long)(ctx_.getFieldOffset(fd) / 8u);
          if (es && field + (unsigned long long)idx * es >=
                        (unsigned long long)hs->bytes)
            report(subscript->getBeginLoc(),
                   "field index " + std::to_string(idx) + " exceeds heap "
                   "allocation '" + varNameOf(ref) + "' of " +
                   std::to_string(hs->bytes) + " bytes");
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
      // A loop induction value is worst-case, not a source-level constant.
      // Name it as an out-of-bounds access rather than a literal index so the
      // diagnostic stays accurate for `a[i++]` inside a counted while loop.
      if (indexMentionsLoopOverride(subscript->getIdx())) {
        report(subscript->getBeginLoc(),
               "out-of-bounds access: index " + std::to_string(value) +
                   " reaches past array '" + name + "' of length " +
                   std::to_string(extent));
        return true;
      }
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
  bool recordArithmeticSites_ = false;
  std::vector<CheckedArithmeticSite> arithmeticSites_;
  // Size expressions already emitted as checked allocation extents; prevents
  // a duplicate Op::Mul guard on the same `n * sizeof(T)` expression.
  std::set<unsigned> allocExtentExprLocs_;
  std::vector<CheckedPointerSite> pointerSites_;
  std::vector<CheckedAllocSite> allocSites_;
  std::vector<CheckedAllocSite> freeSites_;
  std::set<unsigned> handledReallocs_;
  std::map<std::string, SourceLocation> allocLocs_;
  std::map<std::string, SourceLocation> aliasLocs_;
  std::map<std::string, SourceLocation> releaseLocs_;
  std::set<const VarDecl *> stringPtrs_;
  std::map<const VarDecl *, unsigned long long> compoundLiteralVars_;
  std::set<const VarDecl *> nonZeroVars_;
  // Variables whose guard-derived non-zero / lower-bound proof was invalidated
  // by a definite assignment later in the same traversal.
  std::set<const VarDecl *> invalidatedNonZero_;
  std::map<const VarDecl *, long long> loopLowerBound_;
  std::set<std::string> overflowingConstVars_;
  std::map<std::string, std::string> stackAliasOwner_;
  std::map<std::string, std::string> aliasWriter_;
  std::map<std::string, ArithEffect> arithEffects_;
  // Functions whose every return is the same integer constant, so a size
  // argument like `malloc(sz())` still has a provable byte count (S9).
  std::map<std::string, long long> constReturnValues_;
  // --- Interprocedural effect fixpoint state (collection pass) -------------
  // Local name -> parameter slot of the current function, so a free/deref on
  // `int *q = p;` is attributed to p (one-level `free(p)` only missed this).
  std::map<std::string, int> localParamAliases_;
  // Function-pointer variable / struct-field name -> resolved callee name.
  std::map<std::string, std::string> fnPtrVarTargets_;
  // Callee name -> function-pointer parameter slot -> possible target names
  // (callbacks passed from call sites, e.g. callit(consume, p)).
  std::map<std::string, std::map<unsigned, std::set<std::string>>>
      fnPtrParamTargets_;
  // Function -> (`T **` out-param slot -> source param slot) for `*out = p`.
  std::map<std::string, std::map<unsigned, unsigned>> returnedOutParam_;
  // Function -> (`T **` out-param slot -> allocation bytes) for
  // `*out = malloc(N)`.
  std::map<std::string, std::map<unsigned, long long>> returnedOutAlloc_;
  // Union-typed variables: all field views alias the same storage, so
  // `free(u.p); return *u.i;` is a UAF even though the field names differ.
  std::set<std::string> unionVars_;
  // Compile-time-only breaker facts (function-scoped, no runtime effect):
  // union variables whose integer member was forged from a plain constant,
  // locals copied from a forged union pointer member, and local structs whose
  // aggregate initializer holds the address of a local.
  std::set<std::string> forgedUnionVars_;
  std::set<std::string> forgedPointerVars_;
  std::set<std::string> structLocalAddrVars_;
  // Global pointer -> names it may point at, and per-function records of a
  // global assigned from a parameter (`void store(int *p){ g = p; }`).
  std::map<std::string, std::set<std::string>> globalPointsTo_;
  std::map<std::string, std::map<unsigned, std::set<std::string>>>
      globalParamStores_;
  std::map<std::string, HeapAllocState> heapAllocs_;
  std::set<const VarDecl *> heapWritten_;
  std::set<const VarDecl *> heapInterior_;
  std::set<const VarDecl *> onePastEnd_;
  // Pointers formed from a fixed array with a runtime (unproven) offset.
  std::set<const VarDecl *> unprovenPtrOffset_;
  std::set<const VarDecl *> arrayDerivedPtr_;
  std::map<const VarDecl *, std::string> strlenSized_;
  std::map<const VarDecl *, unsigned long long> knownStrLen_;
  std::map<const VarDecl *, long long> loopIndexOverride_;
  std::map<std::string, llvm::APSInt> constLocals_;
  // Local floating constants with a provable initializer, used for strict
  // float-to-integer range checks on `double d = 1e30; f(d)` style sources.
  std::map<std::string, double> constFloatLocals_;
  std::set<std::string> addressTakenVars_;
  std::set<std::string> assignedVars_;
  // Locals whose initializer constant is no longer provable at a compound
  // update because they were also assigned a plain/runtime value.
  std::set<std::string> nonConstantAssignedVars_;
  std::set<std::string> functionsReturningFreed_;
  std::set<std::string> nullReturningFunctions_;
  std::set<std::pair<std::string, unsigned>> derefParams_;
  /// Parameter slots the callee WRITES through (not merely reads). Used for
  /// Rust-style exclusive mutable borrow checks at call sites.
  std::set<std::pair<std::string, unsigned>> paramWriteSlots_;
  std::set<std::string> maybeReleasedVars_;
  // Names assigned NULL on at least one arm of a branch. A later free of such
  // a name can be free(NULL) on that path, so a branch-local release must not
  // be reported as a definite/possible double free at the merge point.
  std::set<std::string> branchNullifiedVars_;
  std::map<std::string, unsigned> maybeReleaseLines_;
  std::set<std::string> guardedFromConst_;
  std::string nonNullBranch_;
  // Lock discipline: mutexes currently held in the traversed function and
  // the nesting orders observed across functions (outer -> inner). The latter
  // catches opposite acquisition orders, a classic lock-order deadlock.
  std::map<std::string, unsigned> heldLocks_;
  std::map<std::pair<std::string, std::string>, std::string> lockOrders_;
  std::set<std::string> provenanceInts_;
  // Interprocedural allocation extents: callee name -> param index -> bytes,
  // gathered from call sites whose argument is a tracked allocation.
  std::map<std::string, std::map<unsigned, ParamExtent>> callParamExtents_;
  // Per-call-site pointer/count pairing facts (see CallSiteExtentFact).
  CallSiteFacts callSiteFacts_;
  CountPairedParams countPairedParams_;
  // Loop induction variable -> companion count parameter slot, while the
  // loop body is traversed.  Used to prove `array[i]` is bounded by `length`.
  std::map<const VarDecl *, unsigned> loopCountBound_;
  // (base parameter, index variable) pairs of active NUL-terminator
  // traversals (`while (s[i] != '\0')` / `for (...; s[i]; ...)`), only for
  // read-only `const char *` parameters.
  std::set<std::pair<const VarDecl *, const VarDecl *>> nulTraversal_;
  // Argument-slot extents (function name, parameter slot).  These are keyed
  // by the callee slot, not by variable spelling, so an extent survives
  // casts and aliases at the use site.  See SlotExtent above.
  IndirectSlotExtents indirectSlotExtents_; // `T **` param <- `&sizedPointer`
  FieldSlotExtents fieldSlotExtents_;       // struct pointer field <- extent
  // A callee parameter passed buffers of different sizes at different call
  // sites has no single valid extent; do not trust any of them.
  std::set<std::pair<std::string, unsigned>> conflictedParams_;
  /// Function name -> parameter index returned unchanged by `return p;`.
  std::map<std::string, unsigned> returnedParam_;
  /// Function name -> fixed extent returned from a static/global array.
  std::map<std::string, long long> returnedFixedExtent_;
  std::map<std::string, long long> returnedAllocExtent_;
  /// Source location (raw encoding) of a call expression -> returned extent.
  std::map<unsigned, long long> callReturnExtent_;
  std::map<std::string, long long> stackArrayExtents_;
  std::set<std::string> allGlobals_;
  std::map<std::string, int> threadEntryCounts_;
  std::map<std::string, SourceLocation> threadEntryLocs_;
  std::map<std::string, int> threadSharedArgCounts_;
  std::map<std::string, SourceLocation> threadSharedArgLocs_;
  bool tuHasLockCalls_ = false;
  const FunctionDecl *currentFnDecl_ = nullptr;
  std::string currentFunction_;

public:
  std::set<std::string> handlers_;
  std::set<std::string> nonVolatileGlobals_;
  std::set<std::string> nullVars_;
  std::set<std::string> releasedVars_;
  std::set<std::string> releasedFields_;
  std::map<std::string, std::set<std::string>> fieldAliases_;
  // Identity-keyed allocation/extent facts for member lvalues, conditional
  // aliases and integer round-trips. Names are deliberately never used here.
  std::map<std::string, HeapAllocState> identityAllocs_;
  // Only a reverse index used to resolve an already-recorded source spelling
  // back to the declaration that owns the provenance fact.
  std::map<std::string, const VarDecl *> namedVars_;
  // Integer laundering: provenance integer variable -> pointer variable it
  // was derived from, so `(int *)u` inherits p's release state (provenance).
  std::map<std::string, std::string> provenanceSource_;
  std::map<std::string, std::string> formatFnAliases_;
  std::map<std::string, std::string> snprintfResults_;
  std::map<std::string, std::string> freadResults_;
  std::set<std::string> contractIssuesReported_;
  // Call sites already diagnosed with the F2 stale-alias error/warning.
  std::set<unsigned> staleIntegerAliasReported_;
  std::set<std::string> guardedSnprintf_;
  std::string reallocRebindOwner_, reallocRebindTemp_;
  // Locals holding a realloc/cplus_realloc result in this function. Used to
  // transfer that result's extent through a plain `p = q` alias, which is not
  // nested in an `if (!q)` success branch and so is invisible to
  // detectReallocSuccess(). Ordinary pointer assignments never create extents.
  std::set<std::string> reallocResultVars_;
  std::map<std::string, std::set<std::string>> aliasGroups_;
  int conditionalDepth_ = 0;
  // Issue #180: free() source locations the CFG proves cannot be a double
  // free (all other frees of the same owner are on mutually exclusive paths).
  // Guards the linear visitor's may-release heuristic; never suppresses a
  // definite release-state double free.
  // Issue #180/#223: free sites proven mutually exclusive by the CFG pass,
  // keyed by owner. A single site proves no exclusivity and must not hide a
  // release established by an interprocedural summary.
  std::map<std::string, std::set<unsigned>> cfgExclusiveFreeSites_;
  std::map<std::string, std::set<int>> userFrees_;
  // Realloc results assigned to an already-declared variable (`q = realloc(...)`).
  std::map<const VarDecl *, const CallExpr *> reallocAssignCalls_;
  std::set<std::string> handledReallocVars_;
  // Callees that release the POINTEE of a pointer parameter (`free(*pp)`).
  // The caller passes `&p`, so ownership of p must be transferred at the
  // call site even though the callee never names p.
  std::map<std::string, std::set<int>> userFreesPointee_;
  // Function -> function names it returns as a function pointer
  // (`void (*get(void))(void *) { return free; }`).
  std::map<std::string, std::set<std::string>> returnedFnPtr_;
  // Function -> globals it frees directly (`void freeg(void){ free(g); }`).
  std::map<std::string, std::set<std::string>> freedGlobals_;
  // Functions registered with atexit; their global frees happen at exit.
  std::set<std::string> atexitFunctions_;
  // Getter function -> globals it returns (`void *getg(void){ return g; }`).
  std::map<std::string, std::set<std::string>> globalGetterReturns_;
  // Allocation-identity points-to: a global slot -> allocation IDs it holds,
  // and allocation ID -> every variable name holding that same allocation.
  std::map<std::string, std::set<std::string>> globalAllocPointsTo_;
  std::map<std::string, std::set<std::string>> allocIdentityMembers_;
  std::map<std::string, std::string> varAllocIdentity_;

  std::map<std::string, std::string> borrowOwner_;
  // Issue #26: identity-based borrow -> (stack owner, source name) facts used
  // by the return escape check. Keyed by VarDecl, not by spelling, so
  // shadowing and casts cannot change which object a borrow refers to. The
  // name is kept separately because RAII lowering replaces a returned borrow
  // with a generated `__cplus_ret` temporary.
  std::map<const VarDecl *, std::pair<const VarDecl *, std::string>>
      returnBorrowOwner_;
  std::map<std::string, std::set<std::string>> writeBorrows_;
  std::set<std::string> movedVars_;
  std::set<std::string> destructors_;
};

// ---------------------------------------------------------------------------
// Issue #179: Cake-style flow-sensitive _Opt / _Owner / _View checking.
//
// This is a separate, additive pass. The existing SafetyVisitor keeps all of
// its historical diagnostics unchanged; this visitor only fires on variables
// the rewriter explicitly tagged with a Cake qualifier. That keeps the new
// rejections deterministic and free of false positives for untagged C code.
//
// State is flow-sensitive in the same sense as Cake: branch refinement is
// scoped to the arm that proves the pointer non-null, and a guard whose other
// arm exits the function refines the state after the statement. Ownership
// reuse (moved/released/consumed) is keyed by declaration name and fed by the
// shared effect summaries, so cplus_move and ownership-transferring calls
// behave consistently with the rest of the checker.
//
// Moved state is NOT a translation-unit-wide fact. The rewriter records each
// cplus_move site as an ORIGINAL source line; those lines are mapped to byte
// offsets in the lowered C buffer parsed by Clang. Moved state is seeded only
// when the walk reaches the site's offset, cleared by an assignment to the
// same name, intersected at if/else joins, and re-applied on the second pass
// over a loop body (Cake visits loops twice). This keeps mutually exclusive
// arms, move/reassign/move sequences and loop-carried moves precise.
// ---------------------------------------------------------------------------
namespace {

/// Byte offset in `code` where ORIGINAL source line `line` starts. Clang's
/// `#line` markers are not source lines and are skipped, so line N of the
/// lowered buffer maps to line N of the annotated file.
size_t cakeLineOffset(const std::string &code, unsigned line) {
  size_t pos = 0;
  unsigned current = 1;
  while (pos <= code.size() && current < line) {
    size_t nl = code.find('\n', pos);
    if (nl == std::string::npos)
      return code.size();
    size_t first = pos;
    while (first < nl && (code[first] == ' ' || code[first] == '\t'))
      ++first;
    if (code.compare(first, 6, "#line ") != 0)
      ++current;
    pos = nl + 1;
  }
  return pos;
}

/// Byte offset of the `nth` `cplus_move(name)` token inside [begin, end).
size_t cakeMoveOffset(const std::string &code, size_t begin, size_t end,
                      const std::string &name, unsigned nth) {
  const std::string token = "cplus_move";
  size_t pos = begin;
  unsigned seen = 0;
  auto skipSpace = [&](size_t &q) {
    while (q < end && (code[q] == ' ' || code[q] == '\t'))
      ++q;
  };
  while (pos < end) {
    size_t p = code.find(token, pos);
    if (p == std::string::npos || p >= end)
      break;
    size_t q = p + token.size();
    skipSpace(q);
    if (q < end && code[q] == '(') {
      ++q;
      skipSpace(q);
      if (code.compare(q, name.size(), name) == 0) {
        q += name.size();
        skipSpace(q);
        if (q < end && code[q] == ')' && seen++ == nth)
          return p;
      }
    }
    pos = p + token.size();
  }
  return std::string::npos;
}

} // namespace

class CakeFlowVisitor : public RecursiveASTVisitor<CakeFlowVisitor> {
public:
  CakeFlowVisitor(ASTContext &ctx, DiagnosticEngine &diags, std::string file,
                  const std::string &code, bool strict)
      : ctx_(ctx), diags_(diags), file_(std::move(file)), code_(code),
        strict_(strict) {}

  int qualifierFor(const VarDecl *vd) const { return qualifierOf(vd); }
  void noteOwnerName(const std::string &n) { currentOwners_.insert(n); }
  void noteViewName(const std::string &n) { localViews_.insert(n); }

  bool TraverseFunctionDecl(FunctionDecl *fd) {
    if (!fd || !fd->doesThisDeclarationHaveABody())
      return true;
    std::set<std::string> savedOwners = std::move(currentOwners_);
    std::set<std::string> savedViews = std::move(localViews_);
    std::set<std::string> savedMoved = std::move(moved_);
    std::set<std::string> savedReleased = std::move(released_);
    std::set<std::string> savedNonNull = std::move(nonNull_);
    std::set<std::string> savedTransferred = std::move(transferred_);
    auto savedOffsets = std::move(moveOffsets_);
    auto savedApplied = std::move(appliedSites_);
    currentOwners_.clear();
    localViews_.clear();
    moved_.clear();
    released_.clear();
    nonNull_.clear();
    transferred_.clear();
    moveOffsets_.clear();
    appliedSites_.clear();
    collectTransfers(fd->getBody());
    // cplus_move is a macro, so it has no CallExpr in the safety AST. The
    // rewriter records each move site by original source line. Discover the
    // qualified names first, then map their sites to byte offsets in the
    // lowered C buffer so the walk can seed moved state at the site itself.
    struct CakeDeclCollector : RecursiveASTVisitor<CakeDeclCollector> {
      CakeFlowVisitor &self;
      CakeDeclCollector(CakeFlowVisitor &s) : self(s) {}
      bool VisitVarDecl(VarDecl *vd) {
        const int q = self.qualifierFor(vd);
        if (q == 2)
          self.noteOwnerName(vd->getNameAsString());
        else if (q == 3)
          self.noteViewName(vd->getNameAsString());
        return true;
      }
    } collector(*this);
    collector.TraverseStmt(fd->getBody());
    seedMoveSites();
    bool r = RecursiveASTVisitor::TraverseFunctionDecl(fd);
    for (const std::string &n : currentOwners_)
      if (!moved_.count(n) && !released_.count(n) && !transferred_.count(n))
        reportAt(fd->getLocation(), "CPLUS-OWN",
                 "_Owner '" + n +
                     "' is leaked: it is neither released nor moved on every "
                     "path",
                 "release it exactly once or transfer it with cplus_move()");
    currentOwners_ = std::move(savedOwners);
    localViews_ = std::move(savedViews);
    moved_ = std::move(savedMoved);
    released_ = std::move(savedReleased);
    nonNull_ = std::move(savedNonNull);
    transferred_ = std::move(savedTransferred);
    moveOffsets_ = std::move(savedOffsets);
    appliedSites_ = std::move(savedApplied);
    return r;
  }

  /// Apply the recorded move sites whose byte offsets fall inside the source
  /// range of the statement just walked. Deferring to AFTER the statement
  /// means a use within the move expression itself is not mistaken for a
  /// use-after-move, while the next statement sees the seeded state.
  bool TraverseStmt(Stmt *s) {
    bool r = RecursiveASTVisitor::TraverseStmt(s);
    unsigned begin = 0, end = 0;
    if (stmtOffsetRange(s, begin, end))
      applyMovesInRange(begin, end, s->getBeginLoc());
    return r;
  }

  bool TraverseIfStmt(IfStmt *s) {
    if (!s)
      return true;
    std::string name;
    bool thenNonNull = false, elseNonNull = false;
    refineFromCond(s->getCond(), name, thenNonNull, elseNonNull);
    const bool tracked = !name.empty() && isQualified(name);
    const std::set<std::string> savedNonNull = nonNull_;
    bool r = TraverseStmt(s->getCond());

    // Fork the ownership state for each arm. appliedSites_ is intentionally
    // shared: a move site belongs to exactly one arm, so the other arm must
    // not re-apply it when its state is restored.
    const std::set<std::string> entryMoved = moved_;
    const std::set<std::string> entryReleased = released_;
    const std::set<std::string> entryTransferred = transferred_;

    moved_ = entryMoved;
    released_ = entryReleased;
    transferred_ = entryTransferred;
    if (tracked && thenNonNull)
      nonNull_.insert(name);
    else
      nonNull_.erase(name);
    r &= TraverseStmt(s->getThen());
    const std::set<std::string> thenMoved = moved_;
    const std::set<std::string> thenReleased = released_;
    const std::set<std::string> thenTransferred = transferred_;

    moved_ = entryMoved;
    released_ = entryReleased;
    transferred_ = entryTransferred;
    nonNull_ = savedNonNull;
    if (Stmt *other = s->getElse()) {
      if (tracked && elseNonNull)
        nonNull_.insert(name);
      else
        nonNull_.erase(name);
      r &= TraverseStmt(other);
    }
    const std::set<std::string> elseMoved = moved_;
    const std::set<std::string> elseReleased = released_;
    const std::set<std::string> elseTransferred = transferred_;

    // Join: a moved/released fact survives only when BOTH arms agree, so a
    // single move in one arm is never treated as an unconditional transfer.
    moved_ = intersect(thenMoved, elseMoved);
    released_ = intersect(thenReleased, elseReleased);
    transferred_ = intersect(thenTransferred, elseTransferred);
    nonNull_ = savedNonNull;
    // A guard whose OTHER arm definitely exits proves non-null after the if.
    if (tracked) {
      if (thenNonNull && s->getElse() && containsReturn(s->getElse()))
        nonNull_.insert(name);
      if (elseNonNull && containsReturn(s->getThen()))
        nonNull_.insert(name);
    }
    return r;
  }

  bool TraverseWhileStmt(WhileStmt *s) {
    return traverseLoop(nullptr, s->getCond(), s->getBody(), nullptr);
  }
  bool TraverseForStmt(ForStmt *s) {
    return traverseLoop(s->getInit(), s->getCond(), s->getBody(), s->getInc());
  }
  bool TraverseDoStmt(DoStmt *s) {
    return traverseLoop(nullptr, s->getCond(), s->getBody(), nullptr);
  }

  /// Cake visits a loop body twice: once to reach the iteration state and a
  /// second time with warnings enabled. Rewinding the move sites inside the
  /// body makes a move without an intervening reassignment a double move on
  /// the back edge; a reassignment in the body clears the state before the
  /// move is seen again.
  bool traverseLoop(Stmt *init, Stmt *cond, Stmt *body, Stmt *inc) {
    if (!body)
      return true;
    const std::set<std::string> entryMoved = moved_;
    const std::set<std::string> entryReleased = released_;
    const std::set<std::string> entryTransferred = transferred_;
    const auto entryApplied = appliedSites_;

    suppress_ = true;
    TraverseStmt(init);
    TraverseStmt(cond);
    TraverseStmt(body);
    TraverseStmt(inc);
    suppress_ = false;
    const std::set<std::string> iterationMoved = moved_;
    const std::set<std::string> iterationReleased = released_;
    const std::set<std::string> iterationTransferred = transferred_;

    unsigned bodyBegin = 0, bodyEnd = 0;
    const bool haveBody = stmtOffsetRange(body, bodyBegin, bodyEnd);
    for (auto it = appliedSites_.begin(); it != appliedSites_.end();) {
      if (!entryApplied.count(*it) && haveBody && it->second >= bodyBegin &&
          it->second <= bodyEnd)
        it = appliedSites_.erase(it);
      else
        ++it;
    }

    moved_ = iterationMoved;
    released_ = iterationReleased;
    transferred_ = iterationTransferred;
    TraverseStmt(init);
    TraverseStmt(cond);
    TraverseStmt(body);
    TraverseStmt(inc);

    // Zero iterations are possible: only definite state survives the exit,
    // while a transfer anywhere in the loop still clears the leak obligation.
    moved_ = intersect(moved_, entryMoved);
    released_ = setUnion(released_, entryReleased);
    transferred_ = setUnion(transferred_, entryTransferred);
    return true;
  }

  bool TraverseBinaryOperator(BinaryOperator *op) {
    if (!op || (op->getOpcode() != BO_LAnd && op->getOpcode() != BO_LOr))
      return RecursiveASTVisitor::TraverseBinaryOperator(op);
    // `p && *p` and `!p || *p` refine p only in the right operand.
    std::string name;
    bool refines = false;
    const Expr *lhs = op->getLHS()->IgnoreParenCasts();
    if (const auto *uo = dyn_cast<UnaryOperator>(lhs)) {
      if (uo->getOpcode() == UO_LNot) {
        name = nameOf(uo->getSubExpr());
        refines = op->getOpcode() == BO_LOr;
      }
    }
    if (name.empty()) {
      name = nameOf(lhs);
      refines = op->getOpcode() == BO_LAnd;
    }
    std::set<std::string> saved = nonNull_;
    if (!name.empty() && isQualified(name)) {
      if (refines)
        nonNull_.insert(name);
      else
        nonNull_.erase(name);
    }
    bool r = TraverseStmt(op->getLHS());
    r &= TraverseStmt(op->getRHS());
    nonNull_ = saved;
    return r;
  }

  bool VisitVarDecl(VarDecl *vd) {
    const int q = qualifierOf(vd);
    if (q == 0)
      return true;
    const std::string n = vd->getNameAsString();
    if (q == 1)
      optVars_.insert(n);
    else if (q == 3)
      viewVars_.insert(n);
    else if (q == 2) {
      ownerVars_.insert(n);
      // Parameters are borrowed by the caller; the callee is not required to
      // destroy them. Only locals create a leak obligation here.
      if (!isa<ParmVarDecl>(vd))
        currentOwners_.insert(n);
    }
    return true;
  }

  bool VisitBinaryOperator(BinaryOperator *op) {
    if (!op || !op->isAssignmentOp())
      return true;
    const std::string n = nameOf(op->getLHS());
    if (n.empty() || !isQualified(n))
      return true;
    // A fresh assignment revives a moved-from owner.
    if (ownerVars_.count(n) || viewVars_.count(n)) {
      moved_.erase(n);
      released_.erase(n);
      transferred_.erase(n);
    }
    if (isNullExpr(op->getRHS()))
      nonNull_.erase(n);
    else
      nonNull_.insert(n);
    return true;
  }

  bool VisitUnaryOperator(UnaryOperator *op) {
    if (!op || op->getOpcode() != UO_Deref)
      return true;
    const std::string n = nameOf(op->getSubExpr());
    if (n.empty())
      return true;
    if (optVars_.count(n) && !nonNull_.count(n))
      reportAt(op->getBeginLoc(), "CPLUS-OPT",
               "_Opt pointer '" + n +
                   "' is dereferenced without an active non-null refinement",
               "guard it with 'if (" + n + ") { ... }' or "
               "'if (!" + n + ") return;' before dereferencing");
    if ((ownerVars_.count(n) || viewVars_.count(n)) && moved_.count(n))
      reportAt(op->getBeginLoc(), "CPLUS-OWN",
               "use-after-move: dereference of moved '" + n + "'",
               "re-assign the owner or borrow it before the move instead");
    return true;
  }

  bool VisitMemberExpr(MemberExpr *member) {
    if (!member || !member->isArrow())
      return true;
    const std::string n = nameOf(member->getBase());
    if (n.empty())
      return true;
    if (optVars_.count(n) && !nonNull_.count(n))
      reportAt(member->getBeginLoc(), "CPLUS-OPT",
               "_Opt pointer '" + n +
                   "' is dereferenced by '->' without an active non-null "
                   "refinement",
               "guard it with 'if (" + n + ") { ... }' first");
    if ((ownerVars_.count(n) || viewVars_.count(n)) && moved_.count(n))
      reportAt(member->getBeginLoc(), "CPLUS-OWN",
               "use-after-move: member access through moved '" + n + "'",
               "re-assign the owner or borrow it before the move instead");
    return true;
  }

  bool VisitCallExpr(CallExpr *call) {
    if (!call)
      return true;
    const FunctionDecl *fd = call->getDirectCallee();
    const std::string fn = fd ? fd->getNameAsString() : std::string();
    const bool isDrop = fn == "free" || fn == "cplus_release" ||
                        (fn.size() > 5 &&
                         fn.compare(fn.size() - 5, 5, "_drop") == 0);
    if (isDrop || fn == "cplus_move") {
      if (call->getNumArgs() >= 1) {
        const std::string n = nameOf(call->getArg(0));
        if (!n.empty()) {
          if (isDrop)
            released_.insert(n);
          if (fn == "cplus_move") {
            if (viewVars_.count(n))
              reportAt(call->getBeginLoc(), "CPLUS-VIEW",
                       "_View '" + n +
                           "' is a borrow and must not be moved",
                       "borrows are non-owning; drop the _View qualifier or "
                       "move the underlying _Owner");
            else if (ownerVars_.count(n)) {
              if (moved_.count(n))
                reportAt(call->getBeginLoc(), "CPLUS-OWN",
                         "_Owner '" + n + "' is moved more than once",
                         "a moved owner is empty; re-assign it before "
                         "transferring it again");
              moved_.insert(n);
              released_.insert(n); // transferred out of this function
            }
          }
        }
      }
    }
    // Ownership-consuming callees reuse the existing effect summaries.
    for (unsigned i = 0; i < call->getNumArgs(); ++i) {
      const std::string n = nameOf(call->getArg(i));
      if (n.empty() || !ownerVars_.count(n))
        continue;
      if (cplusCakeEffectConsumesArg(fn, static_cast<int>(i))) {
        moved_.insert(n);
        released_.insert(n);
      }
    }
    return true;
  }

private:
  static std::string nameOf(const Expr *e) {
    if (!e)
      return std::string();
    e = e->IgnoreParenImpCasts();
    if (const auto *uo = dyn_cast<UnaryOperator>(e))
      if (uo->getOpcode() == UO_AddrOf)
        e = uo->getSubExpr()->IgnoreParenImpCasts();
    if (const auto *dr = dyn_cast<DeclRefExpr>(e))
      if (const auto *vd = dyn_cast<VarDecl>(dr->getDecl()))
        return vd->getNameAsString();
    return std::string();
  }

  static bool isNullExpr(const Expr *e) {
    if (!e)
      return false;
    e = e->IgnoreParenImpCasts();
    if (isa<CXXNullPtrLiteralExpr>(e))
      return true;
    if (const auto *il = dyn_cast<IntegerLiteral>(e))
      return il->getValue().isZero();
    if (const auto *dr = dyn_cast<DeclRefExpr>(e))
      return dr->getDecl() &&
             dr->getDecl()->getNameAsString() == "NULL";
    return false;
  }

  static bool containsReturn(const Stmt *s) {
    if (!s)
      return false;
    if (isa<ReturnStmt>(s))
      return true;
    for (const Stmt *c : s->children())
      if (containsReturn(c))
        return true;
    return false;
  }

  void collectTransfers(const Stmt *s) {
    if (!s)
      return;
    if (const auto *rs = dyn_cast<ReturnStmt>(s)) {
      const std::string n = nameOf(rs->getRetValue());
      if (!n.empty())
        transferred_.insert(n);
    }
    for (const Stmt *c : s->children())
      collectTransfers(c);
  }

  /// Turn the rewriter's original-source line facts into byte offsets in the
  /// lowered C buffer that Clang parsed. Sites are kept in source order, so a
  /// repeated move on one line is matched to its nth token occurrence.
  void seedMoveSites() {
    std::set<std::string> names = currentOwners_;
    names.insert(localViews_.begin(), localViews_.end());
    for (const std::string &n : names) {
      std::vector<unsigned> &offsets = moveOffsets_[n];
      std::vector<unsigned> lines = cplusCakeMoveSites(n);
      std::map<unsigned, unsigned> perLine;
      for (unsigned line : lines) {
        const size_t begin = cakeLineOffset(code_, line);
        const size_t end = cakeLineOffset(code_, line + 1);
        const unsigned nth = perLine[line]++;
        const size_t off = cakeMoveOffset(code_, begin, end, n, nth);
        if (off != std::string::npos)
          offsets.push_back(static_cast<unsigned>(off));
      }
      std::sort(offsets.begin(), offsets.end());
    }
  }

  /// Source-file byte range of a statement, if both endpoints come from the
  /// main file (macro expansions report their expansion location here).
  bool stmtOffsetRange(const Stmt *s, unsigned &begin, unsigned &end) const {
    if (!s)
      return false;
    const SourceManager &sm = ctx_.getSourceManager();
    SourceLocation b = sm.getExpansionLoc(s->getBeginLoc());
    SourceLocation e = sm.getExpansionLoc(s->getEndLoc());
    if (!b.isValid() || !e.isValid() ||
        sm.getFileID(b) != sm.getMainFileID() ||
        sm.getFileID(e) != sm.getMainFileID())
      return false;
    begin = sm.getFileOffset(b);
    end = sm.getFileOffset(e);
    return true;
  }

  void applyMovesInRange(unsigned begin, unsigned end, SourceLocation loc) {
    for (const auto &entry : moveOffsets_) {
      const std::string &n = entry.first;
      for (unsigned off : entry.second) {
        if (off < begin || off > end)
          continue;
        const auto site = std::make_pair(n, off);
        if (!appliedSites_.insert(site).second)
          continue;
        applyMove(n, loc);
      }
    }
  }

  void applyMove(const std::string &n, SourceLocation loc) {
    if (viewVars_.count(n)) {
      reportAt(loc, "CPLUS-VIEW",
               "_View '" + n + "' is a borrow and must not be moved",
               "borrows are non-owning; move the underlying _Owner instead");
      moved_.insert(n);
      return;
    }
    if (!ownerVars_.count(n)) {
      moved_.insert(n);
      return;
    }
    if (moved_.count(n))
      reportAt(loc, "CPLUS-OWN", "_Owner '" + n + "' is moved more than once",
               "a moved owner is empty; re-assign it before transferring it "
               "again");
    moved_.insert(n);
    released_.insert(n); // transferred out; not a leak at scope exit
  }

  static std::set<std::string> intersect(const std::set<std::string> &a,
                                         const std::set<std::string> &b) {
    std::set<std::string> out;
    std::set_intersection(a.begin(), a.end(), b.begin(), b.end(),
                          std::inserter(out, out.begin()));
    return out;
  }

  static std::set<std::string> setUnion(const std::set<std::string> &a,
                                        const std::set<std::string> &b) {
    std::set<std::string> out = a;
    out.insert(b.begin(), b.end());
    return out;
  }

  int qualifierOf(const VarDecl *vd) const {
    if (!vd)
      return 0;
    const std::string n = vd->getNameAsString();
    PresumedLoc pl = ctx_.getSourceManager().getPresumedLoc(vd->getLocation());
    if (pl.isValid()) {
      int q = cplusCakeQualifierOf(n, pl.getLine());
      if (q)
        return q;
    }
    const VarDecl *canonical = vd->getCanonicalDecl();
    if (canonical && canonical != vd) {
      PresumedLoc cp =
          ctx_.getSourceManager().getPresumedLoc(canonical->getLocation());
      if (cp.isValid())
        return cplusCakeQualifierOf(n, cp.getLine());
    }
    return 0;
  }

  bool isQualified(const std::string &n) const {
    return optVars_.count(n) || ownerVars_.count(n) || viewVars_.count(n);
  }

  void refineFromCond(const Expr *cond, std::string &name, bool &thenNonNull,
                      bool &elseNonNull) const {
    name.clear();
    thenNonNull = elseNonNull = false;
    if (!cond)
      return;
    const Expr *c = cond->IgnoreParenCasts();
    if (const auto *uo = dyn_cast<UnaryOperator>(c)) {
      if (uo->getOpcode() == UO_LNot) {
        name = nameOf(uo->getSubExpr());
        thenNonNull = false;
        elseNonNull = !name.empty();
      }
      return;
    }
    if (const auto *bo = dyn_cast<BinaryOperator>(c)) {
      if (!bo->isEqualityOp())
        return;
      name = nameOf(bo->getLHS());
      if (name.empty())
        name = nameOf(bo->getRHS());
      const bool nullComparison =
          isNullExpr(bo->getLHS()) || isNullExpr(bo->getRHS());
      if (!name.empty() && nullComparison) {
        thenNonNull = bo->getOpcode() == BO_NE;
        elseNonNull = bo->getOpcode() == BO_EQ;
      } else {
        name.clear();
      }
      return;
    }
    name = nameOf(c);
    thenNonNull = !name.empty();
  }

  void reportAt(SourceLocation loc, const char *category,
                const std::string &message, const std::string &help) {
    if (suppress_)
      return;
    std::string key = message + "@" + std::to_string(loc.getRawEncoding());
    if (!reported_.insert(key).second)
      return;
    PresumedLoc pl = ctx_.getSourceManager().getPresumedLoc(loc);
    Diag d;
    d.level = strict_ ? DiagLevel::Error : DiagLevel::Warning;
    d.message = "[" + std::string(category) + "] " + message;
    d.help = help;
    d.file = file_;
    if (pl.isValid()) {
      d.line = pl.getLine();
      d.column = pl.getColumn();
      std::string sourceFile = pl.getFilename();
      if (sourceFile == file_ + ".c")
        sourceFile = file_;
      if (!sourceFile.empty() && sourceFile != file_)
        d.file = sourceFile;
    }
    diags_.report(std::move(d));
  }

  ASTContext &ctx_;
  DiagnosticEngine &diags_;
  std::string file_;
  std::string code_;
  bool strict_ = false;
  bool suppress_ = false;
  std::set<std::string> optVars_;
  std::set<std::string> ownerVars_;
  std::set<std::string> viewVars_;
  std::set<std::string> currentOwners_;
  std::set<std::string> localViews_;
  std::set<std::string> moved_;
  std::set<std::string> released_;
  std::set<std::string> nonNull_;
  std::set<std::string> transferred_;
  std::map<std::string, std::vector<unsigned>> moveOffsets_;
  std::set<std::pair<std::string, unsigned>> appliedSites_;
  std::set<std::string> reported_;
};

} // namespace

namespace {
/// True when the backend already sanitises signed integer overflow.  Native
/// checks are only queued when it does not (notably --sanitize=none), so the
/// two mechanisms never duplicate work.
bool backendSanitizesSignedOverflow(const std::vector<std::string> &args) {
  for (const auto &a : args) {
    if (a.rfind("-fsanitize=", 0) != 0)
      continue;
    // Compare whole comma-separated tokens.  A substring search for
    // "signed-integer-overflow" also matches "unsigned-integer-overflow",
    // which is part of the `integer` sanitizer and does NOT check signed
    // overflow; that false positive used to suppress the native guard and
    // leave signed overflow silently unenforced under --sanitize=integer.
    std::string list = a.substr(strlen("-fsanitize="));
    size_t start = 0;
    while (start <= list.size()) {
      size_t comma = list.find(',', start);
      std::string token = list.substr(
          start, comma == std::string::npos ? std::string::npos : comma - start);
      if (token == "undefined" || token == "signed-integer-overflow" ||
          token == "all")
        return true;
      if (comma == std::string::npos)
        break;
      start = comma + 1;
    }
  }
  return false;
}

/// Built-in libc functions whose effect summary is compiled in. A pure libc
/// contract that merely borrows/reads a pointer stays accepted in STRICT even
/// when the contract file is supplied through the environment.
static bool isBuiltinLibcName(llvm::StringRef name) {
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

/// Names whose environment-supplied v2 contract claims NO memory-safety
/// consequence (no free, write or ownership-transfer effect) although the
/// function takes a pointer. Such a contract cannot be trusted to suppress the
/// STRICT unverified-extern gate: a lying "checked"/"borrows" claim would
/// otherwise certify a program that uses freed memory. The compiled-in
/// contract is trusted and deliberately not inspected here.
static std::set<std::string> envNoConsequenceContractFns() {
  std::set<std::string> out;
  const char *env = std::getenv("CPLUS_CONTRACT_PATH");
  if (!env || !*env)
    return out;
  auto buf = llvm::MemoryBuffer::getFile(env);
  if (!buf)
    return out; // unloadable explicit path is fail-closed elsewhere
  auto parsed = llvm::json::parse((*buf)->getBuffer());
  if (!parsed) {
    llvm::consumeError(parsed.takeError());
    return out;
  }
  const llvm::json::Object *root = parsed->getAsObject();
  const llvm::json::Object *fns = root ? root->getObject("functions") : nullptr;
  if (!fns)
    return out;
  auto claims = [](const llvm::json::Object &spec, llvm::StringRef key,
                   llvm::StringRef verb) {
    const auto *arr = spec.getArray(key);
    if (!arr)
      return false;
    for (const auto &v : *arr) {
      auto s = v.getAsString();
      if (s && s->find(verb) != llvm::StringRef::npos)
        return true;
    }
    return false;
  };
  for (const auto &entry : *fns) {
    const auto *spec = entry.second.getAsObject();
    if (!spec || isBuiltinLibcName(entry.first))
      continue;
    bool pointerParam = false;
    if (const auto *params = spec->getArray("params"))
      for (const auto &p : *params) {
        auto s = p.getAsString();
        if (s && (s->find('*') != llvm::StringRef::npos ||
                  s->find('[') != llvm::StringRef::npos)) {
          pointerParam = true;
          break;
        }
      }
    if (!pointerParam)
      continue;
    bool consequence = claims(*spec, "effects", "frees") ||
                       claims(*spec, "effects", "writes") ||
                       claims(*spec, "effects", "write") ||
                       claims(*spec, "effects", "allocates") ||
                       claims(*spec, "freeEffects", "frees") ||
                       claims(*spec, "writeEffects", "write");
    if (auto own = spec->getString("ownershipTransfer"))
      if (!own->empty() && *own != "n/a" && *own != "none")
        consequence = true;
    if (auto own = spec->getString("transfer"))
      if (!own->empty() && *own != "n/a" && *own != "none")
        consequence = true;
    if (!consequence)
      out.insert(entry.first.str());
  }
  return out;
}
} // namespace

void checkAstSafety(const std::string &loweredC, const std::string &filename,
                    const std::vector<std::string> &backendArgs,
                    DiagnosticEngine &diags, bool strict) {
  // A prior TU must not leak its sites into this one.
  setCheckedArithmeticSites({});
  setCheckedPointerSites({});
  bool needNativeArithmetic =
      !backendSanitizesSignedOverflow(backendArgs);

  // An explicitly requested contract file that cannot be loaded must fail
  // closed in STRICT: otherwise its effects would be silently assumed safe.
  if (strict && std::getenv("CPLUS_CONTRACT_PATH") && !contractsLoaded()) {
    Diag d;
    d.level = DiagLevel::Error;
    d.message = "[CPLUS-LIBC] CPLUS_CONTRACT_PATH could not be loaded; STRICT "
                "mode cannot verify libc effects";
    d.file = filename;
    diags.report(std::move(d));
  }

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
          s.find("overflow in") == std::string::npos &&
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

  // STRICT contract-honesty gate. A lying environment-supplied contract that
  // claims no free/write/ownership effect for a pointer-taking function must
  // not suppress the bodiless-extern hard error. Only source-local externs are
  // rejected here; header-declared APIs whose definition lives in another
  // input/sibling TU keep the driver's wave-2 multi-TU treatment.
  if (strict) {
    std::set<std::string> suspect = envNoConsequenceContractFns();
    if (!suspect.empty()) {
      ASTContext &c = ast->getASTContext();
      struct ExternFinder : RecursiveASTVisitor<ExternFinder> {
        ASTContext &ctx;
        const std::string &file;
        const std::set<std::string> &names;
        DiagnosticEngine &diags;
        bool emitted = false;
        ExternFinder(ASTContext &c, const std::string &f,
                     const std::set<std::string> &n, DiagnosticEngine &d)
            : ctx(c), file(f), names(n), diags(d) {}
        bool VisitFunctionDecl(FunctionDecl *fd) {
          if (emitted || !fd || fd->doesThisDeclarationHaveABody())
            return true;
          std::string name = fd->getNameAsString();
          if (!names.count(name))
            return true;
          bool ptr = fd->getReturnType()->isPointerType();
          for (const ParmVarDecl *p : fd->parameters())
            if (p->getType()->isPointerType())
              ptr = true;
          if (!ptr)
            return true;
          SourceManager &sm = ctx.getSourceManager();
          PresumedLoc ploc = sm.getPresumedLoc(fd->getBeginLoc());
          std::string diagFile = ploc.isValid() ? ploc.getFilename() : "";
          if (diagFile != file && diagFile != file + ".c")
            return true; // header-declared: multi-TU logic applies
          Diag d;
          d.level = DiagLevel::Error;
          d.message = "[CPLUS-FFI] function '" + name +
                      "' has an environment-supplied contract with no "
                      "free/write/ownership-transfer effect for a "
                      "pointer-taking function; STRICT check cannot verify "
                      "its effects";
          d.file = file;
          if (ploc.isValid()) {
            d.line = ploc.getLine();
            d.column = ploc.getColumn();
          }
          diags.report(std::move(d));
          emitted = true;
          return true;
        }
      } finder(c, filename, suspect, diags);
      finder.TraverseDecl(c.getTranslationUnitDecl());
    }
  }

  SafetyVisitor collect(ast->getASTContext(), diags, filename, false);
  // Iterate the collection pass to a fixpoint: parameter extents, free/deref
  // effects, returned aliases and global points-to facts must flow through
  // multi-level call chains regardless of definition order.
  for (int round = 0; round < 8; ++round)
    collect.TraverseDecl(ast->getASTContext().getTranslationUnitDecl());

  SafetyVisitor check(ast->getASTContext(), diags, filename, true);
  check.setStrict(strict);
  check.setRecordArithmeticSites(needNativeArithmetic);
  check.setHandlers(collect.handlers());
  check.setGlobals(collect.nonVolatileGlobals());
  check.setDestructors(collect.destructors());
  check.setFieldAliases(collect.fieldAliases());
  check.setArithEffects(collect.arithEffects());
  check.setConstReturnValues(collect.constReturnValues());
  check.setFormatFnAliases(collect.formatFnAliases());
  check.setHandledReallocs(collect.handledReallocs());
  check.setHandledReallocVars(collect.handledReallocVars());
  check.setReallocAssignCalls(collect.reallocAssignCalls());
  check.setConstLocals(collect.constLocals());
  check.setConstFloatLocals(collect.constFloatLocals());
  check.setAssignedVars(collect.assignedVars());
  check.setFunctionsReturningFreed(collect.functionsReturningFreed());
  check.setNullReturningFunctions(collect.nullReturningFunctions());
  check.setDerefParams(collect.derefParams());
  check.setThreadSharedArgFacts(
      collect.threadSharedArgCounts(), collect.threadSharedArgLocs(),
      collect.hasLockCalls());
  check.setParamWriteSlots(collect.paramWriteSlots());
  check.userFrees_ = collect.userFrees_;
  check.userFreesPointee_ = collect.userFreesPointee_;
  check.setGlobalPointsTo(collect.globalPointsTo());
  check.setUnionVars(collect.unionVars());
  check.setFnPtrVarTargets(collect.fnPtrVarTargets());
  check.setFnPtrParamTargets(collect.fnPtrParamTargets());
  check.returnedFnPtr_ = collect.returnedFnPtr_;
  check.freedGlobals_ = collect.freedGlobals_;
  check.atexitFunctions_ = collect.atexitFunctions_;
  check.globalGetterReturns_ = collect.globalGetterReturns_;
  check.globalAllocPointsTo_ = collect.globalAllocPointsTo_;
  check.allocIdentityMembers_ = collect.allocIdentityMembers_;
  check.varAllocIdentity_ = collect.varAllocIdentity_;
  check.setReturnedOutParams(collect.returnedOutParams());
  check.setReturnedOutAllocs(collect.returnedOutAllocs());
  check.setMaybeReleasedVars(collect.maybeReleasedVars());
  check.setMaybeReleaseLines(collect.maybeReleaseLines());
  check.setAddressTakenVars(collect.addressTakenVars());
  check.setStackArrayExtents(collect.stackArrayExtents());
  check.setCallParamExtents(collect.callParamExtents());
  check.setCallSiteFacts(collect.callSiteFacts());
  check.deriveCountPairedParams();
  check.setIndirectSlotExtents(collect.indirectSlotExtents());
  check.setFieldSlotExtents(collect.fieldSlotExtents());
  check.setConflictedParams(collect.conflictedParams());
  check.setReturnedParams(collect.returnedParams());
  check.setReturnedFixedExtents(collect.returnedFixedExtents());
  check.setReturnedAllocExtents(collect.returnedAllocExtents());
  check.setCallReturnExtent(collect.callReturnExtent());
  check.TraverseDecl(ast->getASTContext().getTranslationUnitDecl());
  if (needNativeArithmetic)
    setCheckedArithmeticSites(check.takeArithmeticSites());
    setCheckedPointerSites(check.takePointerSites());
  setCheckedAllocSites(check.takeAllocSites());
  setCheckedFreeSites(check.takeFreeSites());

  // Issue #179: Cake qualifiers are checked only when the source actually
  // declared them, so plain C and every existing diagnostic are untouched.
  CakeFlowVisitor cake(ast->getASTContext(), diags, filename, loweredC, strict);
  cake.TraverseDecl(ast->getASTContext().getTranslationUnitDecl());

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
    GlobalWriteFinder gw(collect.allGlobals(), entryFn);
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

// ---------------------------------------------------------------------------
// Issue #181: opt-in `-fcplus-disjoint-bounds` instrumentation.
//
// This is deliberately additive and off by default.  It runs AFTER all static
// safety analysis and rewrites ONLY the lowered C text: every pointer object
// gets (base, bound, key, lock) metadata in the runtime's pointer-slot side
// table, pointer assignments/arithmetic propagate that tuple, and every
// dereference/subscript gets an O(1) cplus_shadow_slot_check() call.  A stack
// frame key is registered on entry and invalidated by a cleanup attribute on
// every exit path.  Untagged/uninstrumented pointers keep plain-C behaviour.
// ---------------------------------------------------------------------------
namespace {

struct ShadowEdit {
  SourceLocation loc;
  std::string text;
  bool afterToken;
};

static std::vector<std::string>
shadowParseArgs(const std::vector<std::string> &backendArgs) {
  std::vector<std::string> args;
  for (size_t i = 0; i < backendArgs.size(); ++i) {
    const std::string &arg = backendArgs[i];
    if (arg == "-isysroot") {
      ++i; // skip the SDK path
      continue;
    }
    if (arg.rfind("-isysroot=", 0) == 0)
      continue;
    if (arg.rfind("-I", 0) == 0 || arg.rfind("-D", 0) == 0 ||
        arg.rfind("-std=", 0) == 0)
      args.push_back(arg);
  }
#ifdef CPLUS_CLANG_RESOURCE_DIR
  args.push_back(std::string("-resource-dir=") + CPLUS_CLANG_RESOURCE_DIR);
#endif
  args.push_back("-fsyntax-only");
  return args;
}

static bool shadowInMainFile(const SourceManager &sm, SourceLocation loc) {
  return loc.isValid() && !loc.isMacroID() &&
         sm.isWrittenInMainFile(sm.getExpansionLoc(loc));
}

static bool shadowDirectCompoundStmtChild(ASTContext &ctx, const Stmt *s) {
  for (const auto &p : ctx.getParents(*s))
    if (p.get<CompoundStmt>())
      return true;
  return false;
}

/// Statement to instrument before: the nearest ancestor that is a direct
/// child of a CompoundStmt (so the inserted check is a valid statement).
static const Stmt *shadowAnchorStmt(ASTContext &ctx, const Stmt *s) {
  DynTypedNode cur = DynTypedNode::create(*s);
  for (unsigned depth = 0; depth < 64; ++depth) {
    DynTypedNodeList parents = ctx.getParents(cur);
    if (parents.empty())
      return NULL;
    const DynTypedNode &parent = *parents.begin();
    if (parent.get<CompoundStmt>())
      return cur.get<Stmt>();
    if (const Stmt *next = parent.get<Stmt>()) {
      cur = DynTypedNode::create(*next);
      continue;
    }
    // A dereference inside a variable initializer reaches a VarDecl before
    // its DeclStmt.  Climb the Decl chain to that statement.
    if (const Decl *d = parent.get<Decl>()) {
      DynTypedNode dn = DynTypedNode::create(*d);
      for (unsigned j = 0; j < 16; ++j) {
        DynTypedNodeList ps = ctx.getParents(dn);
        if (ps.empty())
          break;
        if (const DeclStmt *ds = ps.begin()->get<DeclStmt>())
          return ds;
        if (const Decl *nd = ps.begin()->get<Decl>()) {
          dn = DynTypedNode::create(*nd);
          continue;
        }
        break;
      }
    }
    return NULL;
  }
  return NULL;
}

static SourceLocation shadowAfterSemicolon(ASTContext &ctx, const Stmt *s) {
  return Lexer::findLocationAfterToken(s->getEndLoc(), tok::semi,
                                       ctx.getSourceManager(),
                                       ctx.getLangOpts(), false);
}

static std::string shadowSourceText(const Expr *e, const ASTContext &ctx) {
  return Lexer::getSourceText(CharSourceRange::getTokenRange(e->getSourceRange()),
                              ctx.getSourceManager(), ctx.getLangOpts())
      .str();
}

/// Only side-effect-free index expressions are duplicated into the check.
static bool shadowSimpleIndex(const Expr *e) {
  if (!e || !e->getType()->isIntegerType())
    return false;
  const Expr *x = e->IgnoreParenImpCasts();
  if (isa<IntegerLiteral>(x) || isa<DeclRefExpr>(x))
    return true;
  if (const auto *uo = dyn_cast<UnaryOperator>(x))
    return uo->getOpcode() == UO_Plus || uo->getOpcode() == UO_Minus
               ? shadowSimpleIndex(uo->getSubExpr())
               : false;
  if (const auto *bo = dyn_cast<BinaryOperator>(x))
    return !bo->isAssignmentOp() && !bo->isCompoundAssignmentOp() &&
           shadowSimpleIndex(bo->getLHS()) &&
           shadowSimpleIndex(bo->getRHS());
  if (const auto *cast = dyn_cast<ExplicitCastExpr>(x))
    return shadowSimpleIndex(cast->getSubExpr());
  return false;
}

class ShadowInstrumentVisitor
    : public RecursiveASTVisitor<ShadowInstrumentVisitor> {
public:
  ShadowInstrumentVisitor(ASTContext &ctx, std::vector<ShadowEdit> &edits)
      : ctx_(ctx), sm_(ctx.getSourceManager()), edits_(edits) {}

  bool VisitFunctionDecl(FunctionDecl *fd) {
    Stmt *body = fd->doesThisDeclarationHaveABody() ? fd->getBody() : nullptr;
    if (!body || !shadowInMainFile(sm_, body->getBeginLoc()))
      return true;
    std::string text =
        "\n  uint64_t __cplus_shadow_frame "
        "__attribute__((cleanup(cplus_shadow_frame_leave))) = "
        "cplus_shadow_frame_enter();";
    for (const ParmVarDecl *p : fd->parameters())
      if (p->getType()->isPointerType())
        text += "\n  cplus_shadow_slot_seed(&" + p->getNameAsString() +
                ", (const void *)(" + p->getNameAsString() + "));";
    edits_.push_back({body->getBeginLoc(), text, true});
    return true;
  }

  bool VisitVarDecl(VarDecl *vd) {
    if (!vd->isLocalVarDecl() || !shadowInMainFile(sm_, vd->getLocation()))
      return true;
    const std::string name = vd->getNameAsString();
    if (name.empty())
      return true;
    if (vd->getType()->isArrayType()) {
      const DeclStmt *ds = enclosingDeclStmt(vd);
      if (!ds || !shadowDirectCompoundStmtChild(ctx_, ds))
        return true;
      SourceLocation after = shadowAfterSemicolon(ctx_, ds);
      if (after.isInvalid() || !shadowInMainFile(sm_, after))
        return true;
      edits_.push_back({after, "\n  cplus_shadow_frame_register(&" + name +
                                   ", &__cplus_shadow_frame, (const void *)(" +
                                   name + "), sizeof(" + name + "));",
                        false});
      return true;
    }
    if (!vd->getType()->isPointerType() || !vd->hasInit())
      return true;
    const DeclStmt *ds = enclosingDeclStmt(vd);
    if (!ds || !shadowDirectCompoundStmtChild(ctx_, ds))
      return true;
    SourceLocation after = shadowAfterSemicolon(ctx_, ds);
    if (after.isInvalid() || !shadowInMainFile(sm_, after))
      return true;
    edits_.push_back({after, "\n  " + seedText(name, vd->getInit()), false});
    return true;
  }

  bool VisitBinaryOperator(BinaryOperator *op) {
    if (op->getOpcode() != BO_Assign ||
        !shadowInMainFile(sm_, op->getBeginLoc()) ||
        !shadowDirectCompoundStmtChild(ctx_, op))
      return true;
    // Compound pointer assignment (p += n) keeps the same base/bound/key, so
    // no re-seed is needed; only plain assignment carries new provenance.
    const auto *lhs =
        dyn_cast<DeclRefExpr>(op->getLHS()->IgnoreParenImpCasts());
    if (!lhs)
      return true;
    const auto *lv = dyn_cast<VarDecl>(lhs->getDecl());
    if (!lv || !lv->isLocalVarDecl() || !lv->getType()->isPointerType())
      return true;
    SourceLocation after = shadowAfterSemicolon(ctx_, op);
    if (after.isInvalid() || !shadowInMainFile(sm_, after))
      return true;
    const std::string name = lv->getNameAsString();
    const Expr *rhs = op->getRHS()->IgnoreParenImpCasts();
    std::string text;
    if (const auto *src = dyn_cast<DeclRefExpr>(rhs))
      text = "cplus_shadow_slot_copy(&" + name + ", &" +
             shadowSourceText(src, ctx_) + ");";
    else if (const auto *arith = dyn_cast<BinaryOperator>(rhs)) {
      const Expr *ptrSide = arith->getLHS()->IgnoreParenImpCasts();
      if (!ptrSide->getType()->isPointerType())
        ptrSide = arith->getRHS()->IgnoreParenImpCasts();
      const auto *src = dyn_cast<DeclRefExpr>(ptrSide);
      if (src && (arith->getOpcode() == BO_Add ||
                  arith->getOpcode() == BO_Sub))
        text = "cplus_shadow_slot_copy(&" + name + ", &" +
               shadowSourceText(src, ctx_) + ");";
    }
    if (text.empty())
      text = "cplus_shadow_slot_seed(&" + name + ", (const void *)(" + name +
             "));";
    edits_.push_back({after, "\n  " + text, false});
    return true;
  }

  bool VisitUnaryOperator(UnaryOperator *uo) {
    if (uo->getOpcode() != UO_Deref)
      return true;
    const auto *base =
        dyn_cast<DeclRefExpr>(uo->getSubExpr()->IgnoreParenImpCasts());
    if (!base || !shadowPointerish(base))
      return true;
    const std::string b = shadowSourceText(base, ctx_);
    addCheck(uo, shadowSourceText(base, ctx_), "(const void *)(" + b + ")",
             "sizeof(*(" + b + "))");
    return true;
  }

  bool VisitArraySubscriptExpr(ArraySubscriptExpr *sub) {
    const auto *base =
        dyn_cast<DeclRefExpr>(sub->getBase()->IgnoreParenImpCasts());
    if (!base || !shadowPointerish(base) ||
        !shadowSimpleIndex(sub->getIdx()))
      return true;
    const std::string b = shadowSourceText(base, ctx_);
    const std::string idx = shadowSourceText(sub->getIdx(), ctx_);
    addCheck(sub, shadowSourceText(base, ctx_),
             "(const void *)((const char *)(" + b +
                 ") + (unsigned long long)(" + idx +
                 ") * (unsigned long long)sizeof((" + b + ")[0]))",
             "sizeof((" + b + ")[0])");
    return true;
  }

private:
  static bool shadowPointerish(const DeclRefExpr *e) {
    return e->getType()->isPointerType() || e->getType()->isArrayType();
  }

  const DeclStmt *enclosingDeclStmt(const Decl *d) const {
    DynTypedNode cur = DynTypedNode::create(*d);
    for (unsigned depth = 0; depth < 16; ++depth) {
      DynTypedNodeList parents = ctx_.getParents(cur);
      if (parents.empty())
        return NULL;
      if (const DeclStmt *ds = parents.begin()->get<DeclStmt>())
        return ds;
      if (const Decl *next = parents.begin()->get<Decl>()) {
        cur = DynTypedNode::create(*next);
        continue;
      }
      return NULL;
    }
    return NULL;
  }

  std::string seedText(const std::string &name, const Expr *init) const {
    if (const auto *addr = dyn_cast<UnaryOperator>(init->IgnoreParenImpCasts())) {
      if (addr->getOpcode() == UO_AddrOf) {
        const auto *obj = dyn_cast<DeclRefExpr>(
            addr->getSubExpr()->IgnoreParenImpCasts());
        if (obj && shadowInMainFile(sm_, obj->getLocation())) {
          const std::string o = shadowSourceText(obj, ctx_);
          return "cplus_shadow_frame_register(&" + name +
                 ", &__cplus_shadow_frame, (const void *)(" + o +
                 "), sizeof(" + o + "));";
        }
      }
    }
    return "cplus_shadow_slot_seed(&" + name + ", (const void *)(" + name +
           "));";
  }

  void addCheck(const Stmt *s, const std::string &slot,
                const std::string &ptrExpr, const std::string &sizeExpr) {
    if (!shadowInMainFile(sm_, s->getBeginLoc()))
      return;
    const Stmt *anchor = shadowAnchorStmt(ctx_, s);
    if (!anchor || !shadowInMainFile(sm_, anchor->getBeginLoc()))
      return;
    edits_.push_back({anchor->getBeginLoc(),
                      "\n  cplus_shadow_slot_check(&" + slot + ", " + ptrExpr +
                          ", " + sizeExpr + ", __FILE__, __LINE__);\n  ",
                      false});
  }

  ASTContext &ctx_;
  SourceManager &sm_;
  std::vector<ShadowEdit> &edits_;
};

} // namespace

bool instrumentDisjointBounds(const std::string &loweredC,
                              const std::vector<std::string> &backendArgs,
                              std::string &out, std::string &error) {
  std::vector<std::string> args = shadowParseArgs(backendArgs);
  clang::IgnoringDiagConsumer ignored;
  std::unique_ptr<ASTUnit> ast = tooling::buildASTFromCodeWithArgs(
      loweredC, args, "cplus-disjoint-bounds.c", "cplus-shadow-instrument",
      std::make_shared<PCHContainerOperations>(),
      tooling::getClangStripDependencyFileAdjuster(), {}, &ignored);
  if (!ast || ast->getDiagnostics().hasErrorOccurred()) {
    error = "cannot build the AST for disjoint-bounds instrumentation";
    return false;
  }
  ASTContext &ctx = ast->getASTContext();
  SourceManager &sm = ctx.getSourceManager();
  std::vector<ShadowEdit> edits;
  ShadowInstrumentVisitor visitor(ctx, edits);
  visitor.TraverseDecl(ctx.getTranslationUnitDecl());
  if (edits.empty()) {
    out = loweredC;
    return true;
  }
  Rewriter rewriter;
  rewriter.setSourceMgr(sm, ctx.getLangOpts());
  for (const ShadowEdit &edit : edits) {
    if (!shadowInMainFile(sm, edit.loc))
      continue;
    if (edit.afterToken)
      rewriter.InsertTextAfterToken(edit.loc, edit.text);
    else
      rewriter.InsertText(edit.loc, edit.text);
  }
  llvm::raw_string_ostream os(out);
  rewriter.getEditBuffer(sm.getMainFileID()).write(os);
  os.flush();
  return true;
}

} // namespace cplus
