//===- RaiiLowering.h - C+ deterministic cleanup lowering -------*- C++ -*-===//
//
// Second lowering stage: inserts deterministic destruction for declarations
// that own a resource or a managed allocation.
//
// The C+ syntax rewriting stage marks every owning declaration with a
// structured comment:
//
//     File f = File_open("x");  /*cplus$own:f:File_drop*/
//     Buffer *b = cplus_new(Buffer);  /*cplus$mem:b*/
//
// This pass finds those markers, tracks lexical scopes, and inserts the
// matching cleanup calls:
//
//   * at normal scope exit, in reverse construction order,
//   * before `return`, innermost scope first,
//   * before `break` / `continue` for the scopes they exit.
//
// Cleanup is intentionally explicit source text, so the result is auditable
// with `c+ build --emit-c`.
//
//===----------------------------------------------------------------------===//

#ifndef CPLUS_RAIILOWERING_H
#define CPLUS_RAIILOWERING_H

#include "Diagnostics.h"
#include "LangFeatures.h"

#include <string>
#include <vector>

namespace cplus {

struct LoweredSource {
  std::string text;
  bool changed = false;
  unsigned obligations = 0;
};

/// Insert cleanup code required by C+ ownership rules into `source`.
/// `resourceTypes` is the set of `resource struct` names discovered by the
/// syntax lowering stage.
LoweredSource lowerRaii(const std::string &source,
                        std::vector<std::string> resourceTypes,
                        DiagnosticEngine &diags, const std::string &filename,
                        Mode mode, bool strict = false);

} // namespace cplus

#endif // CPLUS_RAIILOWERING_H
