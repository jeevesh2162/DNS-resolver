# DNS Recursive Resolver — Interview Prep Notes

---

## 1. The One-Liner Pitch

> "I built a recursive DNS resolver daemon from scratch in C that binds to a UDP port, receives standard DNS queries from clients like `dig`, performs iterative resolution starting from the global Root Servers, caches results with TTL expiry, and responds with RFC 1035–compliant binary packets — supporting both A (IPv4) and AAAA (IPv6) record types."

---

## 2. Project Overview

| Attribute | Detail |
|---|---|
| **Language** | C (C17 standard) |
| **Build** | GNU Make |
| **Transport** | UDP (port 5300 for server, port 53 for upstream queries) |{client request to deamon port 5300 and that doemon will request to the root server port53}
| **Resolution** | Iterative (the daemon does the walking, not the upstream servers) |
| **Record Types** | A (IPv4), AAAA (IPv6), CNAME (aliases), NS (nameservers) |
| **Caching** | Custom hashmap with TTL-based expiry |
| **Binary Protocol** | Full RFC 1035 serialization and deserialization with name compression |

---

## 3. Architecture & Data Flow
dig stands for Domain Information Groper.

It is a command-line tool used to query DNS servers and inspect DNS records.
```
                        ┌─────────────────────┐
                        │   Client (dig/curl)  │
                        └────────┬────────────┘
                                 │ UDP query
                                 ▼
                        ┌─────────────────────┐
                        │  main.c (Daemon)     │
                        │  bind(0.0.0.0:5300)  │
                        │  while(1) recvfrom() │
                        └────────┬────────────┘
                                 │
                    ┌────────────┼────────────┐
                    ▼            ▼            ▼
             ┌──────────┐ ┌──────────┐ ┌──────────────┐
             │dns_packet│ │ resolver │ │    cache     │
             │  parse() │ │  resolve │ │  get/put     │
             │serialize │ │ _domain()│ │  (hashmap)   │
             └──────────┘ └────┬─────┘ └──────────────┘
                               │
                    ┌──────────┼──────────┐
                    ▼          ▼          ▼
             ┌───────────┐ ┌───────┐ ┌──────────┐
             │root_servers│ │network│ │dns_packet│
             │  get_rand()│ │send() │ │  parse() │
             └───────────┘ └───────┘ └──────────┘
                               │
              ┌────────────────┼────────────────┐
              ▼                ▼                 ▼
        Root Server      TLD Server     Authoritative Server
       (198.41.0.4)    (192.5.6.30)    (ns1.google.com)
```

### The Journey of a Query (Step by Step)

1. **Client sends query** → `dig @127.0.0.1 -p 5300 google.com A`
2. **`main.c`** receives the raw UDP bytes via `recvfrom()`
3. **`dns_packet.c` (parse)** deserializes the binary blob into a `DNSMessage` struct
4. **`cache.c`** is checked first — if we have a non-expired cached answer, return immediately
5. **`root_servers.c`** provides a random IPv4 Root Server IP (e.g., `198.41.0.4`)
6. **`resolver.c`** enters a `while(1)` loop:
   - **Iteration 1:** Asks Root Server → gets referral to `.com` TLD servers (NS + glue A records)
   - **Iteration 2:** Asks TLD Server (`192.5.6.30`) → gets referral to Google's authoritative servers
   - **Iteration 3:** Asks `ns1.google.com` → gets the **final answer** (`142.250.190.46`)
7. **`cache.c`** stores the result with its TTL
8. **`dns_packet.c` (serialize)** builds a binary response packet with the answer
9. **`main.c`** sends the response back to the client via `sendto()`

---

## 4. File-by-File Deep Dive

### 4.1 `main.c` — The Server Daemon Loop

**Purpose:** Entry point. Creates a persistent UDP server that listens for DNS queries.

**Key Concepts:**
- `socket(AF_INET, SOCK_DGRAM, 0)` — creates a UDP socket
- `bind()` to `INADDR_ANY:5300` — listens on all network interfaces
- `while(1)` + `recvfrom()` — blocks until a client query arrives
- `sendto()` — sends the response back to the client's IP:port
- `setvbuf(stdout, NULL, _IONBF, 0)` — disables output buffering so logs appear in real-time

**Why port 5300?** Port 53 requires root privileges. Port 5300 is an unprivileged port for development. (On Linux, ports 0–1023 are called privileged ports (or well-known ports).Only processes running as root can bind to them.)

---

### 4.2 `dns_packet.c` / `dns_packet.h` — The Binary Protocol Engine

**Purpose:** Translates between raw binary DNS wire format (RFC 1035) and C structs.

#### Key Data Structures:

```c
typedef struct {
    uint16_t id;       // Transaction ID (match request to response)
    uint16_t flags;    // QR, Opcode, AA, TC, RD, RA, RCODE
    uint16_t qdcount;  // Number of questions
    uint16_t ancount;  // Number of answers
    uint16_t nscount;  // Number of authority records
    uint16_t arcount;  // Number of additional records
} DNSHeader;           // Always 12 bytes

typedef struct {
    char *name;
    uint16_t type;      // A=1, NS=2, CNAME=5, AAAA=28
    uint16_t class;     // IN=1 (Internet)
    uint32_t ttl;       // Time-To-Live in seconds
    uint16_t rdlength;  // Length of rdata
    uint8_t *rdata;     // The actual data (4 bytes for A, 16 for AAAA)
} DNSResourceRecord;
```

#### Key Functions:

| Function | What it does |
|---|---|
| `domain_to_qname()` | Converts `"google.com"` → `\x06google\x03com\x00` (length-prefixed labels) |DNS packets don't store domains as strings.[length][label][length][label][0]
| `parse_domain_name()` | Decodes labels + handles **name compression pointers** (`0xC0xx`) |Convert DNS wire format back to a human-readable domain.
| `build_dns_query()` | Builds a complete binary query packet (Header + Question) |
| `parse_dns_response()` | Parses a full response into `DNSMessage` with all 4 sections |
| `serialize_dns_response()` | Builds a binary response to send back to the client |


EXPLAINTION 
1. domain_to_qname()
Purpose

Convert a normal domain name:

google.com

into DNS wire format:

\x06google\x03com\x00
Why?

DNS packets don't store domains as strings.

Instead they store:

[length][label][length][label][0]

Example:

google.com

google -> 6 chars
com    -> 3 chars

Becomes:

06 google 03 com 00

Byte by byte:

06 67 6f 6f 67 6c 65
03 63 6f 6d
00
Used in
build_dns_query()

before sending queries to DNS servers.

2. parse_domain_name()
Purpose

Convert DNS wire format back to a human-readable domain.

Example:

06 google 03 com 00

becomes:

google.com
Hard Part: Compression

DNS servers often send:

C0 0C

instead of repeating:

google.com

0xC0 means:

This is a pointer.
Jump somewhere else in the packet.

Example:

Offset 12:
06 google 03 com 00

Later:
C0 0C

Meaning:

Go to byte 12 and read the name there.

This saves bandwidth.

Your parser must:

if ((byte & 0xC0) == 0xC0)

detect pointers and follow them.

3. build_dns_query()
Purpose

Construct an entire DNS request packet.

Input:

google.com
A record

Output:

+-----------+
| Header    |
+-----------+
| Question  |
+-----------+

Example:

Header
ID       = 1234
Flags    = 0x0000
QDCOUNT  = 1
Question
google.com
Type = A
Class = IN

Produces a binary packet:

[Header][Question]

ready for:

sendto()
4. parse_dns_response()
Purpose

Take raw bytes from a DNS server and decode everything.

Input:

recvfrom()

returns:

93 bytes of binary garbage

Your parser converts it into:

DNSMessage
{
    header
    questions
    answers
    authorities
    additionals
}

Example:

DNS response:

QUESTION:
google.com

ANSWER:
142.250.77.142

AUTHORITY:
...

ADDITIONAL:
...

becomes:

msg->answers[0]
msg->authority[0]
msg->additional[0]

Now your resolver can inspect them easily.

5. serialize_dns_response()
Purpose

Build a DNS response packet to send back to the client.

Your resolver finds:

google.com
=
142.250.77.142

Now you must return:

HEADER
QUESTION
ANSWER

in DNS binary format.

Example:

Answer Record
Name  = google.com
Type  = A
TTL   = 60
RDATA = 142.250.77.142

gets converted into bytes:

C0 0C
00 01
00 01
00 00 00 3C
00 04
8E FA 4D 8E

and packed into:

[Header]
[Question]
[Answer]

Then:

sendto(client)

returns the result to dig.

Overall Flow
Client
  |
  | google.com A
  v
build_dns_query()
  |
  v
sendto(root server)

recvfrom()
  |
  v
parse_dns_response()
  |
  v
resolver.c
  |
  v
serialize_dns_response()
  |
  v
sendto(client)

Interview summary:

domain_to_qname() → String → DNS wire format
parse_domain_name() → DNS wire format → String
build_dns_query() → Create DNS request packet
parse_dns_response() → Decode DNS response packet
serialize_dns_response() → Create DNS response packet for the client
#### DNS Name Compression (Important Interview Topic!):

DNS uses pointer-based compression to avoid repeating domain names. If a label starts with bits `11xxxxxx`, the next 14 bits are an offset into the packet where the name was previously written.

```c
// Check for pointer (top two bits are 11)
if ((len & 0xC0) == 0xC0) {
    uint16_t pointer = ((len & 0x3F) << 8) | buffer[offset + 1];
    // Jump to 'pointer' offset and continue reading there
}
```

We also **emit** compression when serializing responses:
```c
uint16_t name_ptr = htons(0xC00C); // Points to offset 12 (right after the header)
```

---

### 4.3 `resolver.c` / `resolver.h` — The Resolution Algorithm

**Purpose:** The brain of the project. Implements iterative DNS resolution.

#### Algorithm: `resolve_recursive(domain, qtype, cname_depth)`

```
1. Check cache → if HIT, return immediately
2. Check CNAME cache → if HIT, recursively resolve the alias
3. Pick a random Root Server
4. LOOP:
   a. Build query, send to current_server via network.c
   b. Parse response
   c. Check ANSWER section:
      - If A/AAAA record found → cache it, return the IP
      - If CNAME found → recursively resolve the alias target
   d. Check AUTHORITY + ADDITIONAL sections for NS referrals:
      - Find NS record in Authority section
      - Find matching "glue" A record in Additional section
      - Prefer IPv4 glue over IPv6 glue (WSL compatibility fix)
   e. If no glue record exists → recursively resolve the NS hostname itself
   f. Set current_server = next referral IP, continue loop
5. If no answer and no referral → return NULL (NXDOMAIN)
```

#### Key Design Decisions:
- **CNAME depth limit** of 10 to prevent infinite CNAME chains
- **IPv4 glue priority** — always prefer A glue records over AAAA to avoid IPv6 routing failures
- **Iterative, not recursive** — we set `RD=0` (Recursion Desired = false) in outgoing queries because we do the walking ourselves

---

### 4.4 `network.c` / `network.h` — The Network Transport Layer

**Purpose:** Sends a UDP query to a given IP on port 53 and waits for a response.

**Key Features:**
- **Dual-stack support:** Detects IPv4 vs IPv6 address via `inet_pton()`, creates the appropriate socket family
- **Timeout:** `setsockopt(SO_RCVTIMEO)` with 2-second timeout
- **Retry logic:** Up to 3 retries on timeout before giving up
- Uses `struct sockaddr_storage` — a generic struct large enough for both IPv4 and IPv6

```c
if (inet_pton(AF_INET, ip, &addr4->sin_addr) == 1) {
    family = AF_INET;   // IPv4
} else if (inet_pton(AF_INET6, ip, &addr6->sin6_addr) == 1) {
    family = AF_INET6;  // IPv6
}
sock = socket(family, SOCK_DGRAM, IPPROTO_UDP);
```

---

### 4.5 `cache.c` / `cache.h` — TTL-Based DNS Cache

**Purpose:** Avoids redundant network lookups by storing resolved results in memory.

**Key Design:**
- Cache key = `"domain:type"` (e.g., `"google.com:1"`, `"google.com:28"`)
- This means A and AAAA records for the same domain are cached independently
- **Lazy TTL expiry:** On `cache_get()`, if `now > expires_at`, return `NULL` (treat as miss)
- **Active cleanup:** `cache_cleanup()` iterates all entries and removes expired ones

```c
CacheEntry* cache_get(const char *domain, uint16_t type) {
    CacheEntry *entry = hashmap_get(cache_map, key);
    if (entry && time(NULL) > entry->expires_at) {
        return NULL;  // Expired — lazy eviction
    }
    return entry;
}
```

---

### 4.6 `hashmap.c` / `hashmap.h` — Custom O(1) Hash Table

**Purpose:** Underlying data structure for the DNS cache.

**Implementation Details:**
- **Hash function:** djb2 (`hash * 33 + c`) — a well-known, fast string hash
- **Collision handling:** Separate chaining with linked lists
- **Capacity:** 1024 buckets (configurable)
- **Generic:** Stores `void*` values — type-agnostic design
- **Memory management:** Accepts a `free_value` callback function pointer for cleanup

```c
static unsigned long hash_function(const char *str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c; // hash * 33 + c
    return hash;
}
```

> **Interview Tip:** Be ready to explain why djb2 is a good choice (fast, low collision rate for strings, simple to implement).

---

### 4.7 `root_servers.c` / `root_servers.h` — DNS Root Server Registry

**Purpose:** Provides the 13 global Root Server IPv4 addresses as the starting point for resolution.

```c
const char *ROOT_SERVERS[13] = {
    "198.41.0.4",     // a.root-servers.net
    "199.9.14.201",   // b.root-servers.net
    // ... 11 more
};
```
- `get_random_root_server()` picks a random one to distribute load

---

## 5. Key Networking Concepts

### Byte Ordering (Endianness)
- Network byte order = **Big Endian**
- x86 systems = **Little Endian**
- Must convert with `htons()` / `ntohs()` (16-bit) and `htonl()` / `ntohl()` (32-bit) when reading/writing packet fields

### UDP vs TCP for DNS
- DNS primarily uses **UDP** (connectionless, fast, no handshake)
- UDP limit = 512 bytes (classic). If response is truncated (TC flag), client should retry over TCP
- We check the **TC flag** (`flags & 0x0200`) but currently log a warning only

### Socket API Flow (Server Side)
```
socket() → bind() → recvfrom() → [process] → sendto() → loop back to recvfrom()
```

### Socket API Flow (Client Side — our outbound queries)
```
socket() → setsockopt(timeout) → sendto(root_server:53) → recvfrom() → close()
```

---

## 6. Bugs Encountered & How I Fixed Them

### Bug 1: IPv6 Glue Record Crash

**Symptom:** `dig google.com A` returned `NXDOMAIN` with 0ms query time (immediate failure).

**Root Cause:** When the TLD server returned referrals to Google's authoritative servers, the Additional section contained both `A` and `AAAA` glue records. The code grabbed the **first match** — which happened to be an IPv6 address. WSL doesn't have IPv6 routing, so `sendto()` failed with "Network is unreachable".

**Fix:** Modified the glue record lookup to do **two passes** — first scan for IPv4 (A) records only, then fall back to IPv6 (AAAA) if no IPv4 glue exists.

### Bug 2: Root Server IPv6 Russian Roulette

**Symptom:** Queries would randomly succeed or fail with ~50% probability.

**Root Cause:** The Root Server list contained 26 entries (13 IPv4 + 13 IPv6). `get_random_root_server()` would randomly pick one. If it picked an IPv6 root server, the entire resolution would fail at the very first step.

**Fix:** Removed all IPv6 Root Servers from the list, keeping only the 13 IPv4 addresses.

### Bug 3: Buffered stdout in Daemon Mode

**Symptom:** Server started but no log output appeared until the process was killed.

**Root Cause:** C's stdout is line-buffered when connected to a terminal but **fully buffered** when redirected to a file/pipe. Since WSL was redirecting output, logs were stuck in the buffer.

**Fix:** Added `setvbuf(stdout, NULL, _IONBF, 0)` at the start of `main()` to disable buffering entirely.

---

## 7. Potential Interview Questions & Answers

### Q: "Why did you choose C for this project?"
> DNS is a binary wire protocol operating at the network layer. C gives me direct control over byte-level memory layout, socket syscalls, and zero-copy parsing. There's no serialization overhead — I work directly with the raw packet bytes using `memcpy` and pointer arithmetic.

### Q: "What's the difference between recursive and iterative DNS resolution?"
> In **recursive** mode, a client sends one query to a resolver, and the resolver does all the work and returns the final answer. In **iterative** mode, each server the resolver contacts either gives the answer or gives a referral to the next server. My daemon acts as a **recursive resolver** to the client (the client sends one query and gets one answer), but internally it performs **iterative resolution** (it walks the DNS hierarchy step by step).

### Q: "How does DNS name compression work?"
> DNS packets use pointer-based compression defined in RFC 1035. Instead of repeating a domain name, a 2-byte pointer is used. The first two bits are `11` (making it distinguishable from a length byte which is max 63). The remaining 14 bits are an offset into the packet where the domain name was previously written. My parser handles this recursively with a jump counter (max 20) to prevent infinite loops from malicious packets.

### Q: "What happens if you query a CNAME?"
> If a domain like `www.example.com` is a CNAME pointing to `example.cdn.net`, the authoritative server returns a CNAME record instead of an A record. My resolver detects this, caches the CNAME mapping, and then **recursively calls itself** to resolve the CNAME target (`example.cdn.net`). I limit this to 10 levels deep to prevent infinite CNAME chains.

### Q: "How does your caching work? What about stale entries?"
> I use a custom hashmap (djb2 hash, separate chaining) with composite keys like `"google.com:1"` (domain + record type). Each entry has an `expires_at` timestamp set to `now + TTL`. On lookup, if the entry is expired, I return NULL (lazy eviction). I also have a `cache_cleanup()` function that iterates all buckets and removes expired entries — this could be called periodically in a production system.

### Q: "What would you do to make this production-ready?"
> Several things:
> - **Multithreading:** Currently single-threaded, so one slow upstream query blocks all clients. I'd use a thread pool or `epoll`-based event loop.
> - **TCP fallback:** Handle truncated responses by retrying over TCP.
> - **DNSSEC validation:** Verify cryptographic signatures on responses.
> - **Negative caching:** Cache NXDOMAIN responses to avoid repeated lookups for non-existent domains.
> - **Rate limiting:** Prevent DNS amplification attacks.
> - **Configuration file:** Allow configuring port, upstream forwarders, cache size, etc.

### Q: "Why UDP and not TCP?"
> DNS was designed for UDP because queries and responses are typically small (< 512 bytes), and UDP avoids the overhead of TCP's 3-way handshake. For a simple A record lookup, UDP needs 2 packets (query + response) vs TCP's minimum 5 (SYN, SYN-ACK, ACK, data, FIN). However, for responses larger than 512 bytes (like DNSSEC), TCP is required.

### Q: "Explain the `0xC00C` pointer in your serializer."
> `0xC00C` is a DNS name compression pointer. `0xC0` means "this is a pointer" (top 2 bits = 11). `0x0C` = 12 in decimal, which is the byte offset right after the 12-byte DNS header — exactly where the question name starts. So instead of repeating the domain name in the answer section, I write `0xC00C` to say "the name is at offset 12 in this packet."

### Q: "What's a glue record?"
> When a TLD server says "the authoritative server for google.com is ns1.google.com", we have a chicken-and-egg problem: to resolve ns1.google.com, we need to contact google.com's authoritative server, which IS ns1.google.com. **Glue records** break this cycle — the TLD server includes the IP address of ns1.google.com directly in the Additional section of its response. My resolver looks for these glue records to avoid unnecessary extra lookups.

### Q: "How do you handle memory management in a long-running daemon?"
> Every `DNSMessage` created by `parse_dns_response()` is freed with `free_dns_message()` after use. Every `strdup()`'d string from `resolve_domain()` is freed after serialization. The cache uses a custom `free_cache_entry()` callback that frees both the domain string and the target string. I use `calloc` for zero-initialization to prevent undefined behavior from uninitialized pointers.

---

## 8. Project Structure

```
DNS RESOLVER/
├── Makefile              # Build system (gcc, C17, -Wall -Wextra)
├── src/
│   ├── main.c            # UDP server daemon (bind, recvfrom, sendto loop)
│   ├── dns_packet.c/h    # Binary DNS packet serialization/deserialization
│   ├── resolver.c/h      # Iterative resolution algorithm
│   ├── network.c/h       # UDP transport layer (dual-stack IPv4/IPv6)
│   ├── cache.c/h         # TTL-based DNS cache
│   ├── hashmap.c/h       # djb2 hashmap (separate chaining)
│   └── root_servers.c/h  # 13 global Root Server IPs
└── tests/
    ├── test_cache.c       # Cache unit tests
    └── test_parser.c      # DNS parser unit tests
```

---

## 9. Quick Reference: DNS Record Types

| Type | Value | Description | RDATA Size |
|------|-------|-------------|------------|
| A    | 1     | IPv4 address | 4 bytes |
| NS   | 2     | Nameserver hostname | Variable (domain name) |
| CNAME| 5     | Canonical name (alias) | Variable (domain name) |
| AAAA | 28    | IPv6 address | 16 bytes |

---

## 10. How to Demo

```bash
# Terminal 1 — Start the daemon
make
./dns_resolver

# Terminal 2 — Send queries
dig @127.0.0.1 -p 5300 google.com A       # IPv4 lookup
dig @127.0.0.1 -p 5300 google.com AAAA    # IPv6 lookup
dig @127.0.0.1 -p 5300 reddit.com A       # Different domain
dig @127.0.0.1 -p 5300 reddit.com A       # Cache hit (0ms!)
```
