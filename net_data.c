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
static uint32_t cur_msg_max_size; // buffer size, largest message received so far

int msg_idx;

struct pollfd fds[1];

// for each mf we see
// supposedly 26 = unexplored is the last
#define MF_MAX 26
static enum map_type mf_to_map_type[MF_MAX + 1];

bool net_data_init(void)
{
	// already set up
	if (sock_fd)
		return true;

	// set map network type to internal type correspondence
	for (int i = 0; i < MF_MAX + 1; ++i) {
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
	// strcpy(cur_msg, "waiting for message...");

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

char *turn_to_message(const struct turn *turn)
{
	// return "this is a test turn message";
	// TODO: look into special keys e.g. South East “key_dir_se” {“msg”: “input”, “text”: “3”}
	// return "{\"msg\": \"input\", \"text\": \".\"}";

	char msg_detail[5] = {0};
	switch (turn->type) {
	case TURN_MOVE:
		switch (turn->value.move) {
		case MOVE_N:
			msg_detail[0] = '8';
			break;
		case MOVE_S:
			msg_detail[0] = '2';
			break;
		case MOVE_E:
			msg_detail[0] = '6';
			break;
		case MOVE_W:
			msg_detail[0] = '4';
			break;
		default:
			break;
		}
		break;
	default:
		msg_detail[0] = '.';
		break;
	}
	char message[100];
	snprintf(message, 100, "{\"msg\": \"input\", \"text\": \"%s\"}", msg_detail);
	return strdup(message);
}

bool send_turn_message(const char *message)
{
	uint32_t msgsz = (uint32_t)strlen(message);
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

static bool poll_turn_response(int timeout)
{
	// for fixed header-sized messages, define this to be our message interface: {uint32_t len, message}
	// wait until readable POLLIN
	int poll_res = poll(fds, 1, timeout);

	if (poll_res < 0) {
		perror("poll error");
		return false;
	} else if (poll_res == 0) {
		// TODO: refactor into a separate thread so we don't have to poll each game loop? not sure how expensive
		// log_trace("no poll turn response");
		return true;
	}

	if (fds[0].revents & POLLHUP) {
		fprintf(stderr, "poll hangup\n");
		return false;
	}

	// read size header, set up appropriately sized message buffer
	uint32_t len;
	if (recv(sock_fd, &len, sizeof(len), 0) != sizeof(len)) {
		perror("recv header len failed");
		return false;
	}
	fprintf(stderr, "received len: %u\n", len);

	// >= since we need to add an additional '\0'
	if (len >= cur_msg_max_size) {
		if ((cur_msg = realloc(cur_msg, len + 1)) == NULL) {
			fputs("failed to realloc message buffer", stderr);
			return false;
		}
		cur_msg_max_size = len + 1;
	}

	uint32_t bytes_read = 0;
	uint32_t bytes_remaining = len;
	while (bytes_remaining > 0) {
		if ((len = recv(sock_fd, cur_msg + bytes_read, bytes_remaining,
				0)) == -1) {
			perror("recv failed");
			return false;
		}
		bytes_read += len;
		bytes_remaining -= len;

		// print partial message:
		// cur_msg[bytes_read] = '\0';
		// log_trace("cur_msg: %s", cur_msg);
	}
	cur_msg[bytes_read] = '\0';
	log_trace("cur_msg: %s", cur_msg);

	msg_idx++;

	// TODO make a new buffer each time?
	// read from cur_msg instead of directly returning, return error instead
	return true;
}

bool get_turn_response(void)
{
	// for fixed header-sized messages, define this to be our message interface: {uint32_t len, message}

	// wait until readable POLLIN
	// if (poll(fds, 1, -1) < 1) {
	// 	perror("poll error or not ready");
	// 	return NULL;
	// }
	// if (fds[0].revents & POLLHUP) {
	// 	fprintf(stderr, "poll hangup\n");
	// 	return NULL;
	// }

	// // read size header, set up appropriately sized message buffer
	// uint32_t len;
	// if (recv(sock_fd, &len, sizeof(len), 0) != sizeof(len)) {
	// 	perror("recv header len failed");
	// 	return NULL;
	// }
	// fprintf(stderr, "received len: %u\n", len);

	// // >= since we need to add an additional '\0'
	// if (len >= cur_msg_max_size) {
	// 	if ((cur_msg = realloc(cur_msg, len + 1)) == NULL) {
	// 		fputs("failed to realloc message buffer", stderr);
	// 		return NULL;
	// 	}
	// 	cur_msg_max_size = len + 1;
	// }

	// uint32_t bytes_read = 0;
	// uint32_t bytes_remaining = len;
	// while (bytes_remaining > 0) {
	// 	if ((len = recv(sock_fd, cur_msg + bytes_read, bytes_remaining,
	// 			0)) == -1) {
	// 		perror("recv failed");
	// 		return NULL;
	// 	}
	// 	bytes_read += len;
	// 	bytes_remaining -= len;

	// 	// print partial message:
	// 	// cur_msg[bytes_read] = '\0';
	// 	// log_trace("cur_msg: %s", cur_msg);
	// }
	// cur_msg[bytes_read] = '\0';
	// log_trace("cur_msg: %s", cur_msg);

	// msg_idx++;

	// // TODO make a new buffer each time?
	// return cur_msg;
	return poll_turn_response(-1);
}

bool check_game_response(void)
{
	return poll_turn_response(0);
}

static bool response_is_map(const cJSON *response)
{
	if (!response)
		return false;
	cJSON *msg = cJSON_GetObjectItemCaseSensitive(response, "msg");
	return msg && cJSON_IsString(msg) && msg->valuestring &&
	       (strcmp(msg->valuestring, "map") == 0);
}

static bool update_map_from_json(struct game_context *ctx,
				 const cJSON *response_json)
{
	log_trace("updating map");
	// assumes json actually has map data, validate in process_turn_response()
	bool ret = true;

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

				// TODO tile coord should be int?
				tile_info.coord.x = (float)cell_elem->valueint;
				has_x = true;
			} else if (strcmp(cell_elem->string, "y") == 0) {
				if (!cJSON_IsNumber(cell_elem)) {
					log_err("cell_elem y json element is not a number: %f",
						cell_elem->valuedouble);
					ret = false;
					goto exit;
				}

				tile_info.coord.y = (float)cell_elem->valueint;
			} else if (strcmp(cell_elem->string, "mf") == 0) {
				if (!cJSON_IsNumber(cell_elem)) {
					log_err("cell_elem mf json element is not a number: %f",
						cell_elem->valuedouble);
					ret = false;
					goto exit;
				}
				assert(cell_elem->valueint <= MF_MAX);

				tile_info.type =
					mf_to_map_type[cell_elem->valueint];
			}
			// TODO: add remaining cells info
		}
		if (!has_x)
			++tile_info.coord.x;

		ctx->visible_map[cell_idx] = tile_info;
		// ++cell_idx;
		if (++cell_idx == MAX_MAP_VISIBLE)
			break;
	}
	log_trace("num map sites loaded: %d", cell_idx);
	// print_map_pos_info(ctx->visible_map, cell_idx);
exit:
	return ret;
}

bool process_turn_response(const char *response, struct game_context *ctx)
{
	// TODO: loop here since it may take several messages before a map is sent again

	bool ret = true;

	cJSON *response_json = cJSON_Parse(response);

	// char *response_print = cJSON_Print(response_json);
	// log_trace("response json: %s", response_print);
	// free(response_print);

	// TODO: see if another way if just checking, not using 'cells' here
	// if (cJSON_GetObjectItemCaseSensitive(response_json, "cells")) {
	if (response_is_map(response_json)) {
		if (!update_map_from_json(ctx, response_json)) {
			ret = false;
			goto exit;
		}
	}

exit:
	cJSON_Delete(response_json);
	return ret;
}

bool load_initial_map(struct game_context *ctx)
{
	// read until map is updated
	bool map_updated = false;
	bool res = true;
	cJSON *response_json = NULL;
	while (!map_updated) {
		if (!cur_msg) {
			res = get_turn_response();
			if (!res)
				goto exit;
		}
		cJSON_Delete(response_json);
		response_json = cJSON_Parse(cur_msg);
		if (!response_json) {
			const char *error_ptr = cJSON_GetErrorPtr();
			if (error_ptr)
				log_err("cJSON error: %s", error_ptr);
		}
		// char *response_print = cJSON_Print(response_json);
		// log_trace("response json: %s", response_print);
		// free(response_print);
		if (!response_is_map(response_json)) {
			log_trace("current response is not a map");
			res = get_turn_response();
			if (!res)
				goto exit;
			continue;
		}
		res = update_map_from_json(ctx, response_json);
		if (res) {
			map_updated = true;
			goto exit;
		} else {
			log_trace("update_map_from_json failed");
		}
	}
exit:
	cJSON_Delete(response_json);
	return res;
}
