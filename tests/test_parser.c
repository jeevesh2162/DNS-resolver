#include "../src/dns_packet.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main() {
    printf("Running parser tests...\n");

    uint8_t buffer[512];
    size_t len;
    
    // Build query
    build_dns_query("www.google.com", DNS_TYPE_A, buffer, &len);
    assert(len > 0);

    // We can test parsing the query header back
    DNSMessage *msg = parse_dns_response(buffer, len);
    assert(msg != NULL);
    assert(msg->header.qdcount == 1);
    assert(strcmp(msg->questions[0].qname, "www.google.com") == 0);
    assert(msg->questions[0].qtype == DNS_TYPE_A);
    free_dns_message(msg);

    // --- INTERVIEW TEST 1: The Malformed Packet Test ---
    // Why perform this test: We are simulating a malicious or buggy upstream 
    // server that sends truncated UDP packets. If our parser doesn't bounds-check
    // properly, reading beyond the buffer size will cause a segmentation fault 
    // and crash our entire DNS resolver daemon.
    printf("Testing malformed packet (Security & Robustness)...\n");
    
    // We intentionally pass a length of 5 bytes (which is smaller than a standard 12-byte DNS header).
    // The parser should safely recognize this is invalid and return NULL instead of crashing.
    DNSMessage *malformed_msg = parse_dns_response(buffer, 5);
    assert(malformed_msg == NULL); // Must reject smoothly

    printf("Parser tests passed.\n");
    return 0;
}
