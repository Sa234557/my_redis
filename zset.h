#pragma once

#include "avl.h"
#include "hashtable.h"

/**
 * ============================================================
 * ZSET - Sorted Set (like Redis ZADD/ZRANGE/ZRANK)
 * ============================================================
 *
 * A ZSet stores (name, score) pairs with two indexes:
 *
 * 1. Hash map (HMap): name -> ZNode   O(1) lookup by name
 * 2. AVL tree (root): sorted by (score, name)  O(log n) range queries
 *
 * COMMANDS THIS ENABLES:
 *   zadd  <key> <score> <member>  - insert or update
 *   zscore <key> <member>         - get score by name (uses hash map)
 *   zrank  <key> <member>         - get rank by name (uses AVL cnt)
 *   zrange <key> <start> <stop>   - get members by rank range
 *   zrem   <key> <member>         - remove member
 *
 * INTERVIEW: "ZSet combines a hash map and AVL tree on the same
 * nodes using intrusive design — each ZNode embeds both an HNode
 * and AVLNode. This avoids double allocation and gives O(1) name
 * lookup alongside O(log n) sorted operations. This mirrors
 * exactly how Redis implements sorted sets internally."
 * ============================================================
 */

struct ZSet {
    AVLNode *root = NULL;   // AVL tree root (sorted by score, name)
    HMap     hmap;          // hash map (indexed by name)
};

/**
 * ZNode - One member of a sorted set
 *
 * Embeds both AVLNode (for the tree) and HNode (for the hash map).
 * name[] is a flexible array at the end — the actual name bytes
 * are stored inline after the struct (no separate allocation).
 *
 * Memory layout:
 * [tree][hmap][score][len][name bytes...]
 */
struct ZNode {
    AVLNode tree;       // embedded AVL tree node
    HNode   hmap;       // embedded hash map node
    double  score = 0;  // the score (sort key)
    size_t  len   = 0;  // length of name in bytes
    char    name[0];    // flexible array — name stored inline
};

bool   zset_insert(ZSet *zset, const char *name, size_t len, double score);
ZNode *zset_lookup(ZSet *zset, const char *name, size_t len);
void   zset_delete(ZSet *zset, ZNode *node);
ZNode *zset_seekge(ZSet *zset, double score, const char *name, size_t len);
void   zset_clear(ZSet *zset);
ZNode *znode_offset(ZNode *node, int64_t offset);
