#include "net_data.h"
#include "game.h"
#include "log.h"
#include "cJSON.h"
#include "game.h"
#include <assert.h>
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

// for each mf we see
// supposedly 26 = unexplored is the last
#define MF_MAX 26
static enum map_type mf_to_map_type[MF_MAX+1];

bool net_data_init(void)
{
	// already set up
	if (sock_fd)
		return true;

	// set map network type to internal type correspondence
	for (int i = 0; i < MF_MAX+1; ++i) {
		mf_to_map_type[i] = MTYPE_UNKNOWN;
	}
	mf_to_map_type[1] = MTYPE_FLOOR;
	mf_to_map_type[2] = MTYPE_WALL;
	mf_to_map_type[26] = MTYPE_UNEXPLORED;

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
	// log_trace("cur_msg: %s", cur_msg);

	msg_idx++;

	// TODO make a new buffer each time?
	return cur_msg;
}

bool process_turn_response(const char *response, struct game_context *ctx)
{
	bool ret = true;

	cJSON *response_json = cJSON_Parse(response);

	char *response_print = cJSON_Print(response_json);
	log_info("response json: %s", response_print);

	// for now expect msg: map, cells: array of object with xys
	const cJSON *cells =
		cJSON_GetObjectItemCaseSensitive(response_json, "cells");
	if (!cells) {
		ret = false;
		goto exit;
	}

	// first reset ctx->visible_map
	memset(ctx->visible_map, 0,
	       MAX_MAP_VISIBLE * sizeof(struct map_pos_info));

	/*
	 * retain x and y unless updated
	 * start at xmin, ymin, each elem implicitly increments x
	 * "Only the first tile in a row, or the first tile after a 
	 * series of “empty cells” in a row (cells not sent), will 
	 * contain the x and y value"
	*/
	cJSON *cell;
	int cell_idx = 0;
	struct map_pos_info tile_info = {};
	cJSON_ArrayForEach(cell, cells)
	{
		// reset tile info
		tile_info.type = MTYPE_UNKNOWN;
		bool has_x = false;

		cJSON *cell_elem;
		cJSON_ArrayForEach(cell_elem, cell)
		{

			if (strcmp(cell_elem->string, "x") == 0) {
				if (!cJSON_IsNumber(cell_elem)) {
					log_err("cell_elem x json element is not a number: %f, %s, %s",
						cell_elem->valuedouble,
						cell_elem->string,
						cell_elem->valuestring);
					ret = false;
					goto exit;
				}
				tile_info.coord.x = cell_elem->valueint;
				has_x = true;
			} else if (strcmp(cell_elem->string, "y") == 0) {
				if (!cJSON_IsNumber(cell_elem)) {
					log_err("cell_elem y json element is not a number: %f",
						cell_elem->valuedouble);
					ret = false;
					goto exit;
				}
				tile_info.coord.y = cell_elem->valueint;
			} else if (strcmp(cell_elem->string, "mf") == 0) {
				if (!cJSON_IsNumber(cell_elem)) {
					log_err("cell_elem mf json element is not a number: %f",
						cell_elem->valuedouble);
					ret = false;
					goto exit;
				}
				assert(cell_elem->valueint <= MF_MAX);
				tile_info.type = mf_to_map_type[cell_elem->valueint];
			}
			// TODO: add remaining cells info


		}
		if (!has_x)
			++tile_info.coord.x;

		ctx->visible_map[cell_idx] = tile_info;
		++cell_idx;
	}

	// not printing here...
	print_map_pos_info(ctx->visible_map, cell_idx);
exit:
	cJSON_Delete(response_json);
	return ret;
}
