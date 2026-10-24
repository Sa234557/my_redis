#include <assert.h>
#include "avl.h"

// ============================================================
// HELPER: Update height and cnt after structural changes
// ============================================================

static uint32_t max(uint32_t a, uint32_t b) { return a < b ? b : a; }

/**
 * avl_update - Recalculate height and cnt for a node
 *
 * Must be called after any change to a node's children.
 * height = 1 + max(left_height, right_height)
 * cnt    = 1 + left_cnt + right_cnt
 */
static void avl_update(AVLNode *node) {
    node->height = 1 + max(avl_height(node->left), avl_height(node->right));
    node->cnt    = 1 + avl_cnt(node->left) + avl_cnt(node->right);
}

// ============================================================
// ROTATIONS - The core of AVL balancing
// ============================================================

/**
 * rot_left - Left rotation around `node`
 *
 * BEFORE:          AFTER:
 *     node              R
 *    /    \           /   \
 *   L      R   =>  node   RR
 *         / \      /  \
 *        RL  RR   L   RL
 *
 * When right subtree is taller: rotate left to balance.
 * R becomes the new subtree root.
 * RL (R's left child) becomes node's new right child.
 *
 * All parent pointers must be updated carefully.
 */
static AVLNode *rot_left(AVLNode *node) {
    AVLNode *parent   = node->parent;
    AVLNode *R        = node->right;    // will become new root
    AVLNode *RL       = R->left;        // will be rehomed to node->right

    // Step 1: node adopts RL as its new right child
    node->right = RL;
    if (RL) RL->parent = node;

    // Step 2: R takes node's place — inherits node's parent
    R->parent = parent;

    // Step 3: R adopts node as its left child
    R->left = node;
    node->parent = R;

    // Step 4: Update heights/cnts bottom-up (node first, then R)
    avl_update(node);
    avl_update(R);

    return R;   // R is the new subtree root
}

/**
 * rot_right - Right rotation around `node`
 *
 * BEFORE:          AFTER:
 *     node              L
 *    /    \           /   \
 *   L      R   =>   LL   node
 *  / \                   /  \
 * LL  LR                LR   R
 *
 * Mirror image of rot_left. When left subtree is taller.
 */
static AVLNode *rot_right(AVLNode *node) {
    AVLNode *parent   = node->parent;
    AVLNode *L        = node->left;     // will become new root
    AVLNode *LR       = L->right;       // will be rehomed to node->left

    // Step 1: node adopts LR as its new left child
    node->left = LR;
    if (LR) LR->parent = node;

    // Step 2: L takes node's place
    L->parent = parent;

    // Step 3: L adopts node as its right child
    L->right = node;
    node->parent = L;

    // Step 4: Update bottom-up
    avl_update(node);
    avl_update(L);

    return L;   // L is the new subtree root
}

// ============================================================
// FIX IMBALANCE
// ============================================================

/**
 * avl_fix_left - Fix when left subtree is taller by 2
 *
 * Two cases:
 *
 * Case 1 (Left-Left): left->left is taller — single right rotation
 *     node              L
 *    /    \           /   \
 *   L      R   =>   LL   node
 *  / \                   /  \
 * LL  LR                LR   R
 *
 * Case 2 (Left-Right): left->right is taller — double rotation
 *   First rotate left->left rotation, then node->right rotation
 *     node                node              LR
 *    /    \              /    \           /    \
 *   L      R    =>     LR     R   =>    L     node
 *  / \                /  \             /      /  \
 * LL  LR             L   LRR          LL    LRR    R
 *    /  \           / \
 *  LRL  LRR       LL  LRL
 */
static AVLNode *avl_fix_left(AVLNode *node) {
    // Case 2: left->right taller — convert to Case 1 first
    if (avl_height(node->left->left) < avl_height(node->left->right)) {
        node->left = rot_left(node->left);
        node->left->parent = node;
    }
    // Case 1: single right rotation
    return rot_right(node);
}

/**
 * avl_fix_right - Fix when right subtree is taller by 2
 * Mirror of avl_fix_left.
 */
static AVLNode *avl_fix_right(AVLNode *node) {
    // Case 2: right->left taller — convert to Case 1 first
    if (avl_height(node->right->right) < avl_height(node->right->left)) {
        node->right = rot_right(node->right);
        node->right->parent = node;
    }
    // Case 1: single left rotation
    return rot_left(node);
}

// ============================================================
// PUBLIC API
// ============================================================

/**
 * avl_fix - Restore AVL balance from node up to root
 *
 * Called after insert or delete to fix any balance violations.
 * Walks up the tree fixing imbalances, updating heights/cnts.
 *
 * After insert: at most O(log n) rotations needed.
 * After delete: same, but may need more fixes than insert.
 *
 * Returns the new root of the tree.
 */
AVLNode *avl_fix(AVLNode *node) {
    while (true) {
        // `from` = the pointer that points TO this node
        // (either parent->left, parent->right, or our local `node`)
        // We update it when a rotation changes which node is here.
        AVLNode **from   = &node;
        AVLNode  *parent = node->parent;

        if (parent) {
            from = (parent->left == node) ? &parent->left : &parent->right;
        }

        // Update height and cnt
        avl_update(node);

        // Check balance: fix if |left_height - right_height| == 2
        uint32_t l = avl_height(node->left);
        uint32_t r = avl_height(node->right);

        if (l == r + 2) {
            *from = avl_fix_left(node);   // left too tall
        } else if (l + 2 == r) {
            *from = avl_fix_right(node);  // right too tall
        }

        // If we're at the root, we're done
        if (!parent) {
            return *from;
        }

        // Continue fixing upward
        node = parent;
    }
}

/**
 * avl_del - Remove a node from the tree
 *
 * Three cases:
 * 1. No children: just detach
 * 2. One child: replace node with child
 * 3. Two children: find in-order successor, swap, delete successor
 *
 * Returns the new root of the tree.
 */

// Helper: detach a node that has at most ONE child
static AVLNode *avl_del_easy(AVLNode *node) {
    assert(!node->left || !node->right);

    // The one surviving child (or NULL if leaf)
    AVLNode *child  = node->left ? node->left : node->right;
    AVLNode *parent = node->parent;

    // Child inherits node's parent
    if (child) child->parent = parent;

    if (!parent) {
        // node was root — child becomes new root
        return child;
    }

    // Attach child to grandparent
    AVLNode **from = (parent->left == node) ? &parent->left : &parent->right;
    *from = child;

    // Fix balance from parent upward
    return avl_fix(parent);
}

AVLNode *avl_del(AVLNode *node) {
    // Easy cases: 0 or 1 child
    if (!node->left || !node->right) {
        return avl_del_easy(node);
    }

    // Hard case: 2 children
    // Find in-order successor: leftmost node in right subtree
    // Successor has no left child (it's the minimum of right subtree)
    AVLNode *victim = node->right;
    while (victim->left) {
        victim = victim->left;
    }

    // Detach the successor (it has at most 1 child — its right child)
    AVLNode *root = avl_del_easy(victim);

    // Put successor in node's place — copy all of node's links
    *victim = *node;

    // Fix victim's children to point back to victim (not the deleted node)
    if (victim->left)  victim->left->parent  = victim;
    if (victim->right) victim->right->parent = victim;

    // Attach victim to node's parent
    AVLNode **from = &root;
    AVLNode  *parent = node->parent;
    if (parent) {
        from = (parent->left == node) ? &parent->left : &parent->right;
    }
    *from = victim;

    return root;
}

/**
 * avl_offset - Navigate to node at (current_rank + offset)
 *
 * This enables O(log n) rank queries and range scans.
 * Uses cnt fields to navigate without scanning linearly.
 *
 * `offset` = 0: return node itself
 * `offset` = 1: return next node (in-order successor)
 * `offset` = -1: return previous node
 *
 * INTERVIEW: "avl_offset enables ZRANGE queries. To get nodes
 * ranked 3rd to 7th, I find the 3rd node using avl_offset
 * and then call avl_offset(node, 1) four more times.
 * Each call is O(log n) using the cnt field for navigation,
 * giving O(k log n) total for k results."
 */
AVLNode *avl_offset(AVLNode *node, int64_t offset) {
    int64_t pos = 0;    // current position relative to start node

    while (offset != pos) {
        if (pos < offset && pos + (int64_t)avl_cnt(node->right) >= offset) {
            // Target is in right subtree
            node = node->right;
            pos += avl_cnt(node->left) + 1;
        } else if (pos > offset && pos - (int64_t)avl_cnt(node->left) <= offset) {
            // Target is in left subtree
            node = node->left;
            pos -= avl_cnt(node->right) + 1;
        } else {
            // Must go to parent
            AVLNode *parent = node->parent;
            if (!parent) return NULL;   // offset out of range

            if (parent->right == node) {
                pos -= avl_cnt(node->left) + 1;
            } else {
                pos += avl_cnt(node->right) + 1;
            }
            node = parent;
        }
    }
    return node;
}
