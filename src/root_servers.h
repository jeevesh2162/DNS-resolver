#ifndef ROOT_SERVERS_H
#define ROOT_SERVERS_H

#define NUM_ROOT_SERVERS 13

extern const char *ROOT_SERVERS[NUM_ROOT_SERVERS];

// Returns a random root server IP
const char* get_random_root_server(void);

#endif
