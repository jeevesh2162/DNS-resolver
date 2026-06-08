#include "upstream_resolvers.h"
#include "network.h"
#include "dns_packet.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <arpa/inet.h>

// Default public resolvers (Google, Cloudflare, OpenDNS)
static const char *default_resolvers[] = {
    "8.8.8.8",        // Google DNS
    "8.8.4.4",        // Google DNS Secondary
    "1.1.1.1",        // Cloudflare
    "1.0.0.1",        // Cloudflare Secondary
    "208.67.222.222"  // OpenDNS
};

UpstreamResolverPool* upstream_init_default(void) {
    return upstream_create(default_resolvers, 5);
}

UpstreamResolverPool* upstream_create(const char **ips, int count) {
    UpstreamResolverPool *pool = malloc(sizeof(UpstreamResolverPool));
    if (!pool) return NULL;

    pool->resolvers = malloc(sizeof(const char *) * count);
    if (!pool->resolvers) {
        free(pool);
        return NULL;
    }

    for (int i = 0; i < count; i++) {
        pool->resolvers[i] = ips[i];
    }
    pool->count = count;
    pool->current_idx = 0;

    return pool;
}

const char* upstream_get_next(UpstreamResolverPool *pool) {
    if (!pool || pool->count == 0) return NULL;
    const char *resolver = pool->resolvers[pool->current_idx];
    // Rotate for next call
    pool->current_idx = (pool->current_idx + 1) % pool->count;
    return resolver;
}

void upstream_mark_failed(UpstreamResolverPool *pool) {
    // Move to next resolver
    if (pool && pool->count > 0) {
        pool->current_idx = (pool->current_idx + 1) % pool->count;
        printf("[UPSTREAM] Marked failed, moving to next resolver\n");
    }
}

void upstream_free(UpstreamResolverPool *pool) {
    if (pool) {
        free(pool->resolvers);
        free(pool);
    }
}

char* query_upstream_resolver(const char *resolver_ip, const char *domain, uint16_t qtype) {
    printf("[UPSTREAM RECURSIVE] Querying %s for %s\n", resolver_ip, domain);

    uint8_t query_buffer[DNS_MAX_PAYLOAD];
    size_t query_len;
    build_dns_query_recursive(domain, qtype, query_buffer, &query_len);

    uint8_t response_buffer[DNS_MAX_PAYLOAD];
    int bytes_received = network_send_query_with_fallback(resolver_ip, query_buffer, query_len, response_buffer, sizeof(response_buffer));

    if (bytes_received < 0) {
        printf("[UPSTREAM RECURSIVE] Query failed to %s\n", resolver_ip);
        return NULL;
    }

    DNSMessage *msg = parse_dns_response(response_buffer, bytes_received);
    if (!msg) {
        printf("[UPSTREAM RECURSIVE] Failed to parse response\n");
        return NULL;
    }

    char *result = NULL;

    // Check for answer
    for (int i = 0; i < msg->header.ancount; i++) {
        DNSResourceRecord *rr = &msg->answers[i];
        if (strcasecmp(rr->name, domain) == 0) {
            if ((qtype == DNS_TYPE_A && rr->type == DNS_TYPE_A) ||
                (qtype == DNS_TYPE_AAAA && rr->type == DNS_TYPE_AAAA)) {
                if (rr->type == DNS_TYPE_A && rr->rdlength == 4) {
                    char ip[INET_ADDRSTRLEN];
                    sprintf(ip, "%d.%d.%d.%d", rr->rdata[0], rr->rdata[1], rr->rdata[2], rr->rdata[3]);
                    result = strdup(ip);
                    printf("[UPSTREAM RECURSIVE] Got answer: %s\n", ip);
                    break;
                } else if (rr->type == DNS_TYPE_AAAA && rr->rdlength == 16) {
                    char ip[INET6_ADDRSTRLEN];
                    inet_ntop(AF_INET6, rr->rdata, ip, sizeof(ip));
                    result = strdup(ip);
                    printf("[UPSTREAM RECURSIVE] Got answer: %s\n", ip);
                    break;
                }
            }
        }
    }

    free_dns_message(msg);
    return result;
}
// upstream_resolvers.h / upstream_resolvers.c
// Manages a pool of upstream recursive resolvers.
// Includes default public resolvers like Google and Cloudflare.
// Supports round-robin selection and marking failed resolvers.
// Queries upstream recursive resolvers with recursive DNS queries.