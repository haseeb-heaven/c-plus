#ifndef CPLUS_OWNERSHIP_ANALYSIS_H
#define CPLUS_OWNERSHIP_ANALYSIS_H

#include <clang/AST/Decl.h>

#include <functional>
#include <string>

namespace cplus {

/// Report callback used by the CFG analyses.
using CfgReportFn =
    std::function<void(clang::SourceLocation, const std::string &)>;

/// Path-sensitive definite-initialization check over Clang's CFG.
void checkDefiniteInitialization(clang::ASTContext &ctx,
                                 clang::FunctionDecl *fd, const CfgReportFn &report);

} // namespace cplus

#endif
