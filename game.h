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

// clipping box to apply for collision detection around camera float position
#define PLAYER_BBOX_WIDTH 0.1

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
void update_player_view(struct player *player, const float mouse_dx, const float mouse_dy);

// DCSS defaults to 15x15 square LOS for most species, use for now
// #define MAX_MAP_VISIBLE 225
// #define MAX_MAP_VISIBLE 600
#define MAX_MAP_VISIBLE 300

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
	float x, y; // should be int? that's how game represents it
};

struct map_pos_info {
	struct map_coord coord;
	enum map_type type;
	// etc.
};

// do collision detection here:
struct turn *update_player_pos(struct player *player, const double dt, const struct map_pos_info *visible_map, const size_t visible_map_size);

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

void print_map_pos_info(const struct map_pos_info *visible_map, const size_t map_size);

#endif
