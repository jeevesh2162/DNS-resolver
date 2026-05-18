#ifndef DNS_PACKET_H
#define DNS_PACKET_H

#include <stdint.h>
#include <stddef.h>

// DNS Record Types
#define DNS_TYPE_A      1
#define DNS_TYPE_NS     2
#define DNS_TYPE_CNAME  5
#define DNS_TYPE_SOA    6
#define DNS_TYPE_MX    15
#define DNS_TYPE_AAAA  28
#define DNS_TYPE_SRV   33
#define DNS_TYPE_ANY  255

// DNS Flags
#define DNS_FLAG_RD  0x0100   // Recursion Desired
#define DNS_FLAG_RA  0x0080   // Recursion Available
#define DNS_FLAG_AA  0x0004   // Authoritative Answer
#define DNS_FLAG_TC  0x0200   // Truncation

// DNS Classes
#define DNS_CLASS_IN   1

// DNS Header
typedef struct {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} DNSHeader;

// DNS Question
typedef struct {
    char *qname;
    uint16_t qtype;
    uint16_t qclass;
} DNSQuestion;

// DNS Resource Record
typedef struct {
    char *name;
    uint16_t type;
    uint16_t class;
    uint32_t ttl;
    uint16_t rdlength;
    uint8_t *rdata;
} DNSResourceRecord;

// DNS Message (parsed representation)
typedef struct {
    DNSHeader header;
    DNSQuestion *questions;
    DNSResourceRecord *answers;
    DNSResourceRecord *authorities;
    DNSResourceRecord *additionals;
} DNSMessage;

// Utility functions
uint16_t generate_tx_id(void);
void build_dns_query(const char *domain, uint16_t qtype, uint8_t *buffer, size_t *len);
void build_dns_query_recursive(const char *domain, uint16_t qtype, uint8_t *buffer, size_t *len);
DNSMessage* parse_dns_response(const uint8_t *buffer, size_t len);
void free_dns_message(DNSMessage *msg);
size_t serialize_dns_response(uint16_t id, const char *qname_str, uint16_t qtype, const char *answer_ip, uint16_t answer_type, uint8_t *buffer, size_t max_len);
const char* get_dns_type_name(uint16_t type);

#endif
