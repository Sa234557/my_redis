# MyRedis — Redis Clone in C++

A Redis clone built from scratch in C++ to understand how Redis works internally.

## Features
- Custom hash table with progressive rehashing — no latency spikes on resize
- TTL-based key expiry using a min-heap with back-references for O(log n) removal
- AVL tree-based sorted sets (ZSet) with O(log n) rank queries using subtree counts
- Typed binary serialisation protocol
- 11 commands across string and sorted set types

## Commands
| Command | Description |
|---------|-------------|
| `get <key>` | Get string value |
| `set <key> <value>` | Set string value |
| `del <key>` | Delete key |
| `keys` | List all keys |
| `expire <key> <ms>` | Set TTL in milliseconds |
| `ttl <key>` | Get remaining TTL |
| `zadd <key> <score> <member>` | Add to sorted set |
| `zscore <key> <member>` | Get score by member |
| `zrank <key> <member>` | Get rank of member |
| `zrange <key> <start> <stop>` | Get members by rank range |
| `zrem <key> <member>` | Remove from sorted set |

## Architecture
```
┌─────────────────────────────────────────────────────┐
│                    server.cpp                        │
│  TCP Server → Protocol Parser → Command Dispatcher  │
└──────────┬──────────────────────────────────────────┘
           │
    ┌──────┴──────────────────┐
    │                         │
┌───▼────────────┐    ┌───────▼──────────┐
│  String Store  │    │   Sorted Set      │
│  hashtable.h   │    │   zset.h          │
│  O(1) avg GET  │    │   avl.h (AVL BST) │
│  Progressive   │    │   O(log n) ZADD   │
│  rehashing     │    │   O(1) ZSCORE     │
└───────┬────────┘    └───────┬──────────┘
        │                     │
┌───────▼─────────────────────▼──────────┐
│              heap.h                     │
│     Min-heap for TTL key expiry         │
│     O(log n) insert/delete              │
│     O(1) find next expiry               │
└─────────────────────────────────────────┘
```

## Build and Run
```bash
# Build
make

# Terminal 1 — start server
./server

# Terminal 2 — test commands
./client set name shreenija
./client get name
./client zadd leaderboard 95.5 shreenija
./client zadd leaderboard 88.0 alice
./client zadd leaderboard 92.3 bob
./client zrange leaderboard 0 -1
./client zrank leaderboard shreenija
./client expire name 5000
./client ttl name
```

## Key Design Decisions
- **Progressive rehashing**: Two hash tables maintained simultaneously during resize. Fixed batch migrated per operation — bounds latency to O(1) per request instead of O(n) spike.
- **Intrusive data structures**: HNode and AVLNode embedded directly inside Entry/ZNode structs. No separate allocation, better cache locality.
- **Min-heap with back-references**: Each heap item holds a pointer to the entry's heap_idx. When heap moves items, it updates heap_idx via the pointer — enabling O(log n) removal without scanning.
- **AVL cnt field**: Each node stores subtree size. Enables O(log n) rank queries by accumulating left subtree counts while walking to root.
