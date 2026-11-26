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
                                   const std::string &filename,
                                   bool headerMode = false);

/// Register a class declared in a lowered `.hp` header so that a later
/// translation unit can resolve method calls and constructor sugar for it.
void registerExternalClass(const std::string &name, bool hasConstructor);

/// Lower C+ `try { ... } catch (Exception e) { ... }` and `throw expr;` to
/// plain C using the ABI-neutral runtime exception helpers. Safe mode only;
/// LEGACY source is left byte-identical.  `diags` (when non-null) reports
/// unsupported constructs such as a second catch clause instead of silently
/// discarding them.
std::string lowerExceptions(const std::string &source,
                            DiagnosticEngine *diags = nullptr,
                            const std::string &filename = {});

/// Register an owning type declared in a lowered `.hp` header.
void registerExternalResource(const std::string &name);

/// Clear the external-class registry (used between unrelated builds).
void clearExternalClasses();

} // namespace cplus

#endif // CPLUS_CPLUSREWRITER_H
