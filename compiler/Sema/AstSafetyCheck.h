#ifndef CPLUS_ASTSAFETYCHECK_H
#define CPLUS_ASTSAFETYCHECK_H

#include "Diagnostics.h"

#include <string>
#include <vector>

namespace cplus {

/// Run semantic safety checks on already-lowered C using Clang's AST.
/// Diagnostics are mapped back to the original source via #line directives.
void checkAstSafety(const std::string &loweredC, const std::string &filename,
                    const std::vector<std::string> &backendArgs,
                    DiagnosticEngine &diags, bool strict = false);

} // namespace cplus

#endif
