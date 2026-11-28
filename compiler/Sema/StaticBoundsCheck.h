//===- StaticBoundsCheck.h - C+ static array bounds check ---------*- C++ -*-===//
//
// A small, fast, token-level check for array accesses whose bounds can be
// proven wrong at compile time.  It complements (and does not replace) the
// C compiler's own -Warray-bounds and the UBSan run-time checks.
//
// Currently detects:
//   * constant subscripts past a known array extent;
//   * simple counted `for` loops (`i = 0; i < N; i++`) whose index reaches
//     past an array subscripted with `i`;
//   * negative constant indices into a fixed array, including through a
//     pointer whose value is a provable constant offset from that array;
//   * dereferences of a one-past-the-end pointer derived from a fixed array
//     (forming the one-past pointer is still allowed);
//   * constant indices in multi-dimensional subscript chains, including
//     through a pointer-to-row alias of a fixed array.
// All three extensions require both the index and the extent to be provable
// constants; anything else is left to the host C compiler and UBSan.
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
