#include "dns_packet.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <time.h>

#define MAX_COMPRESSION_JUMPS 20

// Generate a random 16-bit DNS transaction ID used to match
// responses with their corresponding queries.
uint16_t generate_tx_id(void) {
    static int seeded = 0;
    if (!seeded) {
        srand(time(NULL));
        seeded = 1;
    }
    return rand() % 65536;
}

// Convert www.google.com to 3www6google3com0
// uint8_t is an unsigned 8-bit integer. and it garauntees  byte thats why we used it instead of using int

static void domain_to_qname(const char *domain, uint8_t *qname, size_t *qname_len) {
    size_t len = strlen(domain);
    size_t out_idx = 0;
    size_t last_dot = 0;

    for (size_t i = 0; i <= len; i++) {
        if (domain[i] == '.' || domain[i] == '\0') {
            size_t part_len = i - last_dot;
            qname[out_idx++] = (uint8_t)part_len;
            for (size_t j = 0; j < part_len; j++) {
                qname[out_idx++] = domain[last_dot + j];
            }
            last_dot = i + 1;
        }
    }
    qname[out_idx++] = 0; // null terminator
    *qname_len = out_idx;
}

void build_dns_query(const char *domain, uint16_t qtype, uint8_t *buffer, size_t *len) {
    DNSHeader header;
    memset(&header, 0, sizeof(DNSHeader));
    header.id = htons(generate_tx_id());
    // Flags: standard query, recursion desired flag set to 0 (since we do iterative)
    header.flags = htons(0x0000); 
    header.qdcount = htons(1);  //Question Section
    header.ancount = 0;    //Answer Section 
    header.nscount = 0;   // Authority Section
    header.arcount = 0;   //Additional Sect.
    // adding header to the packet that is the buffer
    memcpy(buffer, &header, sizeof(DNSHeader));
    size_t offset = sizeof(DNSHeader);

    size_t qname_len;
    domain_to_qname(domain, buffer + offset, &qname_len);
    offset += qname_len;

    uint16_t qtype_net = htons(qtype);
    memcpy(buffer + offset, &qtype_net, sizeof(uint16_t));
    offset += sizeof(uint16_t);

    uint16_t qclass_net = htons(DNS_CLASS_IN);
    memcpy(buffer + offset, &qclass_net, sizeof(uint16_t));
    offset += sizeof(uint16_t);

    *len = offset;
}
// packet structure
// 63 bytes assigned to each section and total of 255 bytes according to the RFC standard
// +----------------+
// | Header         | 12 bytes
// +----------------+
// | QNAME          |
// | google.com     |   According to RFC 1035, there should be no valid DNS label longer than 63 characters.
// +----------------+
// | QTYPE = A      | 2 bytes  response type
// +----------------+
// | QCLASS = IN    | 2 bytes  specifies the network class of the DNS query.
// +----------------+

// Parses a domain name, handling pointers recursively.
// Returns the number of bytes consumed in the original buffer (NOT the decoded name length).
// jumps means: How many compression pointers have been followed during name resolution. 100 pointing 200 and 200 pointing 100 will cause infinite loop there for infinte jumps
static int parse_domain_name(const uint8_t *buffer, size_t buffer_len, size_t offset, char *out_name, int jumps) {
    if (jumps > MAX_COMPRESSION_JUMPS) {
        return -1; // Prevent infinite loops
    }

    size_t start_offset = offset;
    size_t out_idx = 0;
    int jumped = 0;
    int bytes_consumed = 0;

    while (offset < buffer_len) {
        uint8_t len = buffer[offset];
        if (len == 0) {
            if (!jumped) bytes_consumed++;
            break;
        }

        // Check for pointer (top two bits are 11)
        if ((len & 0xC0) == 0xC0) {
            if (offset + 1 >= buffer_len) return -1; // Truncated
            uint16_t pointer = ((len & 0x3F) << 8) | buffer[offset + 1];
            if (!jumped) bytes_consumed += 2;
            jumped = 1; // Any further parsing does not consume main buffer bytes
            
            // Recursively resolve
            int ret = parse_domain_name(buffer, buffer_len, pointer, out_name + out_idx, jumps + 1);
            if (ret < 0) return -1;
            
            return bytes_consumed; // Pointer is always end of this chain
        } else {
            if (offset + 1 + len > buffer_len) return -1; // Truncated
            if (out_idx > 0) {
                out_name[out_idx++] = '.';
            }
            memcpy(out_name + out_idx, buffer + offset + 1, len);
            out_idx += len;
            offset += len + 1;
            if (!jumped) bytes_consumed += len + 1;
        }
    }

    out_name[out_idx] = '\0';
    return bytes_consumed;
}

// Its job is to parse one DNS Resource Record from a response packet.
// decoding from binary to normal records
static int parse_rr(const uint8_t *buffer, size_t buffer_len, size_t offset, DNSResourceRecord *rr) {
    char name[256];
    int consumed = parse_domain_name(buffer, buffer_len, offset, name, 0);
    if (consumed < 0) return -1;
    
    rr->name = strdup(name);
    offset += consumed;

    if (offset + 10 > buffer_len) return -1;

    uint16_t type;
    memcpy(&type, buffer + offset, sizeof(uint16_t));
    rr->type = ntohs(type);
    offset += 2;

    uint16_t class;
    memcpy(&class, buffer + offset, sizeof(uint16_t));
    rr->class = ntohs(class);
    offset += 2;

    uint32_t ttl;
    memcpy(&ttl, buffer + offset, sizeof(uint32_t));
    rr->ttl = ntohl(ttl);
    offset += 4;

    uint16_t rdlength;
    memcpy(&rdlength, buffer + offset, sizeof(uint16_t));
    rr->rdlength = ntohs(rdlength);
    offset += 2;

    if (offset + rr->rdlength > buffer_len) return -1;
    
    if (rr->type == DNS_TYPE_NS || rr->type == DNS_TYPE_CNAME) {
        char target_name[256];
        int target_consumed = parse_domain_name(buffer, buffer_len, offset, target_name, 0);
        if (target_consumed < 0) return -1;
        rr->rdata = (uint8_t*)strdup(target_name);
    } else {
        rr->rdata = malloc(rr->rdlength);
        memcpy(rr->rdata, buffer + offset, rr->rdlength);
    }

    return consumed + 10 + rr->rdlength;
}

// parse_dns_response() parses the complete DNS packet, whereas parse_rr() is a helper
//  function that parses a single DNS Resource Record within the Answer, Authority, or Additional sections.
DNSMessage* parse_dns_response(const uint8_t *buffer, size_t len) {
    if (len < sizeof(DNSHeader)) return NULL;
    // malloc(size) allocates memory but leaves it uninitialized (contains garbage values).
    // calloc(n, size) allocates memory and initializes all bytes to zero.
    // malloc() takes one argument, while calloc() takes two arguments.
    // calloc() is safer for structs and arrays, while malloc() is slightly faster.
    DNSMessage *msg = calloc(1, sizeof(DNSMessage));
    if (!msg) return NULL;

    memcpy(&msg->header, buffer, sizeof(DNSHeader));
    msg->header.id = ntohs(msg->header.id);
    msg->header.flags = ntohs(msg->header.flags);
    msg->header.qdcount = ntohs(msg->header.qdcount);
    msg->header.ancount = ntohs(msg->header.ancount);
    msg->header.nscount = ntohs(msg->header.nscount);
    msg->header.arcount = ntohs(msg->header.arcount);

    size_t offset = sizeof(DNSHeader);

    // Questions
    if (msg->header.qdcount > 0) {
        msg->questions = calloc(msg->header.qdcount, sizeof(DNSQuestion));
        for (int i = 0; i < msg->header.qdcount; i++) {
            char qname[256];
            int consumed = parse_domain_name(buffer, len, offset, qname, 0);
            if (consumed < 0) goto error;
            
            msg->questions[i].qname = strdup(qname);
            offset += consumed;

            if (offset + 4 > len) goto error;

            uint16_t qtype;
            memcpy(&qtype, buffer + offset, 2);
            msg->questions[i].qtype = ntohs(qtype);
            offset += 2;

            uint16_t qclass;
            memcpy(&qclass, buffer + offset, 2);
            msg->questions[i].qclass = ntohs(qclass);
            offset += 2;
        }
    }

    // Answers
    if (msg->header.ancount > 0) {
        msg->answers = calloc(msg->header.ancount, sizeof(DNSResourceRecord));
        for (int i = 0; i < msg->header.ancount; i++) {
            int consumed = parse_rr(buffer, len, offset, &msg->answers[i]);
            if (consumed < 0) goto error;
            offset += consumed;
        }
    }

    // Authorities
    if (msg->header.nscount > 0) {
        msg->authorities = calloc(msg->header.nscount, sizeof(DNSResourceRecord));
        for (int i = 0; i < msg->header.nscount; i++) {
            int consumed = parse_rr(buffer, len, offset, &msg->authorities[i]);
            if (consumed < 0) goto error;
            offset += consumed;
        }
    }

    // Additionals
    if (msg->header.arcount > 0) {
        msg->additionals = calloc(msg->header.arcount, sizeof(DNSResourceRecord));
        for (int i = 0; i < msg->header.arcount; i++) {
            int consumed = parse_rr(buffer, len, offset, &msg->additionals[i]);
            if (consumed < 0) goto error;
            offset += consumed;
        }
    }

    return msg;

error:
    free_dns_message(msg);
    return NULL;
}

static void free_rrs(DNSResourceRecord *rrs, uint16_t count) {
    if (!rrs) return;
    for (int i = 0; i < count; i++) {
        free(rrs[i].name);
        free(rrs[i].rdata);
    }
    free(rrs);
}

void free_dns_message(DNSMessage *msg) {
    if (!msg) return;
    
    if (msg->questions) {
        for (int i = 0; i < msg->header.qdcount; i++) {
            free(msg->questions[i].qname);
        }
        free(msg->questions);
    }

    free_rrs(msg->answers, msg->header.ancount);
    free_rrs(msg->authorities, msg->header.nscount);
    free_rrs(msg->additionals, msg->header.arcount);

    free(msg);
}


// serialising the response to the binary format to pass it further
size_t serialize_dns_response(uint16_t id, const char *qname_str, uint16_t qtype, const char *answer_ip, uint16_t answer_type, uint8_t *buffer, size_t max_len) {
    if (max_len < sizeof(DNSHeader)) return 0;

    DNSHeader header;
    memset(&header, 0, sizeof(DNSHeader));
    header.id = htons(id);
    
    // Flags: QR=1, Opcode=0, AA=0, TC=0, RD=1, RA=1, Z=0
    uint16_t flags = 0x8180; 
    if (!answer_ip) {
        flags |= 0x0003; // Name Error (NXDOMAIN)
    }
    header.flags = htons(flags);
    header.qdcount = htons(1);
    header.ancount = answer_ip ? htons(1) : 0;
    header.nscount = 0;
    header.arcount = 0;

    memcpy(buffer, &header, sizeof(DNSHeader));
    size_t offset = sizeof(DNSHeader);

    size_t qname_len;
    domain_to_qname(qname_str, buffer + offset, &qname_len);
    offset += qname_len;

    uint16_t qtype_net = htons(qtype);
    memcpy(buffer + offset, &qtype_net, sizeof(uint16_t));
    offset += sizeof(uint16_t);

    uint16_t qclass_net = htons(DNS_CLASS_IN);
    memcpy(buffer + offset, &qclass_net, sizeof(uint16_t));
    offset += sizeof(uint16_t);

    if (answer_ip) {
        // Name compression pointer to question (offset 12)
        uint16_t name_ptr = htons(0xC00C);
        memcpy(buffer + offset, &name_ptr, sizeof(uint16_t));
        offset += sizeof(uint16_t);

        uint16_t atype_net = htons(answer_type);
        memcpy(buffer + offset, &atype_net, sizeof(uint16_t));
        offset += sizeof(uint16_t);

        memcpy(buffer + offset, &qclass_net, sizeof(uint16_t));
        offset += sizeof(uint16_t);

        uint32_t ttl = htonl(60);
        memcpy(buffer + offset, &ttl, sizeof(uint32_t));
        offset += sizeof(uint32_t);

        uint16_t rdlength = (answer_type == DNS_TYPE_A) ? 4 : 16;
        uint16_t rdlength_net = htons(rdlength);
        memcpy(buffer + offset, &rdlength_net, sizeof(uint16_t));
        offset += sizeof(uint16_t);

        if (answer_type == DNS_TYPE_A) {
            inet_pton(AF_INET, answer_ip, buffer + offset);
            offset += 4;
        } else if (answer_type == DNS_TYPE_AAAA) {
            inet_pton(AF_INET6, answer_ip, buffer + offset);
            offset += 16;
        }
    }

    return offset;
}

// Build a recursive DNS query (with RD flag set)
void build_dns_query_recursive(const char *domain, uint16_t qtype, uint8_t *buffer, size_t *len) {
    DNSHeader header;
    memset(&header, 0, sizeof(DNSHeader));
    header.id = htons(generate_tx_id());
    // Set Recursion Desired flag (0x0100)
    header.flags = htons(DNS_FLAG_RD);
    header.qdcount = htons(1);
    header.ancount = 0;
    header.nscount = 0;
    header.arcount = 0;

    memcpy(buffer, &header, sizeof(DNSHeader));
    size_t offset = sizeof(DNSHeader);

    size_t qname_len;
    domain_to_qname(domain, buffer + offset, &qname_len);
    offset += qname_len;

    uint16_t qtype_net = htons(qtype);
    memcpy(buffer + offset, &qtype_net, sizeof(uint16_t));
    offset += sizeof(uint16_t);

    uint16_t qclass_net = htons(DNS_CLASS_IN);
    memcpy(buffer + offset, &qclass_net, sizeof(uint16_t));
    offset += sizeof(uint16_t);

    *len = offset;
}

// Get human-readable DNS type name
const char* get_dns_type_name(uint16_t type) {
    switch (type) {
        case DNS_TYPE_A:     return "A";
        case DNS_TYPE_NS:    return "NS";
        case DNS_TYPE_CNAME: return "CNAME";
        case DNS_TYPE_SOA:   return "SOA";
        case DNS_TYPE_MX:    return "MX";
        case DNS_TYPE_AAAA:  return "AAAA";
        case DNS_TYPE_SRV:   return "SRV";
        case DNS_TYPE_ANY:   return "ANY";
        default:             return "UNKNOWN";
    }
}


// 🧠 Core utilities
// generate_tx_id() → Generates a random 16-bit DNS transaction ID to match queries with responses.
// domain_to_qname() → Converts a normal domain (like google.com) into DNS wire format (3google6com0).
// 📤 Query building
// build_dns_query() → Builds a basic DNS query packet for a given domain and type.
// build_dns_query_recursive() → Builds a DNS query packet with recursion desired flag set for full resolution.
// 📥 Parsing helpers
// parse_domain_name() → Decodes a DNS-encoded domain name, including compression pointers.
// parse_rr() → Parses a single DNS Resource Record (name, type, class, TTL, and data).
// 📦 Response parsing
// parse_dns_response() → Parses the full DNS response packet into a structured DNSMessage (header + sections).
// 🧹 Memory management
// free_rrs() → Frees memory allocated for an array of DNS Resource Records.
// free_dns_message() → Frees the entire DNSMessage structure including all dynamically allocated fields.
// 📤 Response building
// serialize_dns_response() → Builds a DNS response packet in binary form including header, question, and optional answer.
// 🌐 Type utilities
// get_dns_type_name() → Converts DNS record type numbers into human-readable strings (A, AAAA, CNAME, etc.).