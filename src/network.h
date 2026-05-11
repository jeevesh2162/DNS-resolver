#ifndef NETWORK_H
#define NETWORK_H

#include <stddef.h>
#include <stdint.h>

#define DNS_PORT 53
#define DNS_TIMEOUT_SEC 2
#define DNS_MAX_RETRIES 3
#define DNS_MAX_PAYLOAD 512
#define DNS_INITIAL_BACKOFF_MS 100

// Sends a UDP packet to the given IP on port 53, waits for response.
// Handles timeouts, retries, and exponential backoff with jitter.
// Returns the number of bytes received, or -1 on failure.
int network_send_query(const char *ip, const uint8_t *query, size_t query_len, uint8_t *response, size_t response_max_len);

// TCP fallback: sends query via TCP and handles response
// Returns the number of bytes received, or -1 on failure
int network_send_query_tcp(const char *ip, const uint8_t *query, size_t query_len, uint8_t *response, size_t response_max_len);

// Combined UDP with TCP fallback on truncation or failure
int network_send_query_with_fallback(const char *ip, const uint8_t *query, size_t query_len, uint8_t *response, size_t response_max_len);

#endif
