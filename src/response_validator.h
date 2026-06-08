#ifndef RESPONSE_VALIDATOR_H
#define RESPONSE_VALIDATOR_H

#include "dns_packet.h"

// Validate DNS response structure and basic integrity
// Returns 1 if valid, 0 if invalid
int validate_dns_response(DNSMessage *msg, const char *query_domain, uint16_t query_type);

// Check for DNSSEC signatures (not validated, just checks presence)
int has_dnssec_signature(DNSMessage *msg);

// Check for NSEC3 records (indicates DNSSEC)
int has_dnssec_nsec(DNSMessage *msg);

#endif
