#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <errno.h>

#define NETLINK_MSG_TYPE 31
#define MAX_PAYLOAD 1024

int main()
{
	int sockfd;
	struct sockaddr_nl src_addr, dest_addr;
	struct nlmsghdr *nlh;
	struct msghdr msg;
	struct iovec iov;
	char buffer[MAX_PAYLOAD];

	sockfd = socket(AF_NETLINK, SOCK_RAW, NETLINK_MSG_TYPE);
	if (sockfd < 0) {
		perror("socket");
		return -1;
	}

	memset(&src_addr, 0, sizeof(src_addr));
	src_addr.nl_family = AF_NETLINK;
	src_addr.nl_pid = getpid(); /* Set the client's PID as the source */

	if (bind(sockfd, (struct sockaddr *)&src_addr, sizeof(src_addr)) < 0) {
		perror("bind");
		close(sockfd);
		return -1;
	}

	memset(&dest_addr, 0, sizeof(dest_addr));
	dest_addr.nl_family = AF_NETLINK;
	dest_addr.nl_pid = 0; /* Unicast to the kernel module */
	dest_addr.nl_groups = 0; /* No multicast group */

	nlh = (struct nlmsghdr *)malloc(NLMSG_SPACE(MAX_PAYLOAD));
	memset(nlh, 0, NLMSG_SPACE(MAX_PAYLOAD));
	nlh->nlmsg_len = NLMSG_SPACE(MAX_PAYLOAD);
	nlh->nlmsg_pid = getpid(); /* Set the client's PID as the sender */
	nlh->nlmsg_flags = 0;

	iov.iov_base = (void *)nlh;
	iov.iov_len = nlh->nlmsg_len;
	msg.msg_name = (void *)&dest_addr;
	msg.msg_namelen = sizeof(dest_addr);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	/* Send a message to the kernel module */
	strcpy(NLMSG_DATA(nlh), "Hello from client!");
	if (sendmsg(sockfd, &msg, 0) < 0) {
		perror("sendmsg");
		close(sockfd);
		return -1;
	}

	/* Receive messages from the kernel module */
	while (1) {
		memset(buffer, 0, sizeof(buffer));
		iov.iov_base = (void *)buffer;
		iov.iov_len = sizeof(buffer);
		if (recvmsg(sockfd, &msg, 0) < 0) {
			perror("recvmsg");
			close(sockfd);
			return -1;
		}
		nlh = (struct nlmsghdr *)msg.msg_iov[0].iov_base;
		printf("Received message from kernel: %s\n",
		       (char *)NLMSG_DATA(nlh));
	}

	free(nlh);
	close(sockfd);
	return 0;
}
