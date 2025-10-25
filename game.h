#ifndef GAME_H
#define GAME_H

#include "cglm/include/cglm/cglm.h"

#include <stddef.h>
#include <stdbool.h>
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

// need to sync player and camera pos. player's is just int clipping. when it changes send move turn
struct camera {
	vec3 pos; // x,y,z
	float fov;
	float aspect_ratio;
	float theta;
	float phi;
};

struct player {
	struct camera camera;
	float vel_x, vel_y;
	int pos_x, pos_y; // game tile pos, not render float pos
	enum frame_keys keystate;
};

// player or just its camera? view can be camera only, pos needs to do extra work
void update_player_view(struct player *player, float mouse_dx, float mouse_dy);
// do collision detection here:
struct turn *update_player_pos(struct player *player, double dt);

// DCSS defaults to 15x15 square LOS for most species, use for now
#define MAX_MAP_VISIBLE 225

// use MTYPE_NONE as nonvisible tile. can use first instance to terminate visible_map list
// maybe too complicated, for now just set all MAX_MAP_VISIBLE to MTYPE_NONE, then skip shader output if so
enum map_type {
	MTYPE_NONE,
	MTYPE_WALL,
	MTYPE_FLOOR,
	MTYPE_UNEXPLORED,
	MTYPE_UNKNOWN,
	MTYPE_COUNT
};

// does dcss have negative coords or is 0 at corner?
struct map_coord {
	float x, y;
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
	// means this frame loop update everything again for the new layout,
	// otherwise skip assume same as before:
	bool map_needs_change;
};

void game_update_time(struct game_context *ctx);

void print_map_pos_info(struct map_pos_info *visible_map, size_t map_size);

#endif
