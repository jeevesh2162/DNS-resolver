#include "resolver.h"
#include "cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define SERVER_PORT 5300
#define BUFFER_SIZE 2048

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    uint8_t buffer[BUFFER_SIZE];

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);

    if (bind(sockfd, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    cache_init();
    printf("[SERVER] DNS Recursive Daemon listening on port %d...\n", SERVER_PORT);


    // check this loops is it efficient on larger scale ?

    while (1) {
        int n = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&client_addr, &client_len);
        if (n < 0) continue;

        DNSMessage *query = parse_dns_response(buffer, n);
        if (!query || query->header.qdcount == 0) {
            if (query) free_dns_message(query);
            continue;
        }

        DNSQuestion *q = &query->questions[0];
        printf("\n[SERVER] Received query for %s (Type: %d) from %s:%d\n", 
            q->qname, q->qtype, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        char *resolved_ip = resolve_domain(q->qname, q->qtype);

        uint8_t response_buffer[BUFFER_SIZE];
        size_t resp_len = serialize_dns_response(query->header.id, q->qname, q->qtype, resolved_ip, q->qtype, response_buffer, BUFFER_SIZE);

        if (resp_len > 0) {
            sendto(sockfd, response_buffer, resp_len, 0, (const struct sockaddr *)&client_addr, client_len);
            printf("[SERVER] Sent response to %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        }

        if (resolved_ip) free(resolved_ip);
        free_dns_message(query);
    }

    cache_free_all();
    close(sockfd);
    return 0;
}


// Key Responsibilities
// Creates and binds a UDP socket.
// Initializes the DNS cache.
// Receives incoming DNS queries.
// Parses DNS request packets.
// Calls the recursive resolver to obtain the requested record.
// Serializes the DNS response.
// Sends the response back to the client.
// Frees allocated resources after each request.