#ifndef CACHE_H
#define CACHE_H

#include <stdint.h>
#include <time.h>

// Cache Entry with stale support
typedef struct {
    char *domain;
    uint16_t type;
    char *target;
    time_t expires_at;
    time_t soft_expires_at;  // Stale-after time (2x TTL)
    int is_stale;            // Flag: set if returned after soft_expires
} CacheEntry;

void cache_init(void);
void cache_put(const char *domain, uint16_t type, const char *target, uint32_t ttl);

// Get fresh cache entry
CacheEntry* cache_get(const char *domain, uint16_t type);

// Get cache entry including stale entries (marks is_stale flag)
CacheEntry* cache_get_with_stale(const char *domain, uint16_t type);

void cache_remove(const char *domain);
void cache_cleanup(void);
void cache_free_all(void);

#endif
