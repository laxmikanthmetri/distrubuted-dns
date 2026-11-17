
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "../common/protocol.h"

#define CACHE_SIZE 100
#define TTL_SECONDS 30
#define TIMEOUT_SECONDS 1
#define RECOVERY_SECONDS 5
#define SERVER_COUNT 3

typedef struct {
    char domain[256];
    char ip[256];
    time_t expires_at;
    int valid;
} CacheEntry;

typedef struct {
    int healthy;
    int failures;
    time_t retry_after;
} ServerHealth;

static CacheEntry cache[CACHE_SIZE];
static ServerHealth health[SERVER_COUNT] = {
    {1, 0, 0},
    {1, 0, 0},
    {1, 0, 0}
};

static const int ports[SERVER_COUNT] = {
    SERVER1_PORT, SERVER2_PORT, SERVER3_PORT
};

static const char *server_ips[SERVER_COUNT] = {
    "10.0.0.2", "10.0.0.3", "10.0.0.4"
};

static const char *probe_domains[SERVER_COUNT] = {
    "google.com", "youtube.com", "apple.com"
};

static int find_cache(const char *domain, char *ip)
{
    time_t now = time(NULL);

    for (int i = 0; i < CACHE_SIZE; i++) {
        if (cache[i].valid && now >= cache[i].expires_at)
            cache[i].valid = 0;

        if (cache[i].valid &&
            strcmp(cache[i].domain, domain) == 0) {
            snprintf(ip, 256, "%s", cache[i].ip);
            return 1;
        }
    }

    return 0;
}

static void add_cache(const char *domain, const char *ip)
{
    int slot = 0;

    for (int i = 0; i < CACHE_SIZE; i++) {
        if (!cache[i].valid) {
            slot = i;
            goto save;
        }
    }

    slot = (int)(time(NULL) % CACHE_SIZE);

save:
    snprintf(cache[slot].domain, sizeof(cache[slot].domain),
             "%s", domain);
    snprintf(cache[slot].ip, sizeof(cache[slot].ip),
             "%s", ip);
    cache[slot].expires_at = time(NULL) + TTL_SECONDS;
    cache[slot].valid = 1;
}

static int query_server(int index, const char *domain,
                        char *response)
{
    if (index < 0 || index >= SERVER_COUNT)
        return -1;

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return -1;
    }

    struct timeval timeout = {TIMEOUT_SECONDS, 0};

    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO,
                   &timeout, sizeof(timeout)) < 0) {
        perror("setsockopt");
        close(sockfd);
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(ports[index]);

    if (inet_pton(AF_INET, server_ips[index],
                  &addr.sin_addr) != 1) {
        close(sockfd);
        return -1;
    }

    if (sendto(sockfd, domain, strlen(domain) + 1, 0,
               (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("sendto resolver");
        close(sockfd);
        return -1;
    }

    memset(response, 0, BUFFER_SIZE);

    ssize_t n = recvfrom(sockfd, response, BUFFER_SIZE - 1,
                         0, NULL, NULL);
    close(sockfd);

    if (n < 0)
        return -1;

    response[n] = '\0';
    return 0;
}

static void mark_failed(int index)
{
    health[index].healthy = 0;
    health[index].failures++;
    health[index].retry_after = time(NULL) + RECOVERY_SECONDS;

    printf("[HEALTH] Resolver %d (%s) unavailable; retry in %d seconds\n",
           index + 1, server_ips[index], RECOVERY_SECONDS);
}

static void refresh_health(void)
{
    time_t now = time(NULL);

    for (int i = 0; i < SERVER_COUNT; i++) {
        if (!health[i].healthy && now < health[i].retry_after)
            continue;

        char response[BUFFER_SIZE];

        if (query_server(i, probe_domains[i], response) == 0) {
            if (!health[i].healthy)
                printf("[HEALTH] Resolver %d recovered\n", i + 1);

            health[i].healthy = 1;
            health[i].failures = 0;
            health[i].retry_after = 0;
        } else {
            mark_failed(i);
        }
    }
}

static int resolve_domain(const char *domain, char *response)
{
    if (find_cache(domain, response)) {
        printf("[CACHE] %s -> %s\n", domain, response);
        return 0;
    }

    refresh_health();

    /*
     * Try the resolver most likely to hold this domain first.
     * All other healthy resolvers are used for failover.
     */
    int preferred = 0;

    if (strcmp(domain, "youtube.com") == 0 ||
        strcmp(domain, "microsoft.com") == 0 ||
        strcmp(domain, "github.com") == 0) {
        preferred = 1;
    } else if (strcmp(domain, "apple.com") == 0 ||
               strcmp(domain, "netflix.com") == 0 ||
               strcmp(domain, "wikipedia.org") == 0) {
        preferred = 2;
    }

    for (int attempt = 0; attempt < SERVER_COUNT; attempt++) {
        int index = (preferred + attempt) % SERVER_COUNT;

        if (!health[index].healthy &&
            time(NULL) < health[index].retry_after)
            continue;

        printf("[QUERY] %s -> resolver %d (%s)\n",
               domain, index + 1, server_ips[index]);

        if (query_server(index, domain, response) == 0) {
            if (strcmp(response, "NOT_FOUND") != 0 &&
                strcmp(response, "ERROR") != 0) {
                health[index].healthy = 1;
                health[index].failures = 0;
                health[index].retry_after = 0;
                add_cache(domain, response);
                printf("[SUCCESS] %s -> %s\n", domain, response);
                return 0;
            }

            printf("[MISS] Resolver %d does not have %s\n",
                   index + 1, domain);
        } else {
            mark_failed(index);
        }
    }

    snprintf(response, BUFFER_SIZE, "NOT_FOUND");
    return -1;
}

int main(void)
{
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(CONTROLLER_PORT);

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind controller");
        close(sockfd);
        return 1;
    }

    printf("DNS application controller listening on UDP port %d\n",
           CONTROLLER_PORT);
    printf("Resolvers: %s:%d, %s:%d, %s:%d\n",
           server_ips[0], ports[0],
           server_ips[1], ports[1],
           server_ips[2], ports[2]);

    while (1) {
        char domain[BUFFER_SIZE];
        char response[BUFFER_SIZE];

        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        memset(domain, 0, sizeof(domain));

        ssize_t n = recvfrom(sockfd, domain, sizeof(domain) - 1,
                             0, (struct sockaddr *)&client_addr,
                             &client_len);

        if (n < 0) {
            if (errno == EINTR)
                continue;
            perror("recvfrom controller");
            continue;
        }

        domain[n] = '\0';
        domain[strcspn(domain, "\r\n")] = '\0';

        printf("\n[CLIENT] Query: %s\n", domain);

        struct timespec lookup_start, lookup_end;
        clock_gettime(CLOCK_MONOTONIC, &lookup_start);

        int lookup_status = resolve_domain(domain, response);

        clock_gettime(CLOCK_MONOTONIC, &lookup_end);

        double latency_ms =
            (lookup_end.tv_sec - lookup_start.tv_sec) * 1000.0 +
            (lookup_end.tv_nsec - lookup_start.tv_nsec) / 1000000.0;

        printf("[LATENCY] %s: %.3f ms\n", domain, latency_ms);

        if (lookup_status != 0)
            printf("[RESULT] %s -> NOT_FOUND\n", domain);

        if (sendto(sockfd, response, strlen(response) + 1, 0,
                   (struct sockaddr *)&client_addr,
                   client_len) < 0) {
            perror("sendto client");
        }
    }

    close(sockfd);
    return 0;
}

