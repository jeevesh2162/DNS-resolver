#include "root_servers.h"
#include <stdlib.h>
#include <time.h>

const char *ROOT_SERVERS[NUM_ROOT_SERVERS] = {
    "198.41.0.4",     // a.root-servers.net
    "199.9.14.201",   // b.root-servers.net
    "192.33.4.12",    // c.root-servers.net
    "199.7.91.13",    // d.root-servers.net
    "192.203.230.10", // e.root-servers.net
    "192.5.5.241",    // f.root-servers.net
    "192.112.36.4",   // g.root-servers.net
    "198.97.190.53",  // h.root-servers.net
    "192.36.148.17",  // i.root-servers.net
    "192.58.128.30",  // j.root-servers.net
    "193.0.14.129",   // k.root-servers.net
    "199.7.83.42",    // l.root-servers.net
    "202.12.27.33"    // m.root-servers.net
};

const char* get_random_root_server(void) {
    // Seed randomness if needed
    static int seeded = 0;
    if (!seeded) {
        srand(time(NULL));
        seeded = 1;
    }
    int idx = rand() % NUM_ROOT_SERVERS;
    return ROOT_SERVERS[idx];
}


// root_servers.h / root_servers.c
// Contains the 13 root server IPv4 addresses.
// Provides get_random_root_server() to choose a starting root server.