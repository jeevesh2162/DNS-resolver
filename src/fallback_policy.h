#ifndef FALLBACK_POLICY_H
#define FALLBACK_POLICY_H

#define MAX_NAMESERVERS 10

typedef struct {
    char servers[MAX_NAMESERVERS][64];  // Up to 10 nameservers
    int count;
    int current_idx;
    int failed_mask;                    // Bitmap of failed servers
} NameserverList;

// Initialize nameserver list
void ns_list_init(NameserverList *list);

// Add a nameserver to the list
int ns_list_add(NameserverList *list, const char *ip);

// Get current nameserver
const char* ns_list_get_current(NameserverList *list);

// Move to next available nameserver
int ns_list_next(NameserverList *list);

// Mark current server as failed
void ns_list_mark_failed(NameserverList *list);

// Reset list
void ns_list_reset(NameserverList *list);

// Check if all servers have been tried
int ns_list_all_failed(NameserverList *list);

#endif
