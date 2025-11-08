#ifndef RENDER_H
#define RENDER_H

#include "game.h"

// TODO: add cglm/include to include path
#include "cglm/include/cglm/cglm.h"
#include <SDL3/SDL.h>

#include <stdbool.h>

#define FOV_RAD 0.785398
// TODO for mouse fix for x vs y?
#define FOV_DEG 45
#define ASPECT 1.777777

// externally accessible stat subset
struct render_info {
	SDL_Window *window;
	SDL_WindowID window_id;
	int win_w, win_h;
};

extern struct render_info rend_info;

bool render_init();
bool render_draw(const struct game_context *game_ctx);
void render_quit();

inline void map_tile_to_render_coords(struct map_coord coord, vec3 dest)
{
	// returns the center coordinates in render space of the map game-coords
	// cube extends +-0.5 xyz i.e. width = 1.0
	dest[0] = ((float)coord.x * 1.0f) + 0.5f;
	dest[1] = ((float)coord.y * 1.0f) - 0.5f;
}

inline void render_coords_llx_lly_urx_ury(vec3 coords, vec4 dest)
{
	// this should be even numbers ie the ints we cross the map over
	// llx:
	dest[0] = coords[0] - 0.5f;
	// lly:
	dest[1] = coords[1] + 0.5f;
	// urx:
	dest[2] = coords[0] + 0.5f;
	// ury:
	dest[3] = coords[1] - 0.5f;
}


#endif
