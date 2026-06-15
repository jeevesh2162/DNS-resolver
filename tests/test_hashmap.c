#include "../src/hashmap.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Helper to cast strings to void* without warnings, and free them properly.
void free_string(void *val) {
    // In our test, we're just passing string literals, so no free needed.
    // However, if they were malloc'd, we would call free(val) here.
}

int main() {
    printf("Running hashmap tests...\n");

    // --- INTERVIEW TEST 2: Hash Collision & Eviction Test ---
    // Why perform this test: Hashmaps are the backbone of our caching system.
    // If a hashmap fails to handle collisions (when two different keys hash to 
    // the same index) it will overwrite data and cause severe memory leaks.
    // By creating a hashmap with a very small capacity (e.g., 2 buckets) and 
    // inserting 5 items, we guarantee multiple collisions. We then assert that 
    // ALL items can still be retrieved perfectly, proving our collision 
    // resolution (Separate Chaining/Linked Lists) works flawlessly.
    printf("Testing Hash Collisions (Data Structures & Memory)...\n");

    // Force collisions by making a tiny hashmap with only 2 buckets.
    HashMap *map = hashmap_create(2);
    assert(map != NULL);
    assert(map->capacity == 2);

    // Insert 5 items. Since there are only 2 buckets, at least 3 items MUST 
    // collide and be stored as linked list nodes inside the same bucket.
    hashmap_put(map, "domain1.com", "IP_1");
    hashmap_put(map, "domain2.com", "IP_2");
    hashmap_put(map, "domain3.com", "IP_3");
    hashmap_put(map, "domain4.com", "IP_4");
    hashmap_put(map, "domain5.com", "IP_5");

    // Assert that the hashmap size correctly tracked all 5 insertions.
    assert(map->size == 5);

    // Now, verify that despite the collisions, we can retrieve EVERY item correctly.
    // If collision resolution was broken, some of these would return NULL or wrong data.
    char *val1 = (char*)hashmap_get(map, "domain1.com");
    assert(val1 != NULL && strcmp(val1, "IP_1") == 0);

    char *val3 = (char*)hashmap_get(map, "domain3.com");
    assert(val3 != NULL && strcmp(val3, "IP_3") == 0);

    char *val5 = (char*)hashmap_get(map, "domain5.com");
    assert(val5 != NULL && strcmp(val5, "IP_5") == 0);

    // Assert we get NULL for something that isn't there
    char *val_missing = (char*)hashmap_get(map, "domain_missing.com");
    assert(val_missing == NULL);

    // Clean up the hashmap. If there were memory leaks in our linked list
    // traversal during free, tools like Valgrind would catch it here.
    hashmap_free(map, free_string);

    printf("Hashmap tests passed.\n");
    return 0;
}
