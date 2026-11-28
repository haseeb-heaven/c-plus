/* F-063: intset tombstone reuse on collision-heavy keys.
 * Same deterministic tombstone cycle as F-062, driven through intset's
 * insert/remove/contains surface.  Fixed seed 0xF063..., 2300 operations. */
#define F100_ID "F-063"
#define F100_SEED 0xF0635EEDULL
#define F100_OPS 2300
#define F100_MODE F100_M_TOMB_SET
#include "f100_cont_engine.hp"
