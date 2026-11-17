#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "../common/protocol.h"


int main()
{
    int sockfd;

    struct sockaddr_in controller_addr;

    socklen_t controller_len;

    char domain[BUFFER_SIZE];

    char response[BUFFER_SIZE];


    /*
       Create UDP socket
    */

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    if (sockfd < 0)
    {
        perror("socket");
        return 1;
    }


    /*
       Configure controller address
    */

    memset(&controller_addr, 0,
           sizeof(controller_addr));

    controller_addr.sin_family = AF_INET;

    controller_addr.sin_port =
        htons(CONTROLLER_PORT);

    inet_pton(AF_INET,
              "127.0.0.1",
              &controller_addr.sin_addr);


    while (1)
    {
        printf("\nEnter domain (or exit): ");

        scanf("%99s", domain);


        if (strcmp(domain, "exit") == 0)
        {
            break;
        }


        /*
           Send domain to controller
        */

        sendto(sockfd,
               domain,
               strlen(domain) + 1,
               0,
               (struct sockaddr *)&controller_addr,
               sizeof(controller_addr));


        /*
           Receive response from controller
        */

        controller_len =
            sizeof(controller_addr);

        memset(response, 0,
               sizeof(response));


        recvfrom(sockfd,
                 response,
                 sizeof(response) - 1,
                 0,
                 (struct sockaddr *)&controller_addr,
                 &controller_len);


        printf("IP Address: %s\n",
               response);
    }


    close(sockfd);

    return 0;
}
