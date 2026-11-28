/* Common integer workload shared by clang C, C+, Fil-C and (in sibling
 * sources) Rust and Goose.  All implementations use the same SplitMix64
 * sequence, FNV-1a mixing and three phases, so their final checksum must be
 * identical.  Work amount is CPLUS_SIX_SCALE. */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cplus/cplus.h"

#define SCALE_DEFAULT 100u
#define STREAM_PER_SCALE 5000000u
#define ITEMS_PER_SCALE 200000u
#define BUCKETS 8192u
#define ARENA_BYTES (1u << 20)
#define RECORD_BYTES 64u
#define ARENA_ROUNDS_PER_SCALE 200u

static uint64_t state = 0x9e3779b97f4a7c15ULL;

static uint64_t next_random(void) {
    state += 0x9e3779b97f4a7c15ULL;
    uint64_t z = state;
    z = (z ^ (z >> 30u)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27u)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31u);
}

static uint64_t mix(uint64_t h, uint64_t v) {
    return (h ^ v) * 0x100000001b3ULL;
}

typedef struct Node {
    uint64_t key;
    uint64_t value;
    struct Node *next;
} Node;

static void *new_node(size_t bytes) {
#ifdef CPLUS
    return cplus_alloc(bytes);
#else
    return malloc(bytes);
#endif
}

static void drop_node(void *p) {
#ifdef CPLUS
    cplus_release(p);
#else
    free(p);
#endif
}

int main(void) {
    const char *text = getenv("CPLUS_SIX_SCALE");
    uint32_t scale = text ? (uint32_t)strtoul(text, NULL, 10) : SCALE_DEFAULT;
    if (scale == 0u)
        return 2;

    uint64_t stream = 0xcbf29ce484222325ULL;
    uint64_t count = (uint64_t)STREAM_PER_SCALE * scale;
    for (uint64_t i = 0; i < count; ++i)
        stream = mix(stream, next_random());
    printf("PHASE stream %" PRIu64 "\n", stream);

    uint64_t hash = 0x84222325cbf29ce4ULL;
    uint32_t items = ITEMS_PER_SCALE * scale;
    Node **buckets = (Node **)calloc(BUCKETS, sizeof(*buckets));
    Node **all = (Node **)malloc((size_t)items * sizeof(*all));
    if (!buckets || !all)
        return 2;
    for (uint32_t item_i = 0; item_i < items; ++item_i) {
        uint32_t i = item_i;
        Node *node = (Node *)new_node(sizeof(*node));
        if (!node)
            return 2;
        node->key = next_random();
        node->value = i;
        uint64_t b = node->key % BUCKETS;
        node->next = buckets[b];
        buckets[b] = node;
        all[i] = node;
    }
    for (uint32_t lookup_i = 0; lookup_i < items; ++lookup_i) {
        uint32_t i = lookup_i;
        Node *node = all[i];
        Node *cursor = buckets[node->key % BUCKETS];
        while (cursor && cursor->key != node->key)
            cursor = cursor->next;
        if (cursor)
            hash = mix(hash, cursor->value);
    }
    for (uint32_t release_i = 0; release_i < items; ++release_i)
        drop_node(all[release_i]);
    free(all);
    free(buckets);
    printf("PHASE hash %" PRIu64 "\n", hash);

    uint64_t arena = 0x517cc1b727220a95ULL;
    unsigned char *block = (unsigned char *)malloc(ARENA_BYTES);
    if (!block)
        return 2;
    for (uint32_t round_i = 0;
         round_i < ARENA_ROUNDS_PER_SCALE * scale; ++round_i) {
        for (size_t off = 0; off + RECORD_BYTES <= ARENA_BYTES; off += RECORD_BYTES) {
            uint64_t raw = next_random();
            memset(block + off, (int)(raw & 0xffu), RECORD_BYTES);
            arena = mix(arena, block[off]);
        }
    }
    free(block);
    printf("PHASE arena %" PRIu64 "\n", arena);
    printf("TOTAL %" PRIu64 " %" PRIu64 " %" PRIu64 "\n", stream, hash, arena);
    return 0;
}
