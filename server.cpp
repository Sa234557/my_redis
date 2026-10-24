/**
 * ============================================================
 * MyRedis v2 - Complete Redis Clone
 * ============================================================
 * Commands: GET SET DEL KEYS EXPIRE TTL ZADD ZSCORE ZRANK ZRANGE ZREM
 * Features: Custom hash table, progressive rehashing, TTL min-heap,
 *           AVL-tree sorted sets (ZSet)
 *
 * BUILD:
 *   g++ -Wall -Wextra -O2 -std=c++17 -o server \
 *       server.cpp hashtable.cpp heap.cpp avl.cpp zset.cpp
 * ============================================================
 */

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <string>
#include <vector>
#include "common.h"
#include "hashtable.h"
#include "heap.h"
#include "zset.h"

// ============================================================
// CONSTANTS
// ============================================================
const size_t   k_max_msg   = 4096;
const uint16_t SERVER_PORT = 6379;
const size_t   k_max_args  = 200;

// ============================================================
// RESPONSE SERIALIZATION
// ============================================================
enum { TAG_NIL=0, TAG_ERR=1, TAG_STR=2, TAG_INT=3, TAG_DBL=4, TAG_ARR=5 };
enum { ERR_UNKNOWN=1, ERR_BAD_ARG=2, ERR_BAD_TYP=3 };

typedef std::vector<uint8_t> Buffer;

static void buf_u8 (Buffer &b, uint8_t  v) { b.push_back(v); }
static void buf_u32(Buffer &b, uint32_t v) { b.insert(b.end(),(uint8_t*)&v,(uint8_t*)&v+4); }
static void buf_i64(Buffer &b, int64_t  v) { b.insert(b.end(),(uint8_t*)&v,(uint8_t*)&v+8); }
static void buf_dbl(Buffer &b, double   v) { b.insert(b.end(),(uint8_t*)&v,(uint8_t*)&v+8); }
static void buf_raw(Buffer &b, const uint8_t *d, size_t n) { b.insert(b.end(),d,d+n); }

static void out_nil(Buffer &o) { buf_u8(o,TAG_NIL); }
static void out_int(Buffer &o, int64_t v) { buf_u8(o,TAG_INT); buf_i64(o,v); }
static void out_dbl(Buffer &o, double  v) { buf_u8(o,TAG_DBL); buf_dbl(o,v); }
static void out_str(Buffer &o, const char *s, size_t n) {
    buf_u8(o,TAG_STR); buf_u32(o,(uint32_t)n); buf_raw(o,(const uint8_t*)s,n);
}
static void out_err(Buffer &o, uint32_t code, const std::string &msg) {
    buf_u8(o,TAG_ERR); buf_u32(o,code);
    buf_u32(o,(uint32_t)msg.size()); buf_raw(o,(const uint8_t*)msg.data(),msg.size());
}
static void out_arr(Buffer &o, uint32_t n) { buf_u8(o,TAG_ARR); buf_u32(o,n); }

// ============================================================
// ENTRY - One key-value pair (string or zset)
// ============================================================
enum EntryType { T_STR=0, T_ZSET=1 };

struct Entry {
    HNode       node;
    std::string key;
    EntryType   type    = T_STR;
    std::string val;        // used when type == T_STR
    ZSet       *zset = NULL; // used when type == T_ZSET
    size_t      heap_idx = (size_t)-1;  // TTL heap position
};

struct LookupKey {
    HNode node;
    std::string key;
};

static bool entry_eq(HNode *node, HNode *key) {
    Entry     *e  = container_of(node, Entry, node);
    LookupKey *lk = container_of(key,  LookupKey, node);
    return e->key == lk->key;
}

// ============================================================
// GLOBAL STATE
// ============================================================
static struct {
    HMap db;
    std::vector<HeapItem> heap;
} g;

// ============================================================
// TTL HELPERS
// ============================================================
static uint64_t get_monotonic_ms() {
    struct timespec tv = {0,0};
    clock_gettime(CLOCK_MONOTONIC, &tv);
    return (uint64_t)tv.tv_sec*1000 + tv.tv_nsec/1000000;
}

static void heap_delete(std::vector<HeapItem> &a, size_t pos) {
    a[pos] = a.back(); a.pop_back();
    if (pos < a.size()) { *a[pos].ref = pos; heap_update(a.data(),pos,a.size()); }
}

static void heap_upsert(std::vector<HeapItem> &a, size_t pos, HeapItem item) {
    if (pos < a.size()) a[pos] = item;
    else { pos = a.size(); a.push_back(item); }
    heap_update(a.data(), pos, a.size());
}

static void entry_set_ttl(Entry *ent, int64_t ttl_ms) {
    if (ttl_ms < 0) {
        if (ent->heap_idx != (size_t)-1) {
            heap_delete(g.heap, ent->heap_idx);
            ent->heap_idx = (size_t)-1;
        }
    } else {
        uint64_t expire_at = get_monotonic_ms() + (uint64_t)ttl_ms;
        HeapItem item = {expire_at, &ent->heap_idx};
        heap_upsert(g.heap, ent->heap_idx, item);
    }
}

static void entry_del(Entry *ent) {
    entry_set_ttl(ent, -1);
    if (ent->type == T_ZSET && ent->zset) {
        zset_clear(ent->zset);
        delete ent->zset;
    }
    delete ent;
}

static void process_ttl_expiry() {
    uint64_t now = get_monotonic_ms();
    while (!g.heap.empty() && g.heap[0].val <= now) {
        Entry *ent = container_of(g.heap[0].ref, Entry, heap_idx);
        LookupKey lk;
        lk.key = ent->key;
        lk.node.hcode = str_hash((uint8_t*)lk.key.data(), lk.key.size());
        HNode *node = hm_delete(&g.db, &lk.node, &entry_eq);
        assert(node == &ent->node);
        entry_del(ent);
    }
}

// ============================================================
// LOOKUP HELPER
// ============================================================
static Entry *db_lookup(const std::string &key) {
    LookupKey lk;
    lk.key = key;
    lk.node.hcode = str_hash((uint8_t*)key.data(), key.size());
    HNode *node = hm_lookup(&g.db, &lk.node, &entry_eq);
    return node ? container_of(node, Entry, node) : NULL;
}

// ============================================================
// STRING COMMANDS
// ============================================================
static void do_get(std::vector<std::string> &cmd, Buffer &out) {
    if (cmd.size() != 2) return out_err(out, ERR_BAD_ARG, "GET takes 1 arg");
    Entry *ent = db_lookup(cmd[1]);
    if (!ent) return out_nil(out);
    if (ent->type != T_STR) return out_err(out, ERR_BAD_TYP, "WRONGTYPE: not a string");
    // Lazy TTL check
    if (ent->heap_idx != (size_t)-1 && g.heap[ent->heap_idx].val <= get_monotonic_ms()) {
        LookupKey lk; lk.key=ent->key;
        lk.node.hcode = str_hash((uint8_t*)lk.key.data(),lk.key.size());
        hm_delete(&g.db,&lk.node,&entry_eq);
        entry_del(ent);
        return out_nil(out);
    }
    out_str(out, ent->val.data(), ent->val.size());
}

static void do_set(std::vector<std::string> &cmd, Buffer &out) {
    if (cmd.size() != 3) return out_err(out, ERR_BAD_ARG, "SET takes 2 args");
    Entry *ent = db_lookup(cmd[1]);
    if (ent) {
        if (ent->type != T_STR) return out_err(out,ERR_BAD_TYP,"WRONGTYPE");
        ent->val = cmd[2];
        entry_set_ttl(ent, -1);
    } else {
        ent = new Entry();
        ent->key = cmd[1];
        ent->val = cmd[2];
        ent->type = T_STR;
        ent->node.hcode = str_hash((uint8_t*)ent->key.data(), ent->key.size());
        hm_insert(&g.db, &ent->node);
    }
    out_nil(out);
}

static void do_del(std::vector<std::string> &cmd, Buffer &out) {
    if (cmd.size() != 2) return out_err(out, ERR_BAD_ARG, "DEL takes 1 arg");
    LookupKey lk; lk.key=cmd[1];
    lk.node.hcode = str_hash((uint8_t*)lk.key.data(),lk.key.size());
    HNode *node = hm_delete(&g.db,&lk.node,&entry_eq);
    if (node) { entry_del(container_of(node,Entry,node)); return out_int(out,1); }
    out_int(out,0);
}

static bool cb_keys(HNode *node, void *arg) {
    Buffer &out = *(Buffer*)arg;
    out_str(out, container_of(node,Entry,node)->key.data(),
                 container_of(node,Entry,node)->key.size());
    return true;
}
static void do_keys(std::vector<std::string> &, Buffer &out) {
    out_arr(out,(uint32_t)hm_size(&g.db));
    hm_foreach(&g.db,&cb_keys,(void*)&out);
}

static void do_expire(std::vector<std::string> &cmd, Buffer &out) {
    if (cmd.size()!=3) return out_err(out,ERR_BAD_ARG,"EXPIRE takes 2 args");
    char *ep=NULL; int64_t ttl=strtoll(cmd[2].c_str(),&ep,10);
    if (ep!=cmd[2].c_str()+cmd[2].size()||ttl<0)
        return out_err(out,ERR_BAD_ARG,"TTL must be non-negative integer");
    Entry *ent=db_lookup(cmd[1]);
    if (!ent) return out_int(out,0);
    entry_set_ttl(ent,ttl);
    out_int(out,1);
}

static void do_ttl(std::vector<std::string> &cmd, Buffer &out) {
    if (cmd.size()!=2) return out_err(out,ERR_BAD_ARG,"TTL takes 1 arg");
    Entry *ent=db_lookup(cmd[1]);
    if (!ent) return out_int(out,-2);
    if (ent->heap_idx==(size_t)-1) return out_int(out,-1);
    uint64_t exp=g.heap[ent->heap_idx].val, now=get_monotonic_ms();
    out_int(out,(int64_t)(exp>now?exp-now:0));
}

// ============================================================
// ZSET COMMANDS
// ============================================================

/**
 * do_zadd - ZADD <key> <score> <member>
 *
 * Adds member with score to the sorted set at key.
 * Creates the key if it doesn't exist.
 * Updates score if member already exists.
 * Returns 1 if new member added, 0 if score updated.
 *
 * INTERVIEW: "ZADD first looks up the key in O(1) via hash map.
 * If the member exists, it detaches from the AVL tree, updates
 * the score, and reinserts in O(log n). If new, it allocates a
 * ZNode embedding both HNode and AVLNode, then inserts into
 * both structures in O(log n)."
 */
static void do_zadd(std::vector<std::string> &cmd, Buffer &out) {
    if (cmd.size()!=4) return out_err(out,ERR_BAD_ARG,"ZADD takes 3 args: key score member");
    char *ep=NULL;
    double score=strtod(cmd[2].c_str(),&ep);
    if (ep!=cmd[2].c_str()+cmd[2].size())
        return out_err(out,ERR_BAD_ARG,"Score must be a number");

    // Get or create the ZSet entry
    Entry *ent=db_lookup(cmd[1]);
    if (!ent) {
        ent = new Entry();
        ent->key  = cmd[1];
        ent->type = T_ZSET;
        ent->zset = new ZSet();
        ent->node.hcode = str_hash((uint8_t*)ent->key.data(),ent->key.size());
        hm_insert(&g.db,&ent->node);
    } else if (ent->type!=T_ZSET) {
        return out_err(out,ERR_BAD_TYP,"WRONGTYPE: key is not a sorted set");
    }

    const std::string &name=cmd[3];
    bool inserted = zset_insert(ent->zset,name.data(),name.size(),score);
    out_int(out, inserted ? 1 : 0);
}

/**
 * do_zscore - ZSCORE <key> <member>
 *
 * Returns the score of member in the sorted set.
 * O(1) via the embedded hash map.
 */
static void do_zscore(std::vector<std::string> &cmd, Buffer &out) {
    if (cmd.size()!=3) return out_err(out,ERR_BAD_ARG,"ZSCORE takes 2 args");
    Entry *ent=db_lookup(cmd[1]);
    if (!ent||ent->type!=T_ZSET) return out_nil(out);
    const std::string &name=cmd[2];
    ZNode *zn=zset_lookup(ent->zset,name.data(),name.size());
    if (!zn) return out_nil(out);
    out_dbl(out,zn->score);
}

/**
 * do_zrank - ZRANK <key> <member>
 *
 * Returns 0-based rank of member (position in sorted order).
 * Uses avl_offset to count nodes before this one: O(log n).
 *
 * INTERVIEW: "ZRANK first finds the node in O(1) via hash map.
 * Then uses the AVL cnt field: rank = cnt(left subtree).
 * We use avl_offset(node, -rank) to verify, actually we compute
 * rank directly from cnt fields walking up the tree."
 */
static void do_zrank(std::vector<std::string> &cmd, Buffer &out) {
    if (cmd.size()!=3) return out_err(out,ERR_BAD_ARG,"ZRANK takes 2 args");
    Entry *ent=db_lookup(cmd[1]);
    if (!ent||ent->type!=T_ZSET) return out_nil(out);
    const std::string &name=cmd[2];
    ZNode *zn=zset_lookup(ent->zset,name.data(),name.size());
    if (!zn) return out_nil(out);
    // Rank = number of nodes with smaller (score, name) = left subtree cnt
    // Navigate from node to root, accumulating rank
    size_t rank = avl_cnt(zn->tree.left);
    AVLNode *node = &zn->tree;
    while (node->parent) {
        if (node->parent->right == node) {
            rank += avl_cnt(node->parent->left) + 1;
        }
        node = node->parent;
    }
    out_int(out,(int64_t)rank);
}

/**
 * do_zrange - ZRANGE <key> <start> <stop>
 *
 * Returns members at rank positions [start, stop] inclusive.
 * Uses AVL tree's avl_offset for O(log n) navigation between nodes.
 *
 * INTERVIEW: "ZRANGE seeks to the start rank node in O(log n)
 * using avl_offset from the minimum node, then iterates forward
 * using avl_offset(node, 1) — each step is O(log n) — giving
 * O((stop-start) * log n) total. For k results: O(k log n)."
 */
static void do_zrange(std::vector<std::string> &cmd, Buffer &out) {
    if (cmd.size()!=4) return out_err(out,ERR_BAD_ARG,"ZRANGE takes 3 args: key start stop");
    char *ep1=NULL, *ep2=NULL;
    int64_t start=strtoll(cmd[2].c_str(),&ep1,10);
    int64_t stop =strtoll(cmd[3].c_str(),&ep2,10);
    if (ep1!=cmd[2].c_str()+cmd[2].size()||ep2!=cmd[3].c_str()+cmd[3].size())
        return out_err(out,ERR_BAD_ARG,"Start and stop must be integers");

    Entry *ent=db_lookup(cmd[1]);
    if (!ent||ent->type!=T_ZSET) { out_arr(out,0); return; }

    int64_t sz=(int64_t)hm_size(&ent->zset->hmap);
    // Handle negative indices (Python-style: -1 = last)
    if (start<0) start+=sz;
    if (stop <0) stop +=sz;
    if (start<0) start=0;
    if (stop>=sz) stop=sz-1;
    if (start>stop) { out_arr(out,0); return; }

    // Find the minimum node (leftmost in AVL tree)
    AVLNode *cur=ent->zset->root;
    if (!cur) { out_arr(out,0); return; }
    while (cur->left) cur=cur->left;
    ZNode *zn=container_of(cur,ZNode,tree);

    // Navigate to start rank
    zn=znode_offset(zn,(int64_t)start);
    if (!zn) { out_arr(out,0); return; }

    // Collect [start, stop] inclusive
    uint32_t count=(uint32_t)(stop-start+1);
    out_arr(out,count*2);  // return (member, score) pairs
    for (uint32_t i=0; i<count && zn; i++) {
        out_str(out,zn->name,zn->len);
        out_dbl(out,zn->score);
        zn=znode_offset(zn,1);
    }
}

/**
 * do_zrem - ZREM <key> <member>
 *
 * Remove member from sorted set.
 * Returns 1 if removed, 0 if not found.
 */
static void do_zrem(std::vector<std::string> &cmd, Buffer &out) {
    if (cmd.size()!=3) return out_err(out,ERR_BAD_ARG,"ZREM takes 2 args");
    Entry *ent=db_lookup(cmd[1]);
    if (!ent||ent->type!=T_ZSET) return out_int(out,0);
    const std::string &name=cmd[2];
    ZNode *zn=zset_lookup(ent->zset,name.data(),name.size());
    if (!zn) return out_int(out,0);
    zset_delete(ent->zset,zn);
    out_int(out,1);
}

// ============================================================
// PROTOCOL
// ============================================================
static void msg(const char *m) { fprintf(stderr,"%s\n",m); }
static void die(const char *m) { fprintf(stderr,"[%d] %s\n",errno,m); abort(); }

static int32_t read_full(int fd, char *buf, size_t n) {
    while (n>0) { ssize_t rv=read(fd,buf,n); if(rv<=0)return -1; n-=rv; buf+=rv; }
    return 0;
}
static int32_t write_all(int fd, const char *buf, size_t n) {
    while (n>0) { ssize_t rv=write(fd,buf,n); if(rv<=0)return -1; n-=rv; buf+=rv; }
    return 0;
}

static int32_t parse_request(
    const uint8_t *data, size_t len, std::vector<std::string> &out)
{
    if (len<4) return -1;
    uint32_t nargs=0; memcpy(&nargs,data,4); data+=4; len-=4;
    if (nargs>k_max_args) return -1;
    while (out.size()<nargs) {
        if (len<4) return -1;
        uint32_t slen=0; memcpy(&slen,data,4); data+=4; len-=4;
        if (len<slen) return -1;
        out.push_back(std::string((char*)data,slen)); data+=slen; len-=slen;
    }
    return (len==0)?0:-1;
}

static void dispatch(std::vector<std::string> &cmd, Buffer &out) {
    process_ttl_expiry();
    if (cmd.empty()) return out_err(out,ERR_BAD_ARG,"empty command");
    const std::string &c=cmd[0];
    if      (c=="get")    do_get(cmd,out);
    else if (c=="set")    do_set(cmd,out);
    else if (c=="del")    do_del(cmd,out);
    else if (c=="keys")   do_keys(cmd,out);
    else if (c=="expire") do_expire(cmd,out);
    else if (c=="ttl")    do_ttl(cmd,out);
    else if (c=="zadd")   do_zadd(cmd,out);
    else if (c=="zscore") do_zscore(cmd,out);
    else if (c=="zrank")  do_zrank(cmd,out);
    else if (c=="zrange") do_zrange(cmd,out);
    else if (c=="zrem")   do_zrem(cmd,out);
    else out_err(out,ERR_UNKNOWN,"Unknown: "+cmd[0]+
        " (try: get set del keys expire ttl zadd zscore zrank zrange zrem)");
}

static int32_t process_request(int connfd) {
    char hdr[4];
    if (read_full(connfd,hdr,4)<0) return -1;
    uint32_t msglen=0; memcpy(&msglen,hdr,4);
    if (msglen>k_max_msg) { msg("msg too long"); return -1; }
    std::vector<uint8_t> rbuf(msglen);
    if (read_full(connfd,(char*)rbuf.data(),msglen)<0) return -1;
    std::vector<std::string> cmd;
    if (parse_request(rbuf.data(),msglen,cmd)<0) { msg("bad request"); return -1; }
    Buffer out;
    dispatch(cmd,out);
    uint32_t outlen=(uint32_t)out.size();
    if (write_all(connfd,(char*)&outlen,4)<0) return -1;
    if (write_all(connfd,(char*)out.data(),outlen)<0) return -1;
    return 0;
}

// ============================================================
// MAIN
// ============================================================
int main() {
    int fd=socket(AF_INET,SOCK_STREAM,0);
    if (fd<0) die("socket()");
    int val=1; setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&val,sizeof(val));
    struct sockaddr_in addr={};
    addr.sin_family=AF_INET; addr.sin_port=ntohs(SERVER_PORT);
    addr.sin_addr.s_addr=ntohl(0);
    if (bind(fd,(struct sockaddr*)&addr,sizeof(addr))<0) die("bind()");
    if (listen(fd,SOMAXCONN)<0) die("listen()");
    fprintf(stderr,"MyRedis v2 on :%d\n",SERVER_PORT);
    fprintf(stderr,"String: get set del keys expire ttl\n");
    fprintf(stderr,"ZSet:   zadd zscore zrank zrange zrem\n\n");
    while (true) {
        struct sockaddr_in cl={}; socklen_t al=sizeof(cl);
        int connfd=accept(fd,(struct sockaddr*)&cl,&al);
        if (connfd<0) continue;
        fprintf(stderr,"Client connected\n");
        while (process_request(connfd)==0) {}
        close(connfd);
        fprintf(stderr,"Client disconnected\n");
    }
    return 0;
}
