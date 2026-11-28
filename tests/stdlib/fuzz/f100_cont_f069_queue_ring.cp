/* F-069: queue FIFO ring wraparound and doubling growth.
 * Push/pop sequences drive head past the end of the backing array while the
 * shadow tracks FIFO order exactly.  Fixed seed 0xF069..., 2600 operations. */
#define F100_ID "F-069"
#define F100_SEED 0xF0695EEDULL
#define F100_OPS 2600
#define F100_MODE F100_M_QUEUE
#include "f100_cont_engine.hp"
