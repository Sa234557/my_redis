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
