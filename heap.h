#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * ============================================================
 * MIN-HEAP FOR TTL (Time-To-Live) KEY EXPIRY
 * ============================================================
 *
 * PROBLEM: Keys can have expiry times (SET key val EX 5 means
 * delete after 5 seconds). We need to efficiently find which
 * key expires soonest.
 *
 * DATA STRUCTURE: Min-Heap
 * A binary tree where every parent <= its children.
 * The minimum value is always at the root (index 0).
 * Perfect for "what expires next?" — always O(1) to find.
 *
 * STORED AS ARRAY (not pointers):
 * Index 0 = root (minimum)
 * For node at index i:
 *   left child  = 2i + 1
 *   right child = 2i + 2
 *   parent      = (i-1) / 2
 *
 * OPERATIONS:
 * - Insert: add to end, bubble up   O(log n)
 * - Delete min: replace root with last, bubble down  O(log n)
 * - Peek min: array[0]  O(1)
 *
 * CHALLENGE: When a key is deleted, we need to remove its
 * heap entry. But how do we find it in the heap?
 *
 * SOLUTION: Each entry stores a back-reference:
 *   HeapItem.ref = &entry->heap_idx
 * When the heap moves an item, it updates heap_idx via ref.
 * So entry->heap_idx always tells us where in the heap this entry is.
 * ============================================================
 */

struct HeapItem {
    uint64_t val = 0;   // expiry timestamp in milliseconds
    size_t *ref  = NULL; // pointer to Entry::heap_idx (back-reference)
};

/**
 * heap_update - Restore heap property after changing a[pos]
 *
 * After inserting or updating a value, the heap property may be
 * violated. This function fixes it by moving the item up or down
 * until it's in the right place.
 *
 * Figures out which direction to go by comparing with parent.
 */
void heap_update(HeapItem *a, size_t pos, size_t len);
