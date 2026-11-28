/* F-067: doubly linked list front/back churn and clear().
 * Every push allocates one node; pops and clear() release exactly one node
 * each.  The shadow model tracks positional order after every op.
 * Fixed seed 0xF067..., 2400 valid operations. */
#define F100_ID "F-067"
#define F100_SEED 0xF0675EEDULL
#define F100_OPS 2400
#define F100_MODE F100_M_LIST
#include "f100_cont_engine.hp"
