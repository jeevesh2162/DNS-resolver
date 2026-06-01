#ifndef TLD_STRATEGY_H
#define TLD_STRATEGY_H

#include <stdint.h>

typedef enum {
    STRATEGY_ROOT,        // Standard root server lookup
    STRATEGY_DIRECT,      // Direct query to known TLD server (faster)
    STRATEGY_CACHE_FIRST, // Prioritize cache for popular TLDs
} TLDStrategy;

typedef struct {
    const char *tld;
    TLDStrategy strategy;
    const char *direct_ns;  // IP of TLD server for STRATEGY_DIRECT
} TLDConfig;

// Get strategy for a domain
TLDStrategy get_tld_strategy(const char *domain);

// Get direct TLD server IP if available
const char* get_direct_tld_server(const char *domain);

#endif
