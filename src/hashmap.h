#ifndef HASHMAP_H
#define HASHMAP_H

#include <stdint.h>
#include <stddef.h>

typedef struct HashNode {
    char *key;       // Domain name
    void *value;     // Pointer to cache entry
    struct HashNode *next;
} HashNode;

typedef struct {
    HashNode **buckets;
    size_t capacity;
    size_t size;
} HashMap;

HashMap* hashmap_create(size_t capacity);
void hashmap_put(HashMap *map, const char *key, void *value);
void* hashmap_get(HashMap *map, const char *key);
void hashmap_remove(HashMap *map, const char *key, void (*free_value)(void*));
void hashmap_free(HashMap *map, void (*free_value)(void*));
void hashmap_iterate(HashMap *map, void (*callback)(const char *key, void *value, void *user_data), void *user_data);

#endif
