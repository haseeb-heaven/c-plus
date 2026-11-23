/* F-062: intmap tombstone reuse on collision-heavy keys.
 * A scripted insert/remove/reinsert-in-reverse cycle runs every 32 ops so
 * MAP_DELETED slots are repeatedly recycled instead of growing the table.
 * Fixed seed 0xF062..., 2400 valid operations, shadow model after each op. */
#define F100_ID "F-062"
#define F100_SEED 0xF0625EEDULL
#define F100_OPS 2400
#define F100_MODE F100_M_TOMB_INT
#include "f100_cont_engine.hp"
