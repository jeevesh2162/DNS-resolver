#include "hashmap.h"
#include <stdlib.h>
#include <string.h>


// This file implements a simple hash map (dictionary) in C using chaining for collision handling.
// It is a custom hash map implementation that stores key–value pairs using the djb2 hash function 
// and resolves collisions using linked lists.

// djb2 hash
static unsigned long hash_function(const char *str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }
    return hash;
}

HashMap* hashmap_create(size_t capacity) {
    HashMap *map = malloc(sizeof(HashMap));
    if (!map) return NULL;
    map->capacity = capacity;
    map->size = 0;
    map->buckets = calloc(capacity, sizeof(HashNode*));
    return map;
}

void hashmap_put(HashMap *map, const char *key, void *value) {
    unsigned long hash = hash_function(key);
    size_t index = hash % map->capacity;

    HashNode *node = map->buckets[index];
    while (node) {
        if (strcmp(node->key, key) == 0) {
            node->value = value; // Update existing
            return;
        }
        node = node->next;
    }

    HashNode *new_node = malloc(sizeof(HashNode));
    new_node->key = strdup(key);
    new_node->value = value;
    new_node->next = map->buckets[index];
    map->buckets[index] = new_node;
    map->size++;
}

void* hashmap_get(HashMap *map, const char *key) {
    unsigned long hash = hash_function(key);
    size_t index = hash % map->capacity;

    HashNode *node = map->buckets[index];
    while (node) {
        if (strcmp(node->key, key) == 0) {
            return node->value;
        }
        node = node->next;
    }
    return NULL;
}

void hashmap_remove(HashMap *map, const char *key, void (*free_value)(void*)) {
    unsigned long hash = hash_function(key);
    size_t index = hash % map->capacity;

    HashNode *node = map->buckets[index];
    HashNode *prev = NULL;

    while (node) {
        if (strcmp(node->key, key) == 0) {
            if (prev) {
                prev->next = node->next;
            } else {
                map->buckets[index] = node->next;
            }
            if (free_value && node->value) {
                free_value(node->value);
            }
            free(node->key);
            free(node);
            map->size--;
            return;
        }
        prev = node;
        node = node->next;
    }
}

void hashmap_free(HashMap *map, void (*free_value)(void*)) {
    if (!map) return;
    for (size_t i = 0; i < map->capacity; i++) {
        HashNode *node = map->buckets[i];
        while (node) {
            HashNode *next = node->next;
            if (free_value && node->value) {
                free_value(node->value);
            }
            free(node->key);
            free(node);
            node = next;
        }
    }
    free(map->buckets);
    free(map);
}

void hashmap_iterate(HashMap *map, void (*callback)(const char *key, void *value, void *user_data), void *user_data) {
    for (size_t i = 0; i < map->capacity; i++) {
        HashNode *node = map->buckets[i];
        while (node) {
            callback(node->key, node->value, user_data);
            node = node->next;
        }
    }
}
