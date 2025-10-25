#include "game.h"
#include "log.h"
#include "net_data.h"
#include "render.h"
#include "turn.h"

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#define VELOCITY 1.0

bool done = false;

// handle current state of logical keyboard (infrequent at key-poll rate, not per-frame)
struct turn *process_key(SDL_KeyboardEvent *key_event,
			 struct game_context *game_ctx)
{
	struct turn *turn = NULL;
	if (key_event->type == SDL_EVENT_KEY_UP) {
		// TODO: add shift?
		enum frame_keys off_keys = FRAME_KEY_NONE;
		switch (key_event->scancode) {
		case SDL_SCANCODE_Q:
			done = true;
			break;
		case SDL_SCANCODE_W:
			off_keys |= FRAME_KEY_W;
			break;
		case SDL_SCANCODE_S:
			off_keys |= FRAME_KEY_S;
			break;
		case SDL_SCANCODE_A:
			off_keys |= FRAME_KEY_A;
			break;
		case SDL_SCANCODE_D:
			off_keys |= FRAME_KEY_D;
			break;
		case SDL_SCANCODE_LSHIFT:
			off_keys |= FRAME_KEY_LSHIFT;
			break;
		case SDL_SCANCODE_SPACE:
			turn = malloc(sizeof(struct turn));
			*turn = (struct turn){ .type = TURN_MOVE,
					       .value.move = MOVE_N };
		default:
			break;
		}
		game_ctx->player->keystate &= ~off_keys;
	}
	// for hold-down keys eg moving
	if (key_event->type == SDL_EVENT_KEY_DOWN) {
		enum frame_keys on_keys = FRAME_KEY_NONE;
		switch (key_event->scancode) {
		case SDL_SCANCODE_W:
			on_keys |= FRAME_KEY_W;
			break;
		case SDL_SCANCODE_A:
			on_keys |= FRAME_KEY_A;
			break;
		case SDL_SCANCODE_S:
			on_keys |= FRAME_KEY_S;
			break;
		case SDL_SCANCODE_D:
			on_keys |= FRAME_KEY_D;
			break;
		case SDL_SCANCODE_LSHIFT:
			on_keys |= FRAME_KEY_LSHIFT;
			break;
		default:
			break;
		}
		game_ctx->player->keystate |= on_keys;
	}
	return turn;
}

// update state for this frame based on keyboard, mouse input
void process_frame_input(struct game_context *game_ctx)
{
	// clear old velocity
	game_ctx->player->vel_x = 0;
	game_ctx->player->vel_y = 0;

	// apply new velocity
	float velocity = VELOCITY;
	if (game_ctx->player->keystate & FRAME_KEY_LSHIFT)
		velocity *= 2.0;
	if (game_ctx->player->keystate & FRAME_KEY_W)
		game_ctx->player->vel_y += velocity;
	if (game_ctx->player->keystate & FRAME_KEY_S)
		game_ctx->player->vel_y -= velocity;
	if (game_ctx->player->keystate & FRAME_KEY_A)
		game_ctx->player->vel_x -= velocity;
	if (game_ctx->player->keystate & FRAME_KEY_D)
		game_ctx->player->vel_x += velocity;

	// TODO how to incorporate with process_event tier? maybe add a second SDL_GetRelativeMouseState there
	// here, can only do mouse at frame level, skips clicks quicker than a frame
	float mouse_dx, mouse_dy;
	SDL_MouseButtonFlags mouse_state =
		SDL_GetRelativeMouseState(&mouse_dx, &mouse_dy);
	camera_update_from_mouse(mouse_dx, mouse_dy);
}

struct turn *process_event(SDL_Event *event, struct game_context *game_ctx)
{
	struct turn *turn = NULL;
	if (!event)
		return turn;
	switch (event->type) {
	case SDL_EVENT_QUIT:
		done = true;
		break;
	case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
		done = true;
		break;
	case SDL_EVENT_KEY_UP:
	case SDL_EVENT_KEY_DOWN:
		return process_key(&event->key, game_ctx);
	case SDL_EVENT_WINDOW_RESIZED:
		if (!SDL_GetWindowSize(rend_info.window, &rend_info.win_w,
				       &rend_info.win_h)) {
			log_err("SDL_GetWindowSize error: %s", SDL_GetError());
		}
		break;
	case SDL_EVENT_WINDOW_MOUSE_ENTER:
		if (!SDL_SetWindowRelativeMouseMode(rend_info.window, true)) {
			log_err("SDL_SetWindowRelativeMouseMode error :%s",
				SDL_GetError());
			return turn;
		}
	default:
		break;
	}
	return turn;
}

bool crossed_tile()
{
	return false;
}

const static struct map_pos_info dummy_visible_map[MAX_MAP_VISIBLE] = {
	{ { 1, 2 }, MTYPE_FLOOR },
	{ { 1, -2 }, MTYPE_FLOOR }
};

struct turn *update_world(struct game_context *game_ctx)
{
	struct turn *turn = NULL;

	process_frame_input(game_ctx);

	// update camera and move relative the pointed direction
	camera_update_pos(game_ctx->time.dt, game_ctx->player->vel_x,
			  game_ctx->player->vel_y);

	// check if tile bounds crossed, if so take turn
	if (crossed_tile()) {
		turn = malloc(sizeof(struct turn));
		*turn = (struct turn){ .type = TURN_MOVE,
				       .value.move = MOVE_N };
	}

	// update map
	// demo
	if (game_ctx->map_needs_change)
		memcpy(game_ctx->visible_map, dummy_visible_map,
		       MAX_MAP_VISIBLE * sizeof(struct map_pos_info));

	return turn;
}

int main(int argc, char *argv[])
{
	log_init();

	struct player player = { .vel_x = 0.,
				 .vel_y = 0.,
				 .keystate = FRAME_KEY_NONE };

	struct game_context game_ctx = {};
	game_ctx.player = &player;

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		log_err("SDL_Init failure: %s", SDL_GetError());
		return EXIT_FAILURE;
	}

	if (!render_init()) {
		log_err("render_init failure");
		return EXIT_FAILURE;
	}

	// todo allow running without network?
	if (!net_data_init()) {
		log_err("net_init failure");
		return EXIT_FAILURE;
	}

	// dummy once here
	memcpy(game_ctx.visible_map, dummy_visible_map,
	       MAX_MAP_VISIBLE * sizeof(struct map_pos_info));

	struct turn init_turn = { .type = TURN_MOVE,
			     .value.move = MOVE_N };
	do_turn(&init_turn, &game_ctx);

	while (!done) {
		// update time
		game_ctx.time.last_tick = game_ctx.time.cur_tick;
		game_ctx.time.cur_tick = SDL_GetTicks();
		game_ctx.time.dt =
			(game_ctx.time.cur_tick - game_ctx.time.last_tick) /
			1000.0;

		// process events
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			struct turn *turn = process_event(&event, &game_ctx);
			if (turn) {
				// send and receive game turn
				// process_event() may generate a turn
				// TODO: maybe consume the turn and free within function when processing in this function?
				do_turn(turn,
					&game_ctx); // do_consume_turn(turn);
				free_turn(turn);

				// need to re-render game world in case double triggered with movement below
				// do_turn will update world state too
				if (!render_draw(&game_ctx)) {
					log_err("render_draw failure");
				}
			}
		}

		// update world entities, potentially advancing game turn
		struct turn *turn = update_world(&game_ctx);
		if (turn) {
			do_turn(turn, &game_ctx);
			free_turn(turn);
		}

		// render
		if (!render_draw(&game_ctx)) {
			log_err("render_draw failure");
		}
	}

	render_quit();
	SDL_Quit();
	return EXIT_SUCCESS;
}
