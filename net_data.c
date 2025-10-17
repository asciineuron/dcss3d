#include "net_data.h"
#include <poll.h>
#include <stdio.h>
#include <stdlib.h> // abort
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

// for macos 104, for linux 108, see sockaddr_un.sun_path[104]
#define SUN_PATH_MAX 104

char sock_name[SUN_PATH_MAX];

static int sock_fd;

#define MSG_INIT_LEN 2048

char *cur_msg;
static size_t cur_msg_max_size; // buffer size, largest message received so far

int msg_idx;

struct pollfd fds[1];

bool net_data_init(void)
{
	// already set up
	if (sock_fd)
		return true;

	cur_msg = malloc(MSG_INIT_LEN);
	if (!cur_msg) {
		fputs("failed to call malloc", stderr);
		return false;
	}

	// dummy init msg string
	strcpy(cur_msg, "waiting for message...");

	if ((sock_fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
		perror("socket creation failed");
		return false;
	}

	if (!getcwd(sock_name, SUN_PATH_MAX)) {
		perror("getcwd failed");
		return false;
	}
	strcat(sock_name, "/sdlproj1.sock");
	fprintf(stderr, "socket path: %s\n", sock_name);

	struct sockaddr_un remote = { .sun_family = PF_LOCAL };
	strcpy(remote.sun_path, sock_name);

	if (connect(sock_fd, (struct sockaddr *)&remote,
		    sizeof(struct sockaddr_un)) == -1) {
		perror("socket connect failed");
		return false;
	}

	fds[0] = (struct pollfd){ .fd = sock_fd, .events = POLLIN };

	return true;
}

bool net_data_exit(void)
{
	close(sock_fd);
	unlink(sock_name);
	return true;
}

const char *turn_to_message(const struct turn *turn)
{
	return "this is a test turn message";
}

bool send_turn_message(const char *message)
{
	size_t msgsz = (size_t)strlen(message);
	// don't include \0 here, client will have to pad own received string
	if (send(sock_fd, &msgsz, sizeof(msgsz), 0) != sizeof(msgsz)) {
		perror("send failed");
		return false;
	}

	// don't include the \0 in send? since we're sending raw json, not a string"
	int tot_sent = 0;
	int this_send = 0;
	while (tot_sent < msgsz) {
		if ((this_send = send(sock_fd, message + tot_sent,
				      msgsz - tot_sent, 0)) < 1) {
			// 0 for disconnect is also fatal
			perror("send failed");
			return false;
		}
		tot_sent += this_send;
	}
	return true;
}

const char *get_turn_response(void)
{
	// for fixed header-sized messages, define this to be our message interface: {size_t len, message}

	// wait until readable POLLIN
	if (poll(fds, 1, -1) < 1) {
		fprintf(stderr, "poll error or not ready\n");
		return NULL;
	}
	if (fds[0].revents & POLLHUP) {
		fprintf(stderr, "poll hangup\n");
		return NULL;
	}

	// read size header, set up appropriately sized message buffer
	size_t len;
	if (recv(sock_fd, &len, sizeof(len), 0) != sizeof(len)) {
		perror("recv header len failed");
		return NULL;
	}
	fprintf(stderr, "received len: %zu\n", len);

	// >= since we need to add an additional '\0'
	if (len >= cur_msg_max_size) {
		if ((cur_msg = realloc(cur_msg, len + 1)) == NULL) {
			fputs("failed to realloc message buffer", stderr);
			return NULL;
		}
		cur_msg_max_size = len + 1;
	}

	size_t bytes_read = 0;
	size_t bytes_remaining = len;
	while (bytes_remaining > 0) {
		if ((len = recv(sock_fd, cur_msg + bytes_read, bytes_remaining,
				0)) == -1) {
			perror("recv failed");
			return NULL;
		}
		bytes_read += len;
		bytes_remaining -= len;
	}
	cur_msg[bytes_read] = '\0';

	msg_idx++;

	// TODO make a new buffer each time?
	return cur_msg;
}
