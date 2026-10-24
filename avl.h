#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * ============================================================
 * AVL TREE - Self-Balancing Binary Search Tree
 * ============================================================
 *
 * PROBLEM ZSet needs to solve:
 *   - Insert member with a score: O(log n)
 *   - Look up member by name: O(1) via hash map
 *   - Get members in score order: O(log n + k)
 *   - Get rank of a member: O(log n)
 *   - Range query by score: O(log n + k)
 *
 * A plain BST gives O(log n) on average but O(n) worst case
 * if keys arrive in sorted order (degenerates to a linked list).
 *
 * AVL TREE guarantees O(log n) always by keeping the tree balanced.
 *
 * BALANCE INVARIANT:
 * For every node, |height(left) - height(right)| <= 1.
 * Height = length of longest path to a leaf.
 *
 * When an insert or delete violates this, we perform ROTATIONS
 * to restore balance. Rotations are O(1) pointer updates.
 *
 * EXTRA FIELD - cnt (subtree size):
 * Stores the count of nodes in each subtree.
 * Enables O(log n) rank queries:
 *   rank(node) = cnt(left subtree) + 1
 * Without cnt, rank would require O(n) traversal.
 *
 * INTERVIEW:
 * "I used an AVL tree for the ZSet's sorted index because it
 * guarantees O(log n) for all operations. The cnt field in each
 * node enables O(log n) rank queries without scanning the tree.
 * Combined with a hash map for O(1) name lookups, ZSet achieves
 * the same complexity as Redis's own sorted set implementation."
 * ============================================================
 */

struct AVLNode {
    AVLNode *parent = NULL;  // parent pointer (NULL for root)
    AVLNode *left   = NULL;  // left child (smaller keys)
    AVLNode *right  = NULL;  // right child (larger keys)
    uint32_t height = 0;     // height of this subtree
    uint32_t cnt    = 0;     // number of nodes in this subtree
};

// Initialize a new leaf node (height=1, cnt=1, no children)
inline void avl_init(AVLNode *node) {
    node->left = node->right = node->parent = NULL;
    node->height = 1;
    node->cnt    = 1;
}

// Safe height accessor (returns 0 for NULL)
inline uint32_t avl_height(AVLNode *node) { return node ? node->height : 0; }

// Safe count accessor (returns 0 for NULL)
inline uint32_t avl_cnt(AVLNode *node)    { return node ? node->cnt    : 0; }

// Fix balance violations from `node` up to the root. Returns new root.
AVLNode *avl_fix(AVLNode *node);

// Remove `node` from the tree. Returns new root.
AVLNode *avl_del(AVLNode *node);

// Navigate to the node at rank (current_rank + offset). O(log n).
AVLNode *avl_offset(AVLNode *node, int64_t offset);
