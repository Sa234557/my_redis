#include "heap.h"

// ============================================================
// INDEX ARITHMETIC
// Array-based binary tree navigation
// ============================================================

// Parent of node i: floor((i-1)/2)
static size_t heap_parent(size_t i) { return (i + 1) / 2 - 1; }

// Left child of node i: 2i + 1
static size_t heap_left(size_t i)   { return i * 2 + 1; }

// Right child of node i: 2i + 2
static size_t heap_right(size_t i)  { return i * 2 + 2; }

// ============================================================
// SIFT UP (bubble up)
// Used after inserting at the end
// Move node up until parent <= node (min-heap property)
// ============================================================

/**
 * heap_up - Move element at pos upward to its correct position
 *
 * Called after: inserting a new element (it goes to end, then bubbles up)
 * or after: decreasing a value (it might need to go up)
 *
 * Algorithm:
 *   Save element at pos
 *   While pos > 0 and parent > element:
 *     Move parent DOWN to pos  (not a swap — saves one assignment)
 *     Update parent's back-reference
 *     Move pos up to parent
 *   Place saved element at final pos
 *   Update its back-reference
 *
 * WHY update ref? When we move an item to a new position,
 * the Entry that owns it needs to know the new position.
 * HeapItem.ref points to Entry::heap_idx, so we write through it.
 */
static void heap_up(HeapItem *a, size_t pos) {
    HeapItem t = a[pos];    // save the item to be moved up

    while (pos > 0 && a[heap_parent(pos)].val > t.val) {
        // Parent is larger than us — swap parent down
        a[pos] = a[heap_parent(pos)];
        *a[pos].ref = pos;          // update parent's back-reference
        pos = heap_parent(pos);     // move up
    }

    // Found the right spot — place our item here
    a[pos] = t;
    *a[pos].ref = pos;
}

// ============================================================
// SIFT DOWN (bubble down)
// Used after removing root (replace root with last element)
// Move node down until node <= both children (min-heap property)
// ============================================================

/**
 * heap_down - Move element at pos downward to its correct position
 *
 * Called after: removing the minimum (swap root with last, sift down root)
 * or after: increasing a value (it might need to go down)
 *
 * Algorithm:
 *   Save element at pos
 *   While pos has children smaller than element:
 *     Find smallest child
 *     Move smallest child UP to pos
 *     Update its back-reference
 *     Move pos down to smallest child's old position
 *   Place saved element at final pos
 *   Update its back-reference
 */
static void heap_down(HeapItem *a, size_t pos, size_t len) {
    HeapItem t = a[pos];    // save the item to be moved down

    while (true) {
        size_t l = heap_left(pos);
        size_t r = heap_right(pos);

        // Find the minimum among pos, left child, right child
        size_t   min_pos = pos;
        uint64_t min_val = t.val;

        if (l < len && a[l].val < min_val) {
            min_pos = l;
            min_val = a[l].val;
        }
        if (r < len && a[r].val < min_val) {
            min_pos = r;
            // min_val = a[r].val;  -- not needed, just need min_pos
        }

        // If we're already smaller than both children, stop
        if (min_pos == pos) {
            break;
        }

        // Move the smaller child UP to pos
        a[pos] = a[min_pos];
        *a[pos].ref = pos;      // update its back-reference
        pos = min_pos;          // continue from child's position
    }

    // Place our item at its final position
    a[pos] = t;
    *a[pos].ref = pos;
}

// ============================================================
// PUBLIC API
// ============================================================

/**
 * heap_update - Fix heap property after a[pos] changed
 *
 * This single function handles both cases:
 * - Value decreased (or new insert): might need to go UP
 * - Value increased: might need to go DOWN
 *
 * Check by comparing with parent:
 * - If parent > us: go up (violates min-heap)
 * - Otherwise: go down (might violate min-heap with children)
 */
void heap_update(HeapItem *a, size_t pos, size_t len) {
    if (pos > 0 && a[heap_parent(pos)].val > a[pos].val) {
        heap_up(a, pos);
    } else {
        heap_down(a, pos, len);
    }
}
