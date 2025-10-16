// reads stdin and prints len,msg on the socket, and prints anything received to stdout
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

// #define STRING(x) #x
// #define XSTRING(x) STRING(x)

// #define SOCK_NAME XSTRING(SOURCE_ROOT) "/sdlproj1.sock"

int sock_fd;
int client_fd;

#define SUN_PATH_MAX 104

char socket_name[SUN_PATH_MAX];

#define INPUT_LEN 500
char input[INPUT_LEN];
char client_input[INPUT_LEN];

void cleanup(void)
{
	close(client_fd);
	close(sock_fd);
	unlink(socket_name);
}

int main(int argc, char *argv[])
{
	// TODO: works for exit(), but not Ctrl-C i.e. SIGINT
	atexit(cleanup);

	if ((sock_fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
		perror("socket creation failed");
		exit(EXIT_FAILURE);
	}

	if (!getcwd(socket_name, SUN_PATH_MAX)) {
		perror("getcwd failed");
		exit(EXIT_FAILURE);
	}
	strcat(socket_name, "/sdlproj1.sock");
	printf("socket path: %s\n", socket_name);

	struct sockaddr_un remote = { .sun_family = PF_LOCAL };
	socklen_t remote_len = sizeof(remote);

	struct sockaddr_un local = { .sun_family = PF_LOCAL };
	strcpy(local.sun_path, socket_name);

	bind(sock_fd, (struct sockaddr *)&local, sizeof(struct sockaddr_un));

	listen(sock_fd, 10);

	// do this up here to hang onto single connection rather than closing each time
	client_fd = accept(sock_fd, (struct sockaddr *)&remote, &remote_len);

	// POLLHUP shouldn't be specified in .events, auto present in .revents?
	struct pollfd fds[2] = {
		{ .fd = client_fd, .events = POLLIN | POLLHUP },
		{ .fd = STDIN_FILENO, .events = POLLIN | POLLHUP }
	};

	ssize_t recv_len = 0;
	size_t input_len = 0;

	size_t msg_len = 0; // {len, buffer} encoding

	// hang onto client. detect if client disconnects, and
	// only then close() it. rather than closing after sending one message...
	while (poll(fds, 2, -1) > 0) {
		// if we are in this loop, assume client open until after receiving its data,
		// since that's the only time the connection will shut down typically
		// ...after checking pollhup
		if (fds[0].revents & POLLHUP)
			break;
		if (fds[1].revents & POLLIN) {
			fgets(input, INPUT_LEN, stdin);

			// trim fgets-included newline if any:
			input[strcspn(input, "\n")] = '\0';

			input_len = (size_t)strlen(input);
			if (input_len == 0) {
				continue;
			}

			printf("message from stdin, len %zu:\n%s\n", input_len,
			       input);

			// len header
			if (send(client_fd, &input_len, sizeof(input_len), 0) !=
			    sizeof(input_len)) {
				perror("send failed");
				exit(EXIT_FAILURE);
			}

			// body
			// loop to ensure full message sent:
			size_t tot_sent = 0;
			size_t this_send = 0;
			while (tot_sent < input_len) {
				if ((this_send = send(
					     client_fd, input + tot_sent,
					     input_len - tot_sent, 0)) < 1) {
					perror("send failed");
					exit(EXIT_FAILURE);
				}
				tot_sent += this_send;
			}
		}
		if (fds[0].revents & POLLIN) {
			// TODO: add startup option. This is for human plaintext input:
			// recv_len = recv(client_fd, client_input, INPUT_LEN-1, 0);
			// if (recv_len == -1) {
			// 	perror("recv failed");
			// 	exit(EXIT_FAILURE);
			// } else if (recv_len == 0) {
			// 	printf("client disconnected\n");
			// 	break; // quit while loop
			// }
			// client_input[recv_len] = '\0';
			// client_input[strcspn(client_input, "\n")] = '\0';

			// and this is for length + buffer encoded code messages:
			if (recv(client_fd, &msg_len, sizeof(msg_len), 0) !=
			    sizeof(msg_len)) {
				printf("unable to recv message size header");
				exit(EXIT_FAILURE);
			}
			if (recv(client_fd, client_input, msg_len, 0) !=
			    msg_len) {
				printf("unable to recv full message body");
				exit(EXIT_FAILURE);
			}
			client_input[msg_len] =
				'\0'; // msg_len = strlen so need extra trailing '\0'

			fprintf(stdout, "message from socket:\n%s\n",
				client_input);
		}
	}
}
