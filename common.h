#pragma once

#include <stdint.h>
#include <stddef.h>

/**
 * container_of - Get pointer to containing struct from pointer to member
 *
 * CONCEPT: Intrusive data structures
 * Instead of storing a pointer TO the data inside the hash node,
 * we EMBED the hash node INSIDE the data struct.
 *
 * Example:
 *   struct Entry {
 *       HNode node;    <- embedded hash node
 *       string key;
 *       string value;
 *   };
 *
 * The hash table stores HNode pointers.
 * To get back to the Entry, we subtract the offset of `node`
 * from the HNode pointer. That gives us the Entry pointer.
 *
 * WHY: Avoids a separate heap allocation per node.
 * No pointer indirection. Better cache locality.
 * This is how Linux kernel linked lists work.
 *
 * USAGE: Entry *e = container_of(hnode, Entry, node);
 */
#define container_of(ptr, type, member) \
    ( (type *)( (char *)(ptr) - offsetof(type, member) ) )

/**
 * FNV-1a Hash Function
 *
 * FNV = Fowler-Noll-Vo hash. A fast, simple non-cryptographic hash.
 * Used to map string keys to hash table bucket indices.
 *
 * Properties:
 * - O(n) time, n = string length
 * - Good distribution (minimal collisions)
 * - Not cryptographically secure (not needed for a cache)
 *
 * The magic numbers (0x811C9DC5, 0x01000193) are FNV prime constants.
 * They're chosen to minimize collisions across common string patterns.
 *
 * INTERVIEW: "I used FNV-1a because it's fast and has good distribution
 * for string keys. For a production system I'd consider xxHash or
 * MurmurHash for better performance on longer strings."
 */
inline uint64_t str_hash(const uint8_t *data, size_t len) {
    uint32_t h = 0x811C9DC5;   // FNV offset basis
    for (size_t i = 0; i < len; i++) {
        h = (h + data[i]) * 0x01000193;  // FNV prime
    }
    return h;
}
