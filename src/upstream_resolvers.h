#ifndef UPSTREAM_RESOLVERS_H
#define UPSTREAM_RESOLVERS_H

#include <stdint.h>

typedef struct {
    const char **resolvers;
    int count;
    int current_idx;
} UpstreamResolverPool;

// Initialize with default public resolvers
UpstreamResolverPool* upstream_init_default(void);

// Create custom resolver pool
UpstreamResolverPool* upstream_create(const char **ips, int count);

// Get next resolver (round-robin with rotation)
const char* upstream_get_next(UpstreamResolverPool *pool);

// Mark resolver as failed, rotate to next
void upstream_mark_failed(UpstreamResolverPool *pool);

// Free pool
void upstream_free(UpstreamResolverPool *pool);

// Query via upstream recursive resolver
// Returns IP string (malloc'd) or NULL
char* query_upstream_resolver(const char *resolver_ip, const char *domain, uint16_t qtype);

#endif
