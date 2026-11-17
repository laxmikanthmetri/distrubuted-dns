
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>

#include "../common/protocol.h"

#define CACHE_SIZE 100
#define TTL_SECONDS 30
#define SERVER_COUNT 3
#define TIMEOUT_SECONDS 1

struct CacheEntry {
    char domain[256];
    char ip[256];
    time_t expires_at;
    int valid;
};

struct CacheEntry cache[CACHE_SIZE];

int ports[SERVER_COUNT] = {
    SERVER1_PORT, SERVER2_PORT, SERVER3_PORT
};

double elapsed_ms(struct timespec start, struct timespec end)
{
    return (end.tv_sec - start.tv_sec) * 1000.0 +
           (end.tv_nsec - start.tv_nsec) / 1000000.0;
}

int preferred_server(const char *domain)
{
    if (strcmp(domain, "google.com") == 0 ||
        strcmp(domain, "amazon.com") == 0 ||
        strcmp(domain, "facebook.com") == 0)
        return 0;

    if (strcmp(domain, "youtube.com") == 0 ||
        strcmp(domain, "microsoft.com") == 0 ||
        strcmp(domain, "github.com") == 0)
        return 1;

    if (strcmp(domain, "apple.com") == 0 ||
        strcmp(domain, "netflix.com") == 0 ||
        strcmp(domain, "wikipedia.org") == 0)
        return 2;

    return 0;
}

int cache_lookup(const char *domain, char *ip)
{
    time_t now = time(NULL);

    for (int i = 0; i < CACHE_SIZE; i++) {
        if (cache[i].valid &&
            strcmp(cache[i].domain, domain) == 0) {

            if (now < cache[i].expires_at) {
                strcpy(ip, cache[i].ip);
                return 1;
            }

            cache[i].valid = 0;
        }
    }

    return 0;
}

void cache_store(const char *domain, const char *ip)
{
    int slot = -1;

    for (int i = 0; i < CACHE_SIZE; i++) {
        if (cache[i].valid &&
            strcmp(cache[i].domain, domain) == 0) {
            slot = i;
            break;
        }

        if (!cache[i].valid && slot == -1)
            slot = i;
    }

    if (slot == -1)
        slot = 0;

    snprintf(cache[slot].domain,
             sizeof(cache[slot].domain), "%s", domain);

    snprintf(cache[slot].ip,
             sizeof(cache[slot].ip), "%s", ip);

    cache[slot].expires_at = time(NULL) + TTL_SECONDS;
    cache[slot].valid = 1;
}

int query_server(int port, const char *domain, char *response)
{
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    if (sockfd < 0)
        return -1;

    struct timeval timeout;
    timeout.tv_sec = TIMEOUT_SECONDS;
    timeout.tv_usec = 0;

    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO,
                   &timeout, sizeof(timeout)) < 0) {
        close(sockfd);
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, "127.0.0.1",
                  &server_addr.sin_addr) != 1) {
        close(sockfd);
        return -1;
    }

    if (sendto(sockfd, domain, strlen(domain) + 1, 0,
               (struct sockaddr *)&server_addr,
               sizeof(server_addr)) < 0) {
        close(sockfd);
        return -1;
    }

    memset(response, 0, BUFFER_SIZE);

    ssize_t n = recvfrom(sockfd, response, BUFFER_SIZE - 1, 0,
                         NULL, NULL);

    close(sockfd);

    if (n < 0)
        return -1;

    response[n] = '\0';
    return 0;
}

int main(void)
{
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    if (sockfd < 0) {
        perror("controller socket");
        return 1;
    }

    struct sockaddr_in controller_addr;
    memset(&controller_addr, 0, sizeof(controller_addr));

    controller_addr.sin_family = AF_INET;
    controller_addr.sin_addr.s_addr = INADDR_ANY;
    controller_addr.sin_port = htons(CONTROLLER_PORT);

    int reuse = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR,
               &reuse, sizeof(reuse));

    if (bind(sockfd, (struct sockaddr *)&controller_addr,
             sizeof(controller_addr)) < 0) {
        perror("controller bind");
        close(sockfd);
        return 1;
    }

    printf("DNS-like SDN Controller started on port %d\n",
           CONTROLLER_PORT);
    printf("Cache TTL: %d seconds\n", TTL_SECONDS);
    printf("Resolver timeout: %d second(s)\n", TIMEOUT_SECONDS);

    while (1) {
        char domain[BUFFER_SIZE] = {0};
        char response[BUFFER_SIZE] = {0};
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        ssize_t n = recvfrom(sockfd, domain, sizeof(domain) - 1, 0,
                             (struct sockaddr *)&client_addr,
                             &client_len);

        if (n < 0) {
            if (errno == EINTR)
                continue;

            perror("controller recvfrom");
            continue;
        }

        domain[n] = '\0';
        domain[strcspn(domain, "\r\n")] = '\0';

        if (domain[0] == '\0')
            continue;

        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);

        printf("\nQuery: %s\n", domain);

        if (cache_lookup(domain, response)) {
            clock_gettime(CLOCK_MONOTONIC, &end);

            printf("Cache HIT: %s -> %s\n",
                   domain, response);
            printf("Lookup latency: %.3f ms (cached)\n",
                   elapsed_ms(start, end));
        } else {
            printf("Cache MISS\n");

            int first = preferred_server(domain);
            int success = 0;

            for (int attempt = 0;
                 attempt < SERVER_COUNT; attempt++) {

                int index = (first + attempt) % SERVER_COUNT;
                memset(response, 0, sizeof(response));

                printf("Trying resolver on port %d...\n",
                       ports[index]);

                if (query_server(ports[index], domain,
                                 response) < 0) {
                    printf("Resolver %d timed out or failed.\n",
                           ports[index]);
                    continue;
                }

                if (strcmp(response, "NOT_FOUND") == 0) {
                    printf("Resolver %d has no mapping.\n",
                           ports[index]);
                    continue;
                }

                success = 1;

                printf("Resolved by port %d: %s\n",
                       ports[index], response);

                cache_store(domain, response);
                break;
            }

            if (!success) {
                snprintf(response, sizeof(response),
                         "NOT_FOUND");
                printf("All resolvers failed or had no mapping.\n");
            }

            clock_gettime(CLOCK_MONOTONIC, &end);

            printf("Lookup latency: %.3f ms\n",
                   elapsed_ms(start, end));
        }

        if (sendto(sockfd, response, strlen(response) + 1, 0,
                   (struct sockaddr *)&client_addr,
                   client_len) < 0) {
            perror("controller sendto");
        }
    }

    close(sockfd);
    return 0;
}
