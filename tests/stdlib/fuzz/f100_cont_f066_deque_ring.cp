/* F-066: deque ring-buffer wraparound and capacity growth.
 * Alternating push_front/push_back with pop_front/pop_back wraps head around
 * the ring; reserve() and the documented SIZE_MAX overflow (-1) are probed at
 * deterministic points.  Fixed seed 0xF066..., 2600 valid operations. */
#define F100_ID "F-066"
#define F100_SEED 0xF0665EEDULL
#define F100_OPS 2600
#define F100_MODE F100_M_DEQUE
#include "f100_cont_engine.hp"
