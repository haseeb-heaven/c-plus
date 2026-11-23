/* F-065: strmap tombstone reuse with owned key allocation/free.
 * Scripted insert/remove/reinsert cycles stress the key-copy path and
 * MAP_DELETED slot reuse; clear()/drop() must release each owned key once.
 * Fixed seed 0xF065..., 2400 valid operations. */
#define F100_ID "F-065"
#define F100_SEED 0xF0655EEDULL
#define F100_OPS 2400
#define F100_MODE F100_M_TOMB_STR
#include "f100_cont_engine.hp"
