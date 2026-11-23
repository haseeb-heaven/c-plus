/* F-072: vector slice generation staleness via metadata.
 * Reads go through slice::at_checked only while stale() == 0; after a shape
 * change the slice must report stale() == 1 and refuse reads.  Allocator
 * metadata generations are checked by identity, never by dereferencing a
 * retired pointer.  Fixed seed 0xF072..., 2400 valid operations. */
#define F100_ID "F-072"
#define F100_SEED 0xF0725EEDULL
#define F100_OPS 2400
#define F100_MODE F100_M_VEC_STALE
#include "f100_cont_engine.hp"
