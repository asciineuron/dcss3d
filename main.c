#include "log.h"
#include "render.h"
#include "turn.h"

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

// import glm later
#define FOV_RAD 0.785398
// TODO for mouse fix for x vs y?
#define FOV_DEG 45
#define ASPECT 1.777777

// aspect ratio may warrant unequal x and y sensitivities
#define MOUSE_SENSITIVITY_X 0.002
#define MOUSE_SENSITIVITY_Y 0.002
#define VELOCITY 0.8

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

static float wrap(float x, float min, float max)
{
	if (min > max)
		return wrap(x, max, min);
	return (x >= 0 ? min : max) + fmod(x, max - min);
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
	struct camera *cam = &(game_ctx->player->camera);
	cam->theta -= (mouse_dx * MOUSE_SENSITIVITY_X);
	cam->phi -= (mouse_dy * MOUSE_SENSITIVITY_Y);

	// wraparound into [0, 2pi] range:
	cam->theta = wrap(cam->theta, 0, 2 * M_PI);
	cam->phi = wrap(cam->phi, 0, 2 * M_PI);
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
	{ { 1, 1 }, MTYPE_FLOOR },
	{ { 1, 2 }, MTYPE_FLOOR }
};

struct turn *update_world(struct game_context *game_ctx)
{
	struct turn *turn = NULL;

	process_frame_input(game_ctx);

	// update camera and move relative the pointed direction
	struct camera *cam = &(game_ctx->player->camera);
	float vy = game_ctx->player->vel_y;
	float vx = game_ctx->player->vel_x;
	double dx = vy * cos(cam->theta) + vx * cos(M_PI_2 - cam->theta);
	double dy = -vy * cos(M_PI_2 - cam->theta) + vx * cos(cam->theta);
	cam->pos[0] += game_ctx->time.dt * dx;
	cam->pos[1] += game_ctx->time.dt * dy;

	// check if tile bounds crossed, if so take turn
	if (crossed_tile()) {
		turn = malloc(sizeof(struct turn));
		*turn = (struct turn){ .type = TURN_MOVE,
				       .value.move = MOVE_N };
	}

	// update map
	// demo
	memcpy(game_ctx->visible_map, dummy_visible_map,
	       MAX_MAP_VISIBLE * sizeof(struct map_pos_info));

	return turn;
}

float cam_y()
{
	return FOV_DEG;
}

int main(int argc, char *argv[])
{
	log_init();

	struct player player = { .camera = {
				      .pos = GLM_VEC3_ZERO_INIT,
				      .fov = FOV_RAD,
				      .aspect_ratio = ASPECT,
				      .theta = 0,
				      .phi = 0,
			      },
			      .vel_x = 0.,
			      .vel_y = 0.,
			      .keystate = {} };

	struct game_context game_ctx = { {}, &player, { 0, 0, 0 } };

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		log_err("SDL_Init failure: %s", SDL_GetError());
		return EXIT_FAILURE;
	}

	if (!render_init()) {
		log_err("render_init failure");
		return EXIT_FAILURE;
	}

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
