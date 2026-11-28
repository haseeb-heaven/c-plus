/* F-068: stack doubling growth, top_checked and clear().
 * Capacity is tracked in the shadow so every push is checked for the exact
 * failure-atomic reallocation point.  Fixed seed 0xF068..., 2300 operations. */
#define F100_ID "F-068"
#define F100_SEED 0xF0685EEDULL
#define F100_OPS 2300
#define F100_MODE F100_M_STACK
#include "f100_cont_engine.hp"
