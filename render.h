#ifndef RENDER_H
#define RENDER_H

#include <SDL3/SDL.h>

// these x, y have to be different
struct camera {
	float x;
	float y;
	float z;
	float fov;
	float aspect_ratio;
	float theta;
	float phi;
};

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
	struct camera camera;
	float vel_x, vel_y;
	enum frame_keys keystate;
};

// does dcss have negative coords or is 0 at corner?
struct map_coord {
	int x, y;
};

enum map_type { MTYPE_WALL, MTYPE_FLOOR, MTYPE_UNKNOWN };

struct map_pos_info {
	struct map_coord coord;
	enum map_type type;
	// etc.
};

// to measure time difference for steady velocity:
// dt seconds elapsed since last frame
struct time {
	uint64_t cur_tick;
	uint64_t last_tick;
	double dt;
	uint64_t game_turn;
};

// DCSS defaults to 15x15 square LOS for most species, use for now
#define MAX_MAP_VISIBLE 225

struct game_context {
	struct map_pos_info[MAX_MAP_VISIBLE] visible_map;
	struct player *player;
	struct time time;
};

bool render_init();
bool render_draw(const struct game_context *game_ctx);
void render_quit();

// externally accessible stat subset
struct render_info {
	SDL_Window *window;
	SDL_WindowID window_id;
	int win_w, win_h;
};

extern struct render_info rend_info;

#endif
