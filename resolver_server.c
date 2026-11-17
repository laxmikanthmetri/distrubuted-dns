#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "../common/protocol.h"


int find_ip(char *domain, char *ip)
{
    FILE *file;

    char stored_domain[100];
    char stored_ip[50];

    file = fopen("../data/server1.txt", "r");

    if (file == NULL)
    {
        printf("Could not open database.\n");
        return 0;
    }

    while (fscanf(file, "%99s %49s",
                  stored_domain,
                  stored_ip) == 2)
    {
        if (strcmp(domain, stored_domain) == 0)
        {
            strcpy(ip, stored_ip);

            fclose(file);

            return 1;
        }
    }

    fclose(file);

    return 0;
}


int main()
{
    int sockfd;

    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;

    socklen_t client_len;

    char domain[BUFFER_SIZE];
    char ip[50];


    sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    if (sockfd < 0)
    {
        perror("socket");
        return 1;
    }


    memset(&server_addr, 0,
           sizeof(server_addr));

    server_addr.sin_family = AF_INET;

    server_addr.sin_addr.s_addr =
        INADDR_ANY;

    server_addr.sin_port =
        htons(SERVER_PORT);


    if (bind(sockfd,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0)
    {
        perror("bind");
        return 1;
    }


    printf("DNS Resolution Server started.\n");
    printf("Listening on UDP port %d...\n",
           SERVER_PORT);


    while (1)
    {
        client_len = sizeof(client_addr);

        memset(domain, 0,
               sizeof(domain));

        recvfrom(sockfd,
                 domain,
                 sizeof(domain) - 1,
                 0,
                 (struct sockaddr *)&client_addr,
                 &client_len);


        printf("\nReceived query: %s\n",
               domain);


        if (find_ip(domain, ip))
        {
            printf("Found IP: %s\n", ip);

            sendto(sockfd,
                   ip,
                   strlen(ip) + 1,
                   0,
                   (struct sockaddr *)&client_addr,
                   client_len);
        }
        else
        {
            printf("Domain not found.\n");

            char message[] = "NOT_FOUND";

            sendto(sockfd,
                   message,
                   strlen(message) + 1,
                   0,
                   (struct sockaddr *)&client_addr,
                   client_len);
        }
    }


    close(sockfd);

    return 0;
}
