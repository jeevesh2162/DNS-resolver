#include "fallback_policy.h"
#include <string.h>
#include <stdio.h>
// Initializes the nameserver list by zeroing all fields.
void ns_list_init(NameserverList *list) {
    if (!list) return;
    memset(list, 0, sizeof(NameserverList));
}
// Adds a new nameserver IP to the list if space is available.
// TLD servers and authoritative servers are both types of nameservers, but they sit at different levels in the DNS hierarchy.  
int ns_list_add(NameserverList *list, const char *ip) {
    if (!list || !ip || list->count >= MAX_NAMESERVERS) {
        return 0;
    }
    strncpy(list->servers[list->count], ip, 63);
    list->servers[list->count][63] = '\0';
    list->count++;
    return 1;
}
// Returns the currently active nameserver IP.
const char* ns_list_get_current(NameserverList *list) {
    if (!list || list->count == 0 || list->current_idx >= list->count) {
        return NULL;
    }
    return list->servers[list->current_idx];
}
// Switches to the next available (non-failed) nameserver, or retries failed ones if needed.
int ns_list_next(NameserverList *list) {
    if (!list || list->count == 0) {
        return 0;
    }

    // Find next server that hasn't failed
    for (int i = 1; i < list->count; i++) {
        int idx = (list->current_idx + i) % list->count;
        if (!(list->failed_mask & (1 << idx))) {
            list->current_idx = idx;
            printf("[FALLBACK] Moved to nameserver: %s\n", list->servers[idx]);
            return 1;
        }
    }

    // All failed, try to find any
    for (int i = 0; i < list->count; i++) {
        int idx = (list->current_idx + 1 + i) % list->count;
        list->current_idx = idx;
        printf("[FALLBACK] Trying failed nameserver: %s\n", list->servers[idx]);
        return 1;
    }

    return 0;
}
// Marks the current nameserver as failed in the bitmask.
void ns_list_mark_failed(NameserverList *list) {
    if (!list || list->current_idx >= list->count) {
        return;
    }
    list->failed_mask |= (1 << list->current_idx);
    printf("[FALLBACK] Marked nameserver %s as failed\n", list->servers[list->current_idx]);
}
// Resets fallback state by clearing current index and failure tracking.
void ns_list_reset(NameserverList *list) {
    if (!list) return;
    list->current_idx = 0;
    list->failed_mask = 0;
}
// Checks whether all configured nameservers have failed.
int ns_list_all_failed(NameserverList *list) {
    if (!list || list->count == 0) {
        return 1;
    }
    // Check if all bits are set
    int all_mask = (1 << list->count) - 1;
    return (list->failed_mask & all_mask) == all_mask;
}
