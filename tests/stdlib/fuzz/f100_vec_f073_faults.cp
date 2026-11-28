/* F-073: deterministic allocation-fault injection on vector growth paths.
 * Every 37th operation arms a one-shot allocation failure immediately before
 * reserve/insert/reallocation; the operation must return -1 and leave the
 * vector size, capacity and contents unchanged.  Fixed seed 0xF073...,
 * 2400 valid operations. */
#define F100_ID "F-073"
#define F100_SEED 0xF0735EEDULL
#define F100_OPS 2400
#define F100_MODE F100_M_VEC_FAULT
#include "f100_cont_engine.hp"
