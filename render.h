#ifndef RENDER_H
#define RENDER_H

#include "game.h"

#include <stdbool.h>

#include <SDL3/SDL.h>

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

#endif
