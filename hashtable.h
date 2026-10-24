#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * ============================================================
 * CUSTOM HASH TABLE WITH PROGRESSIVE REHASHING
 * ============================================================
 *
 * WHY BUILD A CUSTOM HASH TABLE?
 * std::unordered_map does rehashing all at once — when the load
 * factor is exceeded, it stops and resizes, causing a latency spike.
 * For a server handling thousands of requests/second, that pause
 * is unacceptable.
 *
 * Our solution: PROGRESSIVE REHASHING
 * We maintain TWO hash tables simultaneously:
 *   - newer: the new, larger table (inserts go here)
 *   - older: the old table (being drained)
 *
 * On every operation, we migrate a fixed batch of nodes from
 * older -> newer. This spreads the rehashing work across many
 * requests instead of one big pause.
 *
 * CHAINING (Separate Chaining):
 * Each slot is a linked list (chain) of nodes.
 * When two keys hash to the same slot, they chain together.
 * 
 * Alternative: Open Addressing (linear probing)
 * Better cache performance but more complex deletion.
 * Redis uses chaining (simpler, easier to implement correctly).
 *
 * LOAD FACTOR:
 * load = size / capacity
 * When load > threshold (we use 8), trigger rehashing.
 * High load = long chains = slow lookups.
 * Low load = wasted memory.
 * ============================================================
 */

/**
 * HNode - A node embedded into whatever data we're storing
 *
 * INTRUSIVE DESIGN: The node lives INSIDE the payload struct.
 * No separate allocation. The payload struct (Entry) contains an
 * HNode as a member. The hash table stores HNode pointers.
 * We use container_of() to get back to the Entry.
 */
struct HNode {
    HNode *next  = NULL;    // next node in the chain (linked list)
    uint64_t hcode = 0;     // cached hash code (avoid recomputing)
};

/**
 * HTab - A single fixed-size hash table
 *
 * Size is always a power of 2. This lets us use bitwise AND
 * instead of modulo for slot lookup:
 *   slot = hcode % size        <- slow (division)
 *   slot = hcode & (size - 1)  <- fast (bitwise AND)
 * This works because for power-of-2 N: x % N == x & (N-1)
 */
struct HTab {
    HNode **tab = NULL; // array of slot pointers (each points to a chain)
    size_t mask = 0;    // size - 1 (for fast modulo: hcode & mask)
    size_t size = 0;    // number of nodes currently stored
};

/**
 * HMap - The real hash map (uses two HTabs for progressive rehashing)
 *
 * State machine:
 *   Normal:    newer has data, older is empty
 *   Rehashing: both have data, migrate_pos tracks where we are
 *   Done:      all migrated, older is freed
 */
struct HMap {
    HTab newer;             // active table (all new inserts go here)
    HTab older;             // old table being drained during rehashing
    size_t migrate_pos = 0; // current position in older being migrated
};

// Public API
HNode *hm_lookup(HMap *hmap, HNode *key, bool (*eq)(HNode *, HNode *));
void   hm_insert(HMap *hmap, HNode *node);
HNode *hm_delete(HMap *hmap, HNode *key, bool (*eq)(HNode *, HNode *));
void   hm_clear(HMap *hmap);
size_t hm_size(HMap *hmap);
void   hm_foreach(HMap *hmap, bool (*f)(HNode *, void *), void *arg);
