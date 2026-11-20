//===- StaticBoundsCheck.h - C+ static array bounds check ---------*- C++ -*-===//
//
// A small, fast, token-level check for array accesses whose bounds can be
// proven wrong at compile time.  It complements (and does not replace) the
// C compiler's own -Warray-bounds and the UBSan run-time checks.
//
// Currently detects:
//   * constant subscripts past a known array extent;
//   * simple counted `for` loops (`i = 0; i < N; i++`) whose index reaches
//     past an array subscripted with `i`.
//
//===----------------------------------------------------------------------===//

#ifndef CPLUS_STATICBOUNDSCHECK_H
#define CPLUS_STATICBOUNDSCHECK_H

#include "Diagnostics.h"

#include <map>
#include <string>

namespace cplus {

/// Report statically provable out-of-bounds accesses in `source`.
/// `arrayLengths` maps array names to their extents, as collected by the C+
/// syntax rewriter.
void checkStaticBounds(const std::string &source,
                       const std::map<std::string, size_t> &arrayLengths,
                       DiagnosticEngine &diags, const std::string &filename);

} // namespace cplus

#endif // CPLUS_STATICBOUNDSCHECK_H
