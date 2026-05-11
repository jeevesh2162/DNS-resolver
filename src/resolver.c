#include "resolver.h"
#include "network.h"
#include "root_servers.h"
#include "cache.h"
#include "tld_strategy.h"
#include "upstream_resolvers.h"
#include "response_validator.h"
#include "fallback_policy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

// resolver.h / resolver.c
// Core iterative resolver implementation.
// Resolves domains via root/TLD servers, follows referrals, handles glue records.
// Supports:
// CNAME chasing
// IPv6 AAAA -> A fallback
// upstream recursive resolver fallback
// stale cache fallback
// TLD strategy for direct TLD server use
// Exposes public APIs for normal resolution, upstream-first resolution, and family fallback.

#define MAX_CNAME_DEPTH 10
#define RESOLVE_MAX_ITERATIONS 15

static void format_ipv4(const uint8_t *rdata, char *out_ip) {
    sprintf(out_ip, "%d.%d.%d.%d", rdata[0], rdata[1], rdata[2], rdata[3]);
}

static void format_ipv6(const uint8_t *rdata, char *out_ip) {
    inet_ntop(AF_INET6, rdata, out_ip, INET6_ADDRSTRLEN);
}

static char* resolve_recursive_internal(const char *domain, uint16_t qtype, int cname_depth, int use_upstream);

// FEATURE 1: IPv4/IPv6 Fallback - if AAAA fails, try A
static char* resolve_with_family_fallback_internal(const char *domain, uint16_t qtype) {
    if (qtype != DNS_TYPE_AAAA) {
        return NULL;  // Only do fallback for AAAA
    }

    printf("[IPv6 FALLBACK] AAAA lookup failed for %s, trying A record\n", domain);
    
    char *result = resolve_recursive_internal(domain, DNS_TYPE_A, 0, 0);
    if (result) {
        printf("[IPv6 FALLBACK] Got IPv4 alternative: %s\n", result);
        return result;
    }
    
    return NULL;
}

// FEATURE 5: Query upstream recursive resolver
static char* query_upstream_with_retries(UpstreamResolverPool *pool, const char *domain, uint16_t qtype) {
    if (!pool || pool->count == 0) {
        return NULL;
    }

    printf("[UPSTREAM] Trying upstream recursive resolvers for %s\n", domain);

    for (int attempt = 0; attempt < pool->count; attempt++) {
        const char *resolver_ip = upstream_get_next(pool);
        if (!resolver_ip) break;

        printf("[UPSTREAM] Attempt %d: %s\n", attempt + 1, resolver_ip);
        
        char *result = query_upstream_resolver(resolver_ip, domain, qtype);
        if (result) {
            printf("[UPSTREAM] Success with %s\n", resolver_ip);
            return result;
        }

        upstream_mark_failed(pool);
    }

    printf("[UPSTREAM] All upstream resolvers failed\n");
    return NULL;
}

// FEATURE 6: Stale Cache Fallback - return stale entry if fresh query fails
static char* try_stale_cache(const char *domain, uint16_t qtype) {
    CacheEntry *stale = cache_get_with_stale(domain, qtype);
    if (stale) {
        printf("[STALE CACHE FALLBACK] Using stale entry for %s (age: %ldm)\n", domain, (time(NULL) - stale->expires_at + 3600) / 60);
        return strdup(stale->target);
    }
    return NULL;
}

// FEATURE 4: Secondary Nameservers - collect all NS records and try each
static void collect_nameservers(DNSMessage *msg, NameserverList *ns_list) {
    if (!msg || !ns_list || msg->header.nscount == 0) {
        return;
    }

    for (int i = 0; i < msg->header.nscount; i++) {
        DNSResourceRecord *ns_rr = &msg->authorities[i];
        if (ns_rr->type != DNS_TYPE_NS) continue;

        char *ns_name = (char*)ns_rr->rdata;
        char next_server[64] = {0};

        // Try A glue record first
        for (int j = 0; j < msg->header.arcount; j++) {
            DNSResourceRecord *ar_rr = &msg->additionals[j];
            if (strcasecmp(ar_rr->name, ns_name) == 0 && ar_rr->type == DNS_TYPE_A) {
                format_ipv4(ar_rr->rdata, next_server);
                printf("[SECONDARY NS] Found A glue for %s -> %s\n", ns_name, next_server);
                break;
            }
        }

        // Try AAAA glue if no A
        if (next_server[0] == '\0') {
            for (int j = 0; j < msg->header.arcount; j++) {
                DNSResourceRecord *ar_rr = &msg->additionals[j];
                if (strcasecmp(ar_rr->name, ns_name) == 0 && ar_rr->type == DNS_TYPE_AAAA) {
                    format_ipv6(ar_rr->rdata, next_server);
                    printf("[SECONDARY NS] Found AAAA glue for %s -> %s\n", ns_name, next_server);
                    break;
                }
            }
        }

        if (next_server[0] != '\0') {
            ns_list_add(ns_list, next_server);
        }
    }
}

// Main iterative resolution logic with all fallback policies
static char* resolve_recursive_internal(const char *domain, uint16_t qtype, int cname_depth, int use_upstream) {
    if (cname_depth > MAX_CNAME_DEPTH) {
        fprintf(stderr, "[RESOLVER] Error: Max CNAME depth reached for %s\n", domain);
        return NULL;
    }

    // Check fresh cache first
    CacheEntry *entry = cache_get(domain, qtype);
    if (entry) {
        printf("[CACHE HIT] Domain: %s -> %s\n", domain, entry->target);
        return strdup(entry->target);
    }
    
    // Check CNAME cache
    entry = cache_get(domain, DNS_TYPE_CNAME);
    if (entry && qtype != DNS_TYPE_CNAME) {
        printf("[CACHE HIT CNAME] %s -> %s\n", domain, entry->target);
        return resolve_recursive_internal(entry->target, qtype, cname_depth + 1, 0);
    }

    // (Upstream fallback moved to after iterative resolution)

    char current_server[64];
    
    // FEATURE 9: TLD Strategy - check if we can skip root
    TLDStrategy strategy = get_tld_strategy(domain);
    const char *direct_tld = get_direct_tld_server(domain);
    
    if (strategy == STRATEGY_DIRECT && direct_tld) {
        strcpy(current_server, direct_tld);
        printf("[TLD STRATEGY] Using direct TLD server: %s\n", current_server);
    } else {
        strcpy(current_server, get_random_root_server());
        printf("[ROOT QUERY] Starting with root server: %s\n", current_server);
    }

    int iterations = 0;
    while (iterations < RESOLVE_MAX_ITERATIONS) {
        iterations++;
        
        uint8_t query_buffer[DNS_MAX_PAYLOAD];
        size_t query_len;
        build_dns_query(domain, qtype, query_buffer, &query_len);

        uint8_t response_buffer[DNS_MAX_PAYLOAD];
        
        // FEATURE 2,8: UDP with TCP fallback + exponential backoff
        int bytes_received = network_send_query_with_fallback(current_server, query_buffer, query_len, response_buffer, sizeof(response_buffer));

        if (bytes_received < 0) {
            printf("[RESOLVER] Query failed for server %s, no fallback available\n", current_server);
            // FEATURE 6: Try stale cache as last resort
            char *stale = try_stale_cache(domain, qtype);
            if (stale) return stale;
            return NULL;
        }

        // FEATURE 7: Response validation
        DNSMessage *msg = parse_dns_response(response_buffer, bytes_received);
        if (!msg) {
            printf("[RESOLVER] Failed to parse response, validation error\n");
            // Retry with same server? Or move on?
            continue;
        }

        if (!validate_dns_response(msg, domain, qtype)) {
            printf("[RESOLVER] Response validation failed\n");
            free_dns_message(msg);
            // FEATURE 6: Try stale cache
            char *stale = try_stale_cache(domain, qtype);
            if (stale) return stale;
            return NULL;
        }

        if (msg->header.flags & DNS_FLAG_TC) {
            printf("[RESOLVER] Response truncated, retrying with TCP handled in network layer\n");
        }

        char *result = NULL;
        NameserverList ns_list;
        ns_list_init(&ns_list);

        // 1. Check Answers
        for (int i = 0; i < msg->header.ancount; i++) {
            DNSResourceRecord *rr = &msg->answers[i];
            if (strcasecmp(rr->name, domain) == 0) {
                if (rr->type == DNS_TYPE_A) {
                    char ip_str[INET_ADDRSTRLEN];
                    format_ipv4(rr->rdata, ip_str);
                    cache_put(rr->name, rr->type, ip_str, rr->ttl);
                    if (qtype == DNS_TYPE_A) {
                        result = strdup(ip_str);
                        printf("[AUTHORITATIVE ANSWER] %s A %s\n", rr->name, ip_str);
                    }
                } else if (rr->type == DNS_TYPE_AAAA) {
                    char ip_str[INET6_ADDRSTRLEN];
                    format_ipv6(rr->rdata, ip_str);
                    cache_put(rr->name, rr->type, ip_str, rr->ttl);
                    if (qtype == DNS_TYPE_AAAA) {
                        result = strdup(ip_str);
                        printf("[AUTHORITATIVE ANSWER] %s AAAA %s\n", rr->name, ip_str);
                    }
                } else if (rr->type == DNS_TYPE_CNAME) {
                    char *alias = (char*)rr->rdata;
                    cache_put(rr->name, rr->type, alias, rr->ttl);
                    printf("[AUTHORITATIVE CNAME] %s -> %s\n", rr->name, alias);
                    if (qtype != DNS_TYPE_CNAME && !result) {
                        result = resolve_recursive_internal(alias, qtype, cname_depth + 1, 0);
                    } else if (qtype == DNS_TYPE_CNAME) {
                        result = strdup(alias);
                    }
                }
            }
        }

        if (result) {
            free_dns_message(msg);
            return result;
        }

        // 2. FEATURE 4: Collect all secondary nameservers with glue records
        collect_nameservers(msg, &ns_list);

        if (ns_list.count > 0) {
            printf("[RESOLVER] Found %d nameservers, trying in order\n", ns_list.count);
            strcpy(current_server, ns_list_get_current(&ns_list));
            printf("[NEXT SERVER] Querying %s\n", current_server);
            free_dns_message(msg);
            continue;
        }

        // 3. No glue records - need to resolve NS names
        if (msg->header.nscount > 0) {
            for (int i = 0; i < msg->header.nscount; i++) {
                if (msg->authorities[i].type == DNS_TYPE_NS) {
                    char *ns_name = (char*)msg->authorities[i].rdata;
                    printf("[REFERRAL] Resolving NS %s without glue record\n", ns_name);
                    char *ns_ip = resolve_recursive_internal(ns_name, DNS_TYPE_A, cname_depth + 1, 0);
                    if (ns_ip) {
                        strcpy(current_server, ns_ip);
                        free(ns_ip);
                        free_dns_message(msg);
                        continue;
                    }
                }
            }
        }

        // No answers, no referrals, no glue - dead end
        free_dns_message(msg);
        break;
    }

    if (iterations >= RESOLVE_MAX_ITERATIONS) {
        printf("[RESOLVER] Max iterations reached\n");
    }

    // FEATURE 5: Try upstream recursive resolvers if iterative failed
    if (use_upstream) {
        printf("[UPSTREAM FALLBACK] Iterative resolution failed, trying upstream\n");
        UpstreamResolverPool *upstream = upstream_init_default();
        char *result = query_upstream_with_retries(upstream, domain, qtype);
        upstream_free(upstream);
        if (result) {
            cache_put(domain, qtype, result, 300);  // Cache for 5 min
            return result;
        }
    }

    // FEATURE 1: If IPv6 failed, try IPv4 fallback
    if (qtype == DNS_TYPE_AAAA) {
        return resolve_with_family_fallback_internal(domain, qtype);
    }

    // FEATURE 6: Last resort - try stale cache
    char *stale = try_stale_cache(domain, qtype);
    if (stale) return stale;

    printf("[RESOLVER] Resolution failed for %s (type %u)\n", domain, qtype);
    return NULL;
}

// Public API Functions

char* resolve_domain(const char *domain, uint16_t qtype) {
    return resolve_recursive_internal(domain, qtype, 0, 0);  // Use iterative
}

char* resolve_with_fallback_priority(const char *domain, uint16_t qtype) {
    return resolve_recursive_internal(domain, qtype, 0, 1);  // Try upstream first
}

char* resolve_domain_with_upstream(const char *domain, uint16_t qtype, UpstreamResolverPool *upstream) {
    if (!upstream || upstream->count == 0) {
        return resolve_domain(domain, qtype);
    }

    char *result = resolve_recursive_internal(domain, qtype, 0, 0);
    if (result) {
        return result;
    }

    printf("[FALLBACK] Iterative failed, using upstream resolution\n");
    result = query_upstream_with_retries(upstream, domain, qtype);
    if (result) {
        cache_put(domain, qtype, result, 300);
        return result;
    }

    return NULL;
}

char* resolve_with_family_fallback(const char *domain, uint16_t qtype) {
    char *result = resolve_recursive_internal(domain, qtype, 0, 0);
    
    if (!result && qtype == DNS_TYPE_AAAA) {
        printf("[FAMILY FALLBACK] IPv6 failed, trying IPv4\n");
        result = resolve_recursive_internal(domain, DNS_TYPE_A, 0, 0);
        if (result) {
            printf("[FAMILY FALLBACK] Got IPv4 address: %s\n", result);
        }
    }
    
    return result;
}
