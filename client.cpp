/**
 * MyRedis v2 Client
 *
 * BUILD: g++ -Wall -Wextra -O2 -std=c++17 -o client client.cpp
 *
 * USAGE:
 *   ./client set foo bar
 *   ./client get foo
 *   ./client expire foo 5000     <- expire in 5 seconds
 *   ./client ttl foo             <- check remaining TTL
 *   ./client keys
 *   ./client del foo
 *   ./client get foo             <- returns nil after expiry
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <string>
#include <vector>

const size_t   k_max_msg   = 4096;
const uint16_t SERVER_PORT = 6379;

// Response type tags (must match server)
enum {
    TAG_NIL = 0,
    TAG_ERR = 1,
    TAG_STR = 2,
    TAG_INT = 3,
    TAG_ARR = 5,
};

static void die(const char *m) {
    fprintf(stderr, "[errno:%d] %s\n", errno, m);
    abort();
}

static int32_t read_full(int fd, char *buf, size_t n) {
    while (n > 0) {
        ssize_t rv = read(fd, buf, n);
        if (rv <= 0) return -1;
        n -= (size_t)rv; buf += rv;
    }
    return 0;
}

static int32_t write_all(int fd, const char *buf, size_t n) {
    while (n > 0) {
        ssize_t rv = write(fd, buf, n);
        if (rv <= 0) return -1;
        n -= (size_t)rv; buf += rv;
    }
    return 0;
}

// Read a uint32 from a byte buffer, advance pointer
static bool read_u32(const uint8_t *&p, const uint8_t *end, uint32_t &out) {
    if (p + 4 > end) return false;
    memcpy(&out, p, 4);
    p += 4;
    return true;
}

// Read an int64 from a byte buffer, advance pointer
static bool read_i64(const uint8_t *&p, const uint8_t *end, int64_t &out) {
    if (p + 8 > end) return false;
    memcpy(&out, p, 8);
    p += 8;
    return true;
}

/**
 * print_response - Recursively print a serialized response
 *
 * Handles the typed response format:
 *   TAG_NIL: print "(nil)"
 *   TAG_STR: print quoted string
 *   TAG_INT: print integer
 *   TAG_ERR: print error code + message
 *   TAG_ARR: print each element recursively
 */
static void print_response(const uint8_t *&p, const uint8_t *end, int depth=0) {
    auto indent = [&]() {
        for (int i = 0; i < depth; i++) printf("  ");
    };

    if (p >= end) {
        printf("(error: truncated response)\n");
        return;
    }

    uint8_t tag = *p++;

    switch (tag) {
    case TAG_NIL:
        indent();
        printf("(nil)\n");
        break;

    case TAG_STR: {
        uint32_t len = 0;
        if (!read_u32(p, end, len) || p + len > end) {
            printf("(error: bad string)\n");
            return;
        }
        indent();
        printf("\"%.*s\"\n", (int)len, (char *)p);
        p += len;
        break;
    }

    case TAG_INT: {
        int64_t val = 0;
        if (!read_i64(p, end, val)) {
            printf("(error: bad int)\n");
            return;
        }
        indent();
        printf("(integer) %lld\n", (long long)val);
        break;
    }

    case TAG_ERR: {
        uint32_t code = 0, msglen = 0;
        if (!read_u32(p, end, code) || !read_u32(p, end, msglen) ||
            p + msglen > end)
        {
            printf("(error: bad error response)\n");
            return;
        }
        indent();
        printf("(error) [%u] %.*s\n", code, (int)msglen, (char *)p);
        p += msglen;
        break;
    }

    case TAG_ARR: {
        uint32_t n = 0;
        if (!read_u32(p, end, n)) {
            printf("(error: bad array)\n");
            return;
        }
        indent();
        printf("%u items:\n", n);
        for (uint32_t i = 0; i < n; i++) {
            printf("%d) ", i + 1);
            print_response(p, end, depth + 1);
        }
        break;
    }

    default:
        printf("(unknown tag: %u)\n", tag);
        break;
    }
}

/**
 * send_req - Encode and send a command to the server
 */
static int32_t send_req(int fd, const std::vector<std::string> &cmd) {
    // Calculate body size: 4 (nargs) + sum(4 + arglen) for each arg
    uint32_t bodylen = 4;
    for (const auto &s : cmd) {
        bodylen += 4 + (uint32_t)s.size();
    }

    if (bodylen > k_max_msg) {
        fprintf(stderr, "Command too long\n");
        return -1;
    }

    char buf[4 + 4096];
    memcpy(buf, &bodylen, 4);                   // total length prefix

    uint32_t nargs = (uint32_t)cmd.size();
    memcpy(buf + 4, &nargs, 4);                 // nargs

    size_t pos = 8;
    for (const auto &s : cmd) {
        uint32_t arglen = (uint32_t)s.size();
        memcpy(buf + pos, &arglen, 4);
        memcpy(buf + pos + 4, s.data(), s.size());
        pos += 4 + s.size();
    }

    return write_all(fd, buf, 4 + bodylen);
}

/**
 * read_res - Read and print the server's response
 */
static int32_t read_res(int fd) {
    // Read 4-byte length header
    uint32_t reslen = 0;
    if (read_full(fd, (char *)&reslen, 4) < 0) {
        fprintf(stderr, "Failed to read response length\n");
        return -1;
    }

    if (reslen > k_max_msg) {
        fprintf(stderr, "Response too long\n");
        return -1;
    }

    // Read response body
    std::vector<uint8_t> buf(reslen);
    if (read_full(fd, (char *)buf.data(), reslen) < 0) {
        fprintf(stderr, "Failed to read response body\n");
        return -1;
    }

    // Print typed response
    const uint8_t *p   = buf.data();
    const uint8_t *end = buf.data() + reslen;
    print_response(p, end);

    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <command> [args...]\n\n", argv[0]);
        fprintf(stderr, "Commands:\n");
        fprintf(stderr, "  set <key> <value>\n");
        fprintf(stderr, "  get <key>\n");
        fprintf(stderr, "  del <key>\n");
        fprintf(stderr, "  keys\n");
        fprintf(stderr, "  expire <key> <ms>\n");
        fprintf(stderr, "  ttl <key>\n");
        return 1;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) die("socket()");

    struct sockaddr_in addr = {};
    addr.sin_family      = AF_INET;
    addr.sin_port        = ntohs(SERVER_PORT);
    addr.sin_addr.s_addr = ntohl(INADDR_LOOPBACK);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        die("connect() - is server running?");

    std::vector<std::string> cmd;
    for (int i = 1; i < argc; i++) cmd.push_back(argv[i]);

    if (send_req(fd, cmd) < 0) {
        fprintf(stderr, "Failed to send\n");
        close(fd);
        return 1;
    }

    int32_t err = read_res(fd);
    close(fd);
    return err ? 1 : 0;
}
