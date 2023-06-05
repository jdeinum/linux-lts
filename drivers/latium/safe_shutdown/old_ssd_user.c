#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <linux/netlink.h>

#define MAX_PAYLOAD 1024
#define NETLINK_USER 31

int main(void)
{
    struct sockaddr_nl addr;
    int sock, ret;
    struct nlmsghdr *nlh;
    struct iovec iov;
    struct msghdr msg;
    char payload[MAX_PAYLOAD];

    // Create a netlink socket
    sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_USER);
    if (sock < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    // Bind the socket to the kernel netlink family
    memset(&addr, 0, sizeof(addr));
    addr.nl_family = AF_NETLINK;
    addr.nl_pid = getpid();
    addr.nl_groups = 0;
    ret = bind(sock, (struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0) {
        perror("bind");
        close(sock);
        return EXIT_FAILURE;
    }

    // Wait for messages from the kernel module and request a shutdown when a "shutdown" message is received
    while (1) {
        ret = recv(sock, payload, sizeof(payload), 0);
        if (ret < 0) {
            perror("recvmsg");
            close(sock);
            return EXIT_FAILURE;
        }
        printf("Received message from kernel module: %s\n", payload);

        if (strcmp(payload, "shutdown") == 0) {
            // Request a shutdown using systemd
            ret = system("systemctl poweroff");
            if (ret < 0) {
                perror("system");
                close(sock);
                return EXIT_FAILURE;
            }
            printf("Requested system shutdown\n");
        }
    }

    close(sock);
    return EXIT_SUCCESS;
}
