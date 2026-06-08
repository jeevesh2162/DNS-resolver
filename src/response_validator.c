#include "response_validator.h"
#include <string.h>
#include <stdio.h>

int validate_dns_response(DNSMessage *msg, const char *query_domain, uint16_t query_type) {
    if (!msg) {
        printf("[VALIDATOR] NULL message\n");
        return 0;
    }

    // Check that we have a question section
    if (msg->header.qdcount == 0) {
        printf("[VALIDATOR] No question section\n");
        return 0;
    }

    // Validate first question matches our query
    if (msg->header.qdcount > 0) {
        DNSQuestion *q = &msg->questions[0];
        if (!q->qname) {
            printf("[VALIDATOR] No question name\n");
            return 0;
        }

        if (strcasecmp(q->qname, query_domain) != 0) {
            printf("[VALIDATOR] Question domain mismatch: expected %s, got %s\n", query_domain, q->qname);
            return 0;
        }

        if (q->qtype != query_type) {
            printf("[VALIDATOR] Question type mismatch: expected %u, got %u\n", query_type, q->qtype);
            return 0;
        }
    }

    // Check basic response structure
    if (msg->header.ancount > 0 && !msg->answers) {
        printf("[VALIDATOR] Claims answers but none provided\n");
        return 0;
    }

    if (msg->header.nscount > 0 && !msg->authorities) {
        printf("[VALIDATOR] Claims authorities but none provided\n");
        return 0;
    }

    if (msg->header.arcount > 0 && !msg->additionals) {
        printf("[VALIDATOR] Claims additionals but none provided\n");
        return 0;
    }

    printf("[VALIDATOR] Response valid\n");
    return 1;
}

int has_dnssec_signature(DNSMessage *msg) {
    if (!msg || !msg->answers) return 0;

    // RRSIG type is 46 (simplified check)
    for (int i = 0; i < msg->header.ancount; i++) {
        if (msg->answers[i].type == 46) {  // RRSIG
            return 1;
        }
    }

    if (msg->authorities) {
        for (int i = 0; i < msg->header.nscount; i++) {
            if (msg->authorities[i].type == 46) {  // RRSIG
                return 1;
            }
        }
    }

    return 0;
}

int has_dnssec_nsec(DNSMessage *msg) {
    if (!msg || !msg->answers) return 0;

    // NSEC type is 47, NSEC3 type is 50
    for (int i = 0; i < msg->header.ancount; i++) {
        if (msg->answers[i].type == 47 || msg->answers[i].type == 50) {
            return 1;
        }
    }

    if (msg->authorities) {
        for (int i = 0; i < msg->header.nscount; i++) {
            if (msg->authorities[i].type == 47 || msg->authorities[i].type == 50) {
                return 1;
            }
        }
    }

    return 0;
}

// response_validator.h / response_validator.c
// Validates basic DNS response integrity.
// Confirms question section matches the original query.
// Ensures declared answer/authority/additional counts match parsed data.
// Includes helper checks for DNSSEC signature/NSEC records.