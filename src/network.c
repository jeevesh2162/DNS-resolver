#include "network.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <time.h>

// Jitter is a small random delay added to the retry interval.
//  It prevents multiple clients from retrying at exactly 
//  the same time, reducing network congestion and avoiding the thundering herd problem.

// Calculate exponential backoff with jitter
static long get_backoff_ms(int retry_count) {
    // Exponential backoff: 100ms * 2^retry
    long backoff = DNS_INITIAL_BACKOFF_MS * (1 << retry_count);
    // Cap at 2 seconds
    if (backoff > 2000) backoff = 2000;
    
    // Add jitter (�25%)
    long jitter_range = backoff / 4;
    long jitter = (random() % (jitter_range * 2)) - jitter_range;
    return backoff + jitter;
}

// UDP Query with exponential backoff
int network_send_query(const char *ip, const uint8_t *query, size_t query_len, uint8_t *response, size_t response_max_len) {
    int sock;
    struct sockaddr_storage server_addr;
    socklen_t server_addr_len;
    int family = AF_INET;

    memset(&server_addr, 0, sizeof(server_addr));

    struct sockaddr_in *addr4 = (struct sockaddr_in *)&server_addr;
    struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)&server_addr;

    if (inet_pton(AF_INET, ip, &addr4->sin_addr) == 1) {
        family = AF_INET;
        addr4->sin_family = AF_INET;
        addr4->sin_port = htons(DNS_PORT);
        server_addr_len = sizeof(struct sockaddr_in);
    } else if (inet_pton(AF_INET6, ip, &addr6->sin6_addr) == 1) {
        family = AF_INET6;
        addr6->sin6_family = AF_INET6;
        addr6->sin6_port = htons(DNS_PORT);
        server_addr_len = sizeof(struct sockaddr_in6);
    } else {
        fprintf(stderr, "[NETWORK] Invalid address: %s\n", ip);
        return -1;
    }

    if ((sock = socket(family, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        perror("[NETWORK] UDP socket creation failed");
        return -1;
    }

    struct timeval tv;
    tv.tv_sec = DNS_TIMEOUT_SEC;
    tv.tv_usec = 0;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        perror("[NETWORK] setsockopt failed");
        close(sock);
        return -1;
    }

    int retries = 0;
    while (retries < DNS_MAX_RETRIES) {
        if (sendto(sock, query, query_len, 0, (struct sockaddr *)&server_addr, server_addr_len) < 0) {
            perror("[NETWORK] UDP sendto failed");
            close(sock);
            return -1;
        }

        socklen_t addr_len = server_addr_len;
        int bytes_received = recvfrom(sock, response, response_max_len, 0, (struct sockaddr *)&server_addr, &addr_len);
        
        if (bytes_received >= 0) {
            printf("[UDP SUCCESS] Received %d bytes from %s\n", bytes_received, ip);
            close(sock);
            return bytes_received;
        }

        if (retries < DNS_MAX_RETRIES - 1) {
            long wait_ms = get_backoff_ms(retries);
            printf("[UDP TIMEOUT] Retry %d for %s (backoff: %ldms)\n", retries + 1, ip, wait_ms);
            usleep(wait_ms * 1000);
        } else {
            printf("[UDP FAILURE] All retries exhausted for %s\n", ip);
        }
        
        retries++;
    }

    close(sock);
    return -1;
}

// TCP Query (for large responses, DNSSEC)
int network_send_query_tcp(const char *ip, const uint8_t *query, size_t query_len, uint8_t *response, size_t response_max_len) {
    int sock;
    struct sockaddr_storage server_addr;
    socklen_t server_addr_len;
    int family = AF_INET;

    memset(&server_addr, 0, sizeof(server_addr));

    struct sockaddr_in *addr4 = (struct sockaddr_in *)&server_addr;
    struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)&server_addr;

    if (inet_pton(AF_INET, ip, &addr4->sin_addr) == 1) {
        family = AF_INET;
        addr4->sin_family = AF_INET;
        addr4->sin_port = htons(DNS_PORT);
        server_addr_len = sizeof(struct sockaddr_in);
    } else if (inet_pton(AF_INET6, ip, &addr6->sin6_addr) == 1) {
        family = AF_INET6;
        addr6->sin6_family = AF_INET6;
        addr6->sin6_port = htons(DNS_PORT);
        server_addr_len = sizeof(struct sockaddr_in6);
    } else {
        fprintf(stderr, "[NETWORK] Invalid address for TCP: %s\n", ip);
        return -1;
    }

    if ((sock = socket(family, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        printf("[TCP FALLBACK] TCP socket creation failed, skipping TCP\n");
        return -1;
    }

    // Set TCP_NODELAY to disable Nagle's algorithm
    int flag = 1;
    if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) < 0) {
        printf("[TCP FALLBACK] Warning: TCP_NODELAY failed\n");
    }

    // Set timeout
    struct timeval tv;
    tv.tv_sec = DNS_TIMEOUT_SEC;
    tv.tv_usec = 0;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        perror("[TCP FALLBACK] setsockopt SO_RCVTIMEO failed");
        close(sock);
        return -1;
    }
    if (setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
        perror("[TCP FALLBACK] setsockopt SO_SNDTIMEO failed");
        close(sock);
        return -1;
    }

    // Connect
    if (connect(sock, (struct sockaddr *)&server_addr, server_addr_len) < 0) {
        printf("[TCP FALLBACK] Connection failed to %s\n", ip);
        close(sock);
        return -1;
    }

    printf("[TCP FALLBACK] Connected to %s, sending query\n", ip);

    // DNS over TCP: first 2 bytes are the length of the message
    uint16_t tcp_len = htons(query_len);
    uint8_t tcp_query[DNS_MAX_PAYLOAD + 2];
    memcpy(tcp_query, &tcp_len, 2);
    memcpy(tcp_query + 2, query, query_len);

    if (send(sock, tcp_query, query_len + 2, 0) < 0) {
        printf("[TCP FALLBACK] Send failed\n");
        close(sock);
        return -1;
    }

    // Read response: first 2 bytes are length
    uint8_t len_buf[2];
    if (recv(sock, len_buf, 2, MSG_WAITALL) != 2) {
        printf("[TCP FALLBACK] Failed to read response length\n");
        close(sock);
        return -1;
    }

    uint16_t resp_len = ntohs(*(uint16_t*)len_buf);
    if (resp_len > response_max_len - 2) {
        printf("[TCP FALLBACK] Response too large: %d bytes\n", resp_len);
        close(sock);
        return -1;
    }

    if (recv(sock, response, resp_len, MSG_WAITALL) != (int)resp_len) {
        printf("[TCP FALLBACK] Failed to read full response\n");
        close(sock);
        return -1;
    }

    printf("[TCP FALLBACK] Received %d bytes via TCP\n", resp_len);
    close(sock);
    return resp_len;
}

// Combined: Try UDP, fallback to TCP on truncation
int network_send_query_with_fallback(const char *ip, const uint8_t *query, size_t query_len, uint8_t *response, size_t response_max_len) {
    // Try UDP first
    int bytes_received = network_send_query(ip, query, query_len, response, response_max_len);
    
    if (bytes_received > 0) {
        // Check TC (truncation) flag at offset 2 in the response (byte 3, bit 1)
        if (bytes_received >= 3 && (response[2] & 0x02)) {
            printf("[TRUNCATION DETECTED] Response truncated, falling back to TCP\n");
            return network_send_query_tcp(ip, query, query_len, response, response_max_len);
        }
        return bytes_received;
    }

    // UDP failed, try TCP
    printf("[UDP FAILED] Attempting TCP fallback\n");
    return network_send_query_tcp(ip, query, query_len, response, response_max_len);
}



// Key Features
// Supports both IPv4 and IPv6.
// Uses UDP as the primary transport (standard DNS behavior).
// Automatically retries timed-out UDP requests.
// Implements exponential backoff with jitter to reduce network congestion.
// Detects truncated UDP responses using the TC (Truncation) flag.
// Automatically falls back to TCP for large or failed queries.
// Configures socket timeouts to avoid indefinite blocking.