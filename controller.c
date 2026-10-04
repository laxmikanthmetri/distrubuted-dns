#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "../common/protocol.h"


int get_server_port(char *domain)
{
    if (strcmp(domain, "google.com") == 0 ||
        strcmp(domain, "amazon.com") == 0 ||
        strcmp(domain, "facebook.com") == 0)
    {
        return SERVER1_PORT;
    }


    if (strcmp(domain, "youtube.com") == 0 ||
        strcmp(domain, "microsoft.com") == 0 ||
        strcmp(domain, "github.com") == 0)
    {
        return SERVER2_PORT;
    }


    if (strcmp(domain, "apple.com") == 0 ||
        strcmp(domain, "netflix.com") == 0 ||
        strcmp(domain, "wikipedia.org") == 0)
    {
        return SERVER3_PORT;
    }


    return 0;
}


int main()
{
    int sockfd;

    int server_port;

    int server_sockfd;

    struct sockaddr_in controller_addr;
    struct sockaddr_in client_addr;
    struct sockaddr_in server_addr;

    socklen_t client_len;
    socklen_t server_len;

    char domain[BUFFER_SIZE];
    char response[BUFFER_SIZE];


    /*
       Create controller socket
    */

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    if (sockfd < 0)
    {
        perror("controller socket");
        return 1;
    }


    /*
       Configure controller address
    */

    memset(&controller_addr, 0,
           sizeof(controller_addr));

    controller_addr.sin_family = AF_INET;

    controller_addr.sin_addr.s_addr =
        INADDR_ANY;

    controller_addr.sin_port =
        htons(CONTROLLER_PORT);


    /*
       Bind controller to port 5000
    */

    if (bind(sockfd,
             (struct sockaddr *)&controller_addr,
             sizeof(controller_addr)) < 0)
    {
        perror("controller bind");
        return 1;
    }


    printf("SDN-like Controller started.\n");

    printf("Listening on UDP port %d...\n",
           CONTROLLER_PORT);


    while (1)
    {
        /*
           Receive query from client
        */

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


        /*
           Decide which resolver server
           should receive the query
        */

        server_port =
            get_server_port(domain);


        if (server_port == 0)
        {
            printf("No server found for this domain.\n");

            char message[] = "NOT_FOUND";

            sendto(sockfd,
                   message,
                   strlen(message) + 1,
                   0,
                   (struct sockaddr *)&client_addr,
                   client_len);

            continue;
        }


        printf("Forwarding request to port %d...\n",
               server_port);


        /*
           Create socket for communication
           with resolver server
        */

        server_sockfd =
            socket(AF_INET,
                   SOCK_DGRAM,
                   0);

        if (server_sockfd < 0)
        {
            perror("server socket");
            continue;
        }


        /*
           Configure resolver server address
        */

        memset(&server_addr, 0,
               sizeof(server_addr));

        server_addr.sin_family = AF_INET;

        server_addr.sin_port =
            htons(server_port);

        inet_pton(AF_INET,
                  "127.0.0.1",
                  &server_addr.sin_addr);


        /*
           Send domain to resolver server
        */

        sendto(server_sockfd,
               domain,
               strlen(domain) + 1,
               0,
               (struct sockaddr *)&server_addr,
               sizeof(server_addr));


        /*
           Receive response from resolver server
        */

        server_len =
            sizeof(server_addr);

        memset(response, 0,
               sizeof(response));


        recvfrom(server_sockfd,
                 response,
                 sizeof(response) - 1,
                 0,
                 (struct sockaddr *)&server_addr,
                 &server_len);


        printf("Server response: %s\n",
               response);


        /*
           Send response back to client
        */

        sendto(sockfd,
               response,
               strlen(response) + 1,
               0,
               (struct sockaddr *)&client_addr,
               client_len);


        close(server_sockfd);
    }


    close(sockfd);

    return 0;
}
