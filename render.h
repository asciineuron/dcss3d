#ifndef RENDER_H
#define RENDER_H

#include "game.h"

#include <stdbool.h>

// TODO: add cglm/include to include path
#include "cglm/include/cglm/cglm.h"
#include <SDL3/SDL.h>

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

void camera_update_from_mouse(float mouse_dx, float mouse_dy);
void camera_update_pos(double dt, float vx, float vy);

#endif
