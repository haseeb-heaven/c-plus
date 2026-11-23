/* F-075: cleanup and telemetry balance across every .hp container.
 * Telemetry is enabled and reset before the schedule; after all containers are
 * dropped exactly once, alloc_count must equal free_count with zero invalid,
 * bounds, ownership, lifecycle or allocation-failure events.  Fixed seed
 * 0xF075..., 2600 valid operations. */
#define F100_ID "F-075"
#define F100_SEED 0xF0755EEDULL
#define F100_OPS 2600
#define F100_MODE F100_M_BALANCE
#include "f100_cont_engine.hp"
