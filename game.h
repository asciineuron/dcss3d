#ifndef GAME_H
#define GAME_H

#include <stdint.h>

// for frame-level key state, not key-poll level changes e.g. open inventory, quit
enum frame_keys {
	FRAME_KEY_NONE = 0,
	FRAME_KEY_W = 1,
	FRAME_KEY_A = 1 << 2,
	FRAME_KEY_S = 1 << 3,
	FRAME_KEY_D = 1 << 4,
	FRAME_KEY_LSHIFT = 1 << 5
};

struct player {
	// TODO move to e.g. camera?
	float vel_x, vel_y;
	int pos_x, pos_y; // game tile pos, not render float pos
	enum frame_keys keystate;
};

// DCSS defaults to 15x15 square LOS for most species, use for now
#define MAX_MAP_VISIBLE 225

enum map_type { MTYPE_WALL, MTYPE_FLOOR, MTYPE_UNKNOWN, MTYPE_COUNT };

// does dcss have negative coords or is 0 at corner?
struct map_coord {
	int x, y;
};

struct map_pos_info {
	struct map_coord coord;
	enum map_type type;
	// etc.
};

// to measure time difference for steady velocity:
// dt seconds elapsed since last frame
struct game_time {
	uint64_t cur_tick;
	uint64_t last_tick;
	double dt;
	uint64_t game_turn;
};

struct game_context {
	struct map_pos_info visible_map[MAX_MAP_VISIBLE];
	struct player *player;
	struct game_time time;
};

void game_update_time(struct game_context *ctx);

#endif
