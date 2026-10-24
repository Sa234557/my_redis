#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include "zset.h"
#include "common.h"

// ============================================================
// ZNODE ALLOCATION
// ============================================================

/**
 * znode_new - Allocate a ZNode with inline name storage
 *
 * Uses a flexible array member (char name[0]) to store the name
 * bytes immediately after the struct — one allocation, no pointer.
 *
 * sizeof(ZNode) + len = space for struct + name bytes
 */
static ZNode *znode_new(const char *name, size_t len, double score) {
    ZNode *node = (ZNode *)malloc(sizeof(ZNode) + len);
    assert(node);
    avl_init(&node->tree);
    node->hmap.next  = NULL;
    node->hmap.hcode = str_hash((uint8_t *)name, len);
    node->score      = score;
    node->len        = len;
    memcpy(&node->name[0], name, len);  // copy name inline
    return node;
}

static void znode_del(ZNode *node) { free(node); }

// ============================================================
// AVL TREE ORDERING: sorted by (score, name) lexicographically
// ============================================================

static size_t zmin(size_t a, size_t b) { return a < b ? a : b; }

/**
 * zless - Compare AVLNode to (score, name) tuple
 *
 * Sort order: first by score (ascending), then by name (lexicographic).
 * This gives a total order — no two distinct members are equal.
 *
 * Why include name in ordering?
 * Multiple members can have the same score.
 * The (score, name) pair is always unique.
 */
static bool zless(AVLNode *lhs, double score, const char *name, size_t len) {
    ZNode *zl = container_of(lhs, ZNode, tree);
    if (zl->score != score) {
        return zl->score < score;   // primary sort: score
    }
    // Secondary sort: lexicographic name comparison
    int rv = memcmp(zl->name, name, zmin(zl->len, len));
    if (rv != 0) return rv < 0;
    return zl->len < len;           // shorter name comes first if prefix match
}

// Overload for comparing two AVLNodes directly
static bool zless(AVLNode *lhs, AVLNode *rhs) {
    ZNode *zr = container_of(rhs, ZNode, tree);
    return zless(lhs, zr->score, zr->name, zr->len);
}

// ============================================================
// TREE OPERATIONS
// ============================================================

/**
 * tree_insert - Insert a ZNode into the AVL tree
 *
 * Standard BST insert: walk left/right based on comparison,
 * attach at the empty slot, then call avl_fix() to restore balance.
 */
static void tree_insert(ZSet *zset, ZNode *node) {
    AVLNode  *parent = NULL;
    AVLNode **from   = &zset->root;

    while (*from) {
        parent = *from;
        from = zless(&node->tree, parent) ? &parent->left : &parent->right;
    }

    *from = &node->tree;
    node->tree.parent = parent;
    zset->root = avl_fix(&node->tree);  // rebalance from insert point up
}

/**
 * zset_update - Update score of an existing node
 *
 * Must remove from tree and reinsert — score is the sort key.
 * Hash map entry stays (name doesn't change).
 */
static void zset_update(ZSet *zset, ZNode *node, double score) {
    if (node->score == score) return;   // no change needed
    zset->root = avl_del(&node->tree);  // remove from tree
    avl_init(&node->tree);              // reset tree node fields
    node->score = score;
    tree_insert(zset, node);            // reinsert at new position
}

// ============================================================
// HASH MAP COMPARISON
// ============================================================

// Temporary struct for hash map lookups by name
struct HKey {
    HNode      node;
    const char *name = NULL;
    size_t     len   = 0;
};

static bool hcmp(HNode *node, HNode *key) {
    ZNode *znode = container_of(node, ZNode, hmap);
    HKey  *hkey  = container_of(key,  HKey,  node);
    if (znode->len != hkey->len) return false;
    return memcmp(znode->name, hkey->name, znode->len) == 0;
}

// ============================================================
// PUBLIC ZSet API
// ============================================================

/**
 * zset_insert - Insert or update (name, score) pair
 *
 * If name exists: update score (reposition in tree)
 * If new: allocate ZNode, insert into both hash map and tree
 *
 * Returns: true if inserted, false if updated
 */
bool zset_insert(ZSet *zset, const char *name, size_t len, double score) {
    ZNode *node = zset_lookup(zset, name, len);
    if (node) {
        zset_update(zset, node, score);
        return false;   // updated existing
    }
    node = znode_new(name, len, score);
    hm_insert(&zset->hmap, &node->hmap);    // O(1) hash map insert
    tree_insert(zset, node);                // O(log n) tree insert
    return true;    // new insertion
}

/**
 * zset_lookup - Find ZNode by name (O(1) via hash map)
 */
ZNode *zset_lookup(ZSet *zset, const char *name, size_t len) {
    if (!zset->root) return NULL;

    HKey key;
    key.node.hcode = str_hash((uint8_t *)name, len);
    key.name = name;
    key.len  = len;

    HNode *found = hm_lookup(&zset->hmap, &key.node, &hcmp);
    return found ? container_of(found, ZNode, hmap) : NULL;
}

/**
 * zset_delete - Remove a member from the sorted set
 */
void zset_delete(ZSet *zset, ZNode *node) {
    // Remove from hash map
    HKey key;
    key.node.hcode = node->hmap.hcode;
    key.name = node->name;
    key.len  = node->len;
    HNode *found = hm_delete(&zset->hmap, &key.node, &hcmp);
    assert(found);

    // Remove from AVL tree and free
    zset->root = avl_del(&node->tree);
    znode_del(node);
}

/**
 * zset_seekge - Find first node with (score, name) >= given key
 *
 * Used for ZRANGE: find the start of a score range.
 * Returns NULL if no such node exists.
 *
 * INTERVIEW: "zset_seekge does a BST traversal, tracking the
 * last candidate where node >= key. Final candidate is the answer.
 * This is the standard 'find lower bound' BST algorithm."
 */
ZNode *zset_seekge(
    ZSet *zset, double score, const char *name, size_t len)
{
    AVLNode *found = NULL;
    AVLNode *node  = zset->root;

    while (node) {
        if (zless(node, score, name, len)) {
            node = node->right;     // node < key: go right
        } else {
            found = node;           // node >= key: candidate
            node  = node->left;     // try to find smaller candidate
        }
    }
    return found ? container_of(found, ZNode, tree) : NULL;
}

/**
 * znode_offset - Navigate to node at (rank + offset) in O(log n)
 *
 * Wraps avl_offset for ZNode.
 * Used for ZRANGE after finding the start node.
 */
ZNode *znode_offset(ZNode *node, int64_t offset) {
    AVLNode *tnode = node ? avl_offset(&node->tree, offset) : NULL;
    return tnode ? container_of(tnode, ZNode, tree) : NULL;
}

// ============================================================
// CLEANUP
// ============================================================

static void tree_dispose(AVLNode *node) {
    if (!node) return;
    tree_dispose(node->left);
    tree_dispose(node->right);
    znode_del(container_of(node, ZNode, tree));
}

void zset_clear(ZSet *zset) {
    hm_clear(&zset->hmap);
    tree_dispose(zset->root);
    zset->root = NULL;
}
