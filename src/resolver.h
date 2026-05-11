#ifndef RESOLVER_H
#define RESOLVER_H

#include <stdint.h>
#include <stddef.h>
#include "dns_packet.h"
#include "upstream_resolvers.h"

// Resolves a given domain for the given QTYPE (e.g. DNS_TYPE_A).
// Performs iterative lookup with comprehensive fallback policies
// Returns an IP address as a malloc'd string (caller must free), or NULL on failure.
char* resolve_domain(const char *domain, uint16_t qtype);

// Resolve with explicit upstream resolver pool
char* resolve_domain_with_upstream(const char *domain, uint16_t qtype, UpstreamResolverPool *upstream);

// Try to resolve via upstream recursive resolver first
// Falls back to iterative if all upstreams fail
char* resolve_with_fallback_priority(const char *domain, uint16_t qtype);

// Try A records if AAAA fails (IPv4/IPv6 fallback)
char* resolve_with_family_fallback(const char *domain, uint16_t qtype);

#endif
