// SPDX-License-Identifier: GPL-2.0
/* Test for 100_netlink_socket: send a string, print the upper-cased echo. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <linux/netlink.h>

#define NETLINK_TRAINING 31
#define MAX_PAYLOAD 256

int main(int argc, char **argv)
{
	const char *msg = argc > 1 ? argv[1] : "hello from user space";
	struct sockaddr_nl src = { .nl_family = AF_NETLINK, .nl_pid = getpid() };
	struct sockaddr_nl dst = { .nl_family = AF_NETLINK, .nl_pid = 0 };
	char buf[NLMSG_SPACE(MAX_PAYLOAD)];
	struct nlmsghdr *nlh = (struct nlmsghdr *)buf;
	int fd;

	fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_TRAINING);
	if (fd < 0) {
		perror("socket (is 100_netlink_socket loaded?)");
		return 1;
	}
	if (bind(fd, (struct sockaddr *)&src, sizeof(src)) < 0) {
		perror("bind");
		return 1;
	}

	memset(buf, 0, sizeof(buf));
	nlh->nlmsg_len = NLMSG_LENGTH(strlen(msg) + 1);
	nlh->nlmsg_pid = getpid();
	strncpy(NLMSG_DATA(nlh), msg, MAX_PAYLOAD - 1);

	if (sendto(fd, nlh, nlh->nlmsg_len, 0, (struct sockaddr *)&dst, sizeof(dst)) < 0) {
		perror("sendto");
		return 1;
	}
	if (recv(fd, buf, sizeof(buf), 0) < 0) {
		perror("recv");
		return 1;
	}
	printf("kernel replied: %s\n", (char *)NLMSG_DATA(nlh));
	close(fd);
	return 0;
}
