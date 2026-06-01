#include "tld_strategy.h"
#include <string.h>
#include <stdio.h>

// TLD strategies for common TLDs
static TLDConfig tld_configs[] = {
    // Commercial - DIRECT to reduce lookup time
    {"com", STRATEGY_DIRECT, "192.55.83.30"},        // a.gtld-servers.net
    {"net", STRATEGY_DIRECT, "192.52.178.30"},       // a.nstld.verisign-grs.com
    {"org", STRATEGY_DIRECT, "199.19.56.1"},         // a0.org.afilias-nst.info
    
    // Geographic TLDs - CACHE_FIRST (popular)
    {"co.uk", STRATEGY_CACHE_FIRST, NULL},
    {"de", STRATEGY_CACHE_FIRST, NULL},
    {"fr", STRATEGY_CACHE_FIRST, NULL},
    
    // Government - use standard root
    {"gov", STRATEGY_ROOT, NULL},
    {"edu", STRATEGY_ROOT, NULL},
    
    // Info - DIRECT
    {"info", STRATEGY_DIRECT, "192.54.112.1"},       // a.nic.info
    
    // Terminator
    {NULL, STRATEGY_ROOT, NULL}
};

static const char* extract_tld(const char *domain) {
    // Find last dot to get TLD
    const char *dot = strrchr(domain, '.');
    if (!dot || dot == domain) return domain;
    return dot + 1;
}

TLDStrategy get_tld_strategy(const char *domain) {
    const char *tld = extract_tld(domain);
    
    for (int i = 0; tld_configs[i].tld != NULL; i++) {
        if (strcasecmp(tld_configs[i].tld, tld) == 0) {
            return tld_configs[i].strategy;
        }
    }
    
    return STRATEGY_ROOT;
}

const char* get_direct_tld_server(const char *domain) {
    const char *tld = extract_tld(domain);
    
    for (int i = 0; tld_configs[i].tld != NULL; i++) {
        if (strcasecmp(tld_configs[i].tld, tld) == 0) {
            if (tld_configs[i].strategy == STRATEGY_DIRECT) {
                return tld_configs[i].direct_ns;
            }
        }
    }
    
    return NULL;
}
