#include "cache.h"
#include "hashmap.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static HashMap *cache_map = NULL;

// Initialize the DNS cache by creating the underlying hash map.
// Called once during resolver startup.
void cache_init(void) {
    if (!cache_map) {
        cache_map = hashmap_create(1024);
    }
}

// Helper function used when removing cache entries.
// Frees all dynamically allocated memory associated with a cache entry.
static void free_cache_entry(void *entry_ptr) {
    CacheEntry *entry = (CacheEntry*)entry_ptr;
    if (entry) {
        free(entry->domain);
        free(entry->target);
        free(entry);
    }
}

// Generates a unique cache key in the format "domain:type".
// Example: "google.com:1" for an A record lookup.
static char* generate_cache_key(const char *domain, uint16_t type) {
    char *key = malloc(strlen(domain) + 10);
    sprintf(key, "%s:%u", domain, type);
    return key;
}

// Insert a new DNS record into the cache or update an existing one.
// Stores expiry timestamps based on TTL and supports stale-cache serving.
void cache_put(const char *domain, uint16_t type, const char *target, uint32_t ttl) {
    if (!cache_map) return;
    
    char *key = generate_cache_key(domain, type);
    CacheEntry *existing = hashmap_get(cache_map, key);

    time_t now = time(NULL);
    
    // Ensure minimum TTL of 60 seconds
    if (ttl < 60) ttl = 60;

    if (existing) {
        free(existing->target);
        existing->target = strdup(target);
        existing->expires_at = now + ttl;
        existing->soft_expires_at = now + (ttl * 2);
        existing->is_stale = 0;
        free(key);
    } else {
        CacheEntry *new_entry = malloc(sizeof(CacheEntry));
        new_entry->domain = strdup(domain);
        new_entry->type = type;
        new_entry->target = strdup(target);
        new_entry->expires_at = now + ttl;
        new_entry->soft_expires_at = now + (ttl * 2);
        new_entry->is_stale = 0;
        hashmap_put(cache_map, key, new_entry);
    }
}

// Get fresh cache entry (expires_at not exceeded)
CacheEntry* cache_get(const char *domain, uint16_t type) {
    if (!cache_map) return NULL;
    
    char *key = generate_cache_key(domain, type);
    CacheEntry *entry = hashmap_get(cache_map, key);
    free(key);
    
    if (!entry) return NULL;
    
    time_t now = time(NULL);
    if (now > entry->expires_at) {
        return NULL;  // Expired
    }
    
    entry->is_stale = 0;
    return entry;
}
// USE OF STALE IN THE CACHE MEMORY
// 5–10 line interview flow:

// At 10:00 AM, the resolver caches google.com → 142.250.183.14 with TTL = 300s.

// At 10:06 AM, the normal TTL expires, so the entry becomes stale.

// The resolver still keeps it until soft_expires_at (for example, another 5 minutes).

// A user requests google.com during this stale window.

// The resolver first tries to refresh the record from the upstream DNS server.

// If the upstream server responds, the cache is updated with the new IP and TTL.

// If the upstream server is unavailable or times out, the resolver serves the stale cached IP temporarily.

// This improves availability because a slightly old DNS answer is usually better than no DNS answer


// Get cache entry including stale entries (soft_expires_at)
// Returns entry with is_stale flag set if past hard expiry
CacheEntry* cache_get_with_stale(const char *domain, uint16_t type) {
    if (!cache_map) return NULL;
    
    char *key = generate_cache_key(domain, type);
    CacheEntry *entry = hashmap_get(cache_map, key);
    free(key);
    
    if (!entry) return NULL;
    
    time_t now = time(NULL);
    
    // Truly expired (past soft_expires_at)
    if (now > entry->soft_expires_at) {
        return NULL;
    }
    
    // Past hard expiry but within soft expiry window
    if (now > entry->expires_at) {
        entry->is_stale = 1;
        return entry;
    }
    
    entry->is_stale = 0;
    return entry;
}

void cache_remove(const char *domain) {
    if (!cache_map) return;
    for (uint16_t type = 1; type < 300; type++) {
        char *key = generate_cache_key(domain, type);
        hashmap_remove(cache_map, key, free_cache_entry);
        free(key);
    }
}

void cache_cleanup(void) {
    if (!cache_map) return;
    time_t now = time(NULL);
    // Simple linear scan (could be optimized with linked list)
    // For now, just let entries expire naturally
    printf("[CACHE] Cleanup called (entries expire naturally)\n");
}

void cache_free_all(void) {
    if (!cache_map) return;
    hashmap_free(cache_map, free_cache_entry);
    cache_map = NULL;
}
