//===- CPlusRewriter.h - C+ source rewriting ---------------------*- C++ -*-===//

#ifndef CPLUS_CPLUSREWRITER_H
#define CPLUS_CPLUSREWRITER_H

#include "Diagnostics.h"
#include "Sema/SemaCPlus.h"

#include <string>

namespace cplus {

/// Rewrite C+ specific syntax into equivalent, annotated C syntax.
/// See src/Sema/CPlusRewriter.cpp for the transformation rules.
RewrittenSource rewriteCPlusSource(const std::string &source,
                                   DiagnosticEngine &diags,
                                   const std::string &filename);

} // namespace cplus

#endif // CPLUS_CPLUSREWRITER_H
