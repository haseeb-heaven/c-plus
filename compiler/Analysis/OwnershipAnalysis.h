#ifndef CPLUS_OWNERSHIP_ANALYSIS_H
#define CPLUS_OWNERSHIP_ANALYSIS_H

#include "llvm/ADT/StringRef.h"

#include <clang/AST/Decl.h>

#include <functional>
#include <set>
#include <string>
#include <vector>

namespace cplus {

/// Minimal interprocedural effect summary for known library/user functions.
struct FunctionEffect {
  bool allocates = false;      ///< returns an owned allocation
  bool frees = false;          ///< frees a pointer argument
  bool borrows = false;        ///< observes a pointer argument without taking ownership
  bool writes = false;         ///< writes through a pointer/size argument
  bool returnsOwned = false;   ///< return value transfers ownership
  bool nullableArg = false;    ///< pointer argument may be NULL
  bool checkedSize = false;    ///< has an explicit destination-capacity argument
  bool mayFail = false;        ///< failure is reported in return value/errno
  int freedArg = -1;           ///< argument index freed, when known
  int destArg = -1;            ///< destination buffer argument, when known
  int sizeArg = -1;            ///< destination capacity argument, when known

  // --- Precise v2 argument-index metadata ---------------------------------
  // Additive: these carry the exact indices encoded in the v2 contract's
  // structured fields. The scalar fields above are deliberately left
  // untouched so existing diagnostics are unchanged; new consumers should
  // prefer these precise lists.
  /// Exact indices from the `nullableArgs` array (arguments accepting NULL).
  std::vector<int> nullableArgIndices;
  /// Argument indices denoted by `ownershipTransfer` when it refers to an
  /// argument (e.g. "arg", or the old pointer invalidated on success).
  /// A return-value transfer ("return") yields no index.
  std::vector<int> ownershipTransferArgs;
  /// Argument indices freed/consumed, derived from `freeEffects`.
  std::vector<int> freeArgs;
  /// Argument indices written through, derived from `writeEffects`.
  std::vector<int> writeArgs;

  std::string category = "CPLUS-SAFE-FFI"; ///< diagnostic category
};

/// Known libc effects. Unknown functions are NOT assumed safe.
FunctionEffect libcEffect(llvm::StringRef name);

/// True when `name` has a declared safety effect summary: either a v2 JSON
/// contract (CPLUS_CONTRACT_PATH / compiled-in default) or one of the built-in
/// libc fallbacks in libcEffect(). STRICT analysis may accept a bodiless call
/// only when such a summary exists; an unannotated extern with pointer effects
/// is unverifiable and must be rejected by `c+ check --strict`.
bool hasDeclaredEffect(llvm::StringRef name);

/// Names of functions actually DEFINED (given a body) in `source`, parsed with
/// the same parse-relevant args as probeSafetyAst. The driver uses this to
/// tell a genuine unverified extern from a multi-TU API whose implementation
/// lives in another input file or a sibling helper in the same directory.
std::set<std::string> definedFunctionNames(const std::string &source,
                                           const std::vector<std::string> &args,
                                           const std::string &fileName);

/// A contract that could not be fully interpreted. STRICT safe mode must
/// reject calls to functions with such contracts instead of assuming safety.
struct ContractIssue {
  std::string function; ///< empty for file-level issues
  std::string message;
};

/// Issues found while loading/validating the machine-readable contracts.
const std::vector<ContractIssue> &contractIssues();

/// True when the contract file was found and parsed successfully.
bool contractsLoaded();

/// Report callback used by the CFG analyses.
using CfgReportFn =
    std::function<void(clang::SourceLocation, const std::string &)>;

/// Path-sensitive definite-initialization check over Clang's CFG.
void checkDefiniteInitialization(clang::ASTContext &ctx,
                                 clang::FunctionDecl *fd, const CfgReportFn &report);

/// Result of a lightweight parse probe used by the driver to (a) fail closed
/// when the in-memory safety AST cannot be built and (b) disclose that STRICT
/// analysis stops at translation-unit boundaries.
struct SafetyAstProbe {
  /// False when the source (with the backend's parse-relevant flags) failed to
  /// parse. The C+ safety analysis would then be silently skipped.
  bool parsed = true;
  /// Names of user (non-system-header) functions that are referenced by the
  /// translation unit but have no definition in it. Their bodies live in
  /// another TU or a library and are therefore not safety-checked here.
  std::vector<std::string> unresolvedFunctions;
  /// Subset of `unresolvedFunctions` that (a) is referenced or called by the
  /// TU, (b) is declared in user code (not a system header), (c) has pointer
  /// effects in its signature, and (d) has NO declared contract. Their bodies
  /// and effects are invisible to this TU, so STRICT `check` must reject them
  /// instead of reporting a clean result. Pure libc calls are excluded here
  /// because libcEffect()/the JSON contract already describes them.
  std::vector<std::string> unverifiedPointerExterns;
  /// Subset of `unverifiedPointerExterns` whose declaration lives in a header
  /// (not the main source file). A source-local `extern` with no body is the
  /// suspicious red case; a header-declared API is the normal multi-TU shape.
  std::vector<std::string> headerDeclaredExterns;
};

/// Build an in-memory Clang AST for `source` with the same parse-relevant
/// arguments the backend compile uses. This never emits user-facing
/// diagnostics; it only reports parse viability and bodiless user functions.
SafetyAstProbe probeSafetyAst(const std::string &source,
                              const std::vector<std::string> &args,
                              const std::string &fileName);

} // namespace cplus

#endif
