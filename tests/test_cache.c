#include "../src/cache.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

int main() {
    printf("Running cache tests...\n");
    cache_init();

    // Test insertion and retrieval
    cache_put("example.com", 1, "1.2.3.4", 300);
    CacheEntry *entry = cache_get("example.com", 1);
    assert(entry != NULL);
    assert(strcmp(entry->target, "1.2.3.4") == 0);

    // Test removal
    cache_remove("example.com");
    entry = cache_get("example.com", 1);
    assert(entry == NULL);

    // --- INTERVIEW TEST 3: The TTL Expiration Test ---
    // Why perform this test: Caching is only useful if it prevents stale data. 
    // We must ensure that when the Time-to-Live (TTL) of a record expires, 
    // the cache strictly refuses to return it via the normal cache_get().
    // We simulate this by putting a record, manually hacking its expiration 
    // timestamp to the past, and verifying it returns NULL.
    printf("Testing TTL expiration (State & Time Management)...\n");
    cache_put("expire.com", 1, "5.5.5.5", 300);
    
    // Retrieve it to get a pointer to the entry
    entry = cache_get_with_stale("expire.com", 1);
    assert(entry != NULL);
    
    // Simulate time passing (move expiration to 10 seconds ago)
    entry->expires_at = time(NULL) - 10;
    
    // Now standard cache_get should return NULL because it's expired
    CacheEntry *expired_entry = cache_get("expire.com", 1);
    assert(expired_entry == NULL);

    // However, it should still be available as stale data if we ask for it!
    CacheEntry *stale_entry = cache_get_with_stale("expire.com", 1);
    assert(stale_entry != NULL);
    assert(stale_entry->is_stale == 1);

    cache_free_all();
    printf("Cache tests passed.\n");
    return 0;
}
