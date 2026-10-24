#include <assert.h>
#include <stdlib.h>     // calloc(), free()
#include "hashtable.h"

// ============================================================
// SINGLE TABLE OPERATIONS (internal)
// ============================================================

/**
 * h_init - Initialize a hash table with n slots
 *
 * n MUST be a power of 2. The assert checks this with a bit trick:
 * A power of 2 in binary is 10...0. Subtract 1: 01...1.
 * AND them: 0. So (n-1) & n == 0 iff n is a power of 2.
 *
 * calloc vs malloc: calloc zero-initializes memory.
 * We need all slot pointers to start as NULL (empty chains).
 */
static void h_init(HTab *htab, size_t n) {
    assert(n > 0 && ((n - 1) & n) == 0);   // must be power of 2
    htab->tab  = (HNode **)calloc(n, sizeof(HNode *));
    htab->mask = n - 1;
    htab->size = 0;
}

/**
 * h_insert - Insert a node into a single hash table
 *
 * Uses chaining: new node is prepended to the chain at its slot.
 * Prepending is O(1) — no need to walk the chain.
 *
 * Slot index = hcode & mask (fast power-of-2 modulo)
 */
static void h_insert(HTab *htab, HNode *node) {
    size_t pos   = node->hcode & htab->mask;    // which slot?
    HNode *next  = htab->tab[pos];              // current chain head
    node->next   = next;                        // new node points to old head
    htab->tab[pos] = node;                      // slot now points to new node
    htab->size++;
}

/**
 * h_lookup - Find a node in a single hash table
 *
 * Returns: pointer to the POINTER that owns the found node.
 * This is a double pointer trick that enables O(1) deletion
 * without needing a "previous node" pointer.
 *
 * Example chain: slot -> A -> B -> C -> NULL
 * If we want B, we return &A->next (the pointer pointing TO B).
 * To delete B: *from = B->next (sets A->next = C, bypassing B).
 *
 * WHY double pointer?
 * Single pointer to B means we also need pointer to A to do deletion.
 * Double pointer to B's owner eliminates that need.
 *
 * We check hcode FIRST (cheap int compare) before calling eq()
 * (expensive string compare). This skips most false positives fast.
 */
static HNode **h_lookup(
    HTab *htab, HNode *key, bool (*eq)(HNode *, HNode *))
{
    if (!htab->tab) {
        return NULL;    // uninitialized table
    }

    size_t pos    = key->hcode & htab->mask;
    HNode **from  = &htab->tab[pos];    // start: the slot pointer itself

    for (HNode *cur; (cur = *from) != NULL; from = &cur->next) {
        // hcode match is cheap, call eq() only when hcodes match
        if (cur->hcode == key->hcode && eq(cur, key)) {
            return from;    // found it — return address of owner pointer
        }
    }
    return NULL;            // not found
}

/**
 * h_detach - Remove a node from a chain (after h_lookup found it)
 *
 * `from` is the owner pointer returned by h_lookup.
 * We bypass the node by making owner point to node->next.
 *
 * Before: [from] -> [node] -> [node->next]
 * After:  [from] ---------> [node->next]
 *
 * The node itself is untouched — caller handles deallocation.
 */
static HNode *h_detach(HTab *htab, HNode **from) {
    HNode *node = *from;        // the node to remove
    *from = node->next;         // bypass it
    htab->size--;
    return node;
}

// ============================================================
// PROGRESSIVE REHASHING
// ============================================================

// How many nodes to migrate per operation
// Small constant = bounded latency per request
const size_t k_rehashing_work = 128;

/**
 * hm_help_rehashing - Migrate a batch of nodes from older to newer
 *
 * Called on every hm_lookup, hm_insert, hm_delete.
 * Each call migrates up to k_rehashing_work nodes.
 * This spreads rehashing cost across many requests.
 *
 * INTERVIEW: "This is the key design decision. Standard hash maps
 * rehash all at once causing O(n) latency spike. Our progressive
 * approach does O(1) work per request, keeping latency bounded."
 */
static void hm_help_rehashing(HMap *hmap) {
    size_t nwork = 0;

    while (nwork < k_rehashing_work && hmap->older.size > 0) {
        HNode **from = &hmap->older.tab[hmap->migrate_pos];

        if (!*from) {
            // Empty slot — skip it, advance position
            hmap->migrate_pos++;
            continue;
        }

        // Move first node in this chain to the newer table
        h_insert(&hmap->newer, h_detach(&hmap->older, from));
        nwork++;
    }

    // If older is fully drained, free it
    if (hmap->older.size == 0 && hmap->older.tab) {
        free(hmap->older.tab);
        hmap->older = HTab{};   // zero it out
    }
}

/**
 * hm_trigger_rehashing - Start a rehashing cycle
 *
 * Called when load factor exceeds threshold.
 * Promotes newer to older, creates a new larger newer.
 * New size = 2x old size.
 *
 * After this:
 *   older = what was newer (needs draining)
 *   newer = new empty table (2x bigger)
 */
static void hm_trigger_rehashing(HMap *hmap) {
    assert(hmap->older.tab == NULL);    // can't rehash while already rehashing
    hmap->older = hmap->newer;          // old table needs draining
    h_init(&hmap->newer, (hmap->newer.mask + 1) * 2);  // new table 2x bigger
    hmap->migrate_pos = 0;
}

// ============================================================
// PUBLIC API
// ============================================================

/**
 * hm_lookup - Look up a key in the hash map
 *
 * Searches newer first, then older (if rehashing in progress).
 * Also does a bit of rehashing work on every call.
 *
 * Time complexity: O(1) average, O(n) worst case (all keys in one chain)
 * With good hash function and load factor <= 8: effectively O(1)
 */
HNode *hm_lookup(HMap *hmap, HNode *key, bool (*eq)(HNode *, HNode *)) {
    hm_help_rehashing(hmap);

    // Search newer table first
    HNode **from = h_lookup(&hmap->newer, key, eq);
    if (!from) {
        // Not in newer, check older (if rehashing)
        from = h_lookup(&hmap->older, key, eq);
    }
    return from ? *from : NULL;
}

// Load factor threshold: trigger rehash when size >= capacity * 8
// Real Redis uses 1.0 for the main table, 0.1 for rehashing-in-progress
const size_t k_max_load_factor = 8;

/**
 * hm_insert - Insert a node into the hash map
 *
 * Always inserts into newer table.
 * Triggers rehashing if load factor is exceeded.
 * Does incremental rehashing work.
 */
void hm_insert(HMap *hmap, HNode *node) {
    if (!hmap->newer.tab) {
        h_init(&hmap->newer, 4);    // lazy init: start small
    }

    h_insert(&hmap->newer, node);

    // Trigger rehash if load factor exceeded (only when not already rehashing)
    if (!hmap->older.tab) {
        size_t threshold = (hmap->newer.mask + 1) * k_max_load_factor;
        if (hmap->newer.size >= threshold) {
            hm_trigger_rehashing(hmap);
        }
    }

    hm_help_rehashing(hmap);   // always do some rehash work
}

/**
 * hm_delete - Delete a key from the hash map
 *
 * Returns the deleted node (caller handles deallocation).
 * Returns NULL if not found.
 */
HNode *hm_delete(HMap *hmap, HNode *key, bool (*eq)(HNode *, HNode *)) {
    hm_help_rehashing(hmap);

    // Try newer first
    if (HNode **from = h_lookup(&hmap->newer, key, eq)) {
        return h_detach(&hmap->newer, from);
    }
    // Try older
    if (HNode **from = h_lookup(&hmap->older, key, eq)) {
        return h_detach(&hmap->older, from);
    }
    return NULL;
}

/**
 * hm_clear - Free all memory used by the hash map
 * Note: Does NOT free the nodes themselves — caller owns them
 */
void hm_clear(HMap *hmap) {
    free(hmap->newer.tab);
    free(hmap->older.tab);
    *hmap = HMap{};
}

/**
 * hm_size - Total number of entries in both tables
 */
size_t hm_size(HMap *hmap) {
    return hmap->newer.size + hmap->older.size;
}

/**
 * hm_foreach - Iterate over all nodes, call f() on each
 * Stops if f() returns false (early exit).
 * Used for KEYS command.
 */
static bool h_foreach(HTab *htab, bool (*f)(HNode *, void *), void *arg) {
    for (size_t i = 0; htab->mask != 0 && i <= htab->mask; i++) {
        for (HNode *node = htab->tab[i]; node != NULL; node = node->next) {
            if (!f(node, arg)) {
                return false;   // early exit
            }
        }
    }
    return true;
}

void hm_foreach(HMap *hmap, bool (*f)(HNode *, void *), void *arg) {
    h_foreach(&hmap->newer, f, arg) && h_foreach(&hmap->older, f, arg);
}
