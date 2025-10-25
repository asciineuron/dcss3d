#include "game.h"
#include "log.h"
#include "turn.h"

#include <SDL3/SDL.h>

// aspect ratio may warrant unequal x and y sensitivities
#define MOUSE_SENSITIVITY_X 0.005
#define MOUSE_SENSITIVITY_Y 0.005

void game_update_time(struct game_context *ctx)
{
	ctx->time.last_tick = ctx->time.cur_tick;
	ctx->time.cur_tick = SDL_GetTicks();
	ctx->time.dt = (ctx->time.cur_tick - ctx->time.last_tick) / 1000.0;
}

void print_map_pos_info(struct map_pos_info *visible_map, size_t map_size)
{
	if (!visible_map)
		return;
	log_trace("printing map pos:");
	for (int i = 0; i < map_size; ++i) {
		log_trace("i: %d, (x,y): (%f,%f), type: %d", i,
			  visible_map[i].coord.x, visible_map[i].coord.y,
			  visible_map[i].type);
	}
}

static float wrap(float x, float min, float max)
{
	if (min > max)
		return wrap(x, max, min);
	return (x >= 0 ? min : max) + fmod(x, max - min);
}

void update_player_view(struct player *player, float mouse_dx, float mouse_dy)
{
	struct camera *cam = &player->camera;
	cam->theta -= (mouse_dx * MOUSE_SENSITIVITY_X);
	cam->phi -= (mouse_dy * MOUSE_SENSITIVITY_Y);

	// wraparound into [0, 2pi] range:
	cam->theta = wrap(cam->theta, 0, 2 * M_PI);
	cam->phi = wrap(cam->phi, 0, 2 * M_PI);
}

/*
 * (x,y): index 0,1,2 TODO maybe invert x rows
 *   (-1,1)  (0,1)  (1,1)
 *   (-1,0)  (0,0)  (1,0)
 *   (-1,-1) (0,-1) (1,-1)
 */
static const enum move_direction shift_to_move_dir[3][3] = {
	{ MOVE_NW, MOVE_N, MOVE_NE },
	{ MOVE_W, MOVE_NONE, MOVE_E },
	{ MOVE_SW, MOVE_S, MOVE_SE }
};

struct turn *update_player_pos(struct player *player, double dt)
{
	struct turn *turn = NULL;

	struct camera *cam = &player->camera;
	float dx = player->vel_y * cos(cam->theta) +
		   player->vel_x * cos(M_PI_2 - cam->theta);
	float dy = -player->vel_y * cos(M_PI_2 - cam->theta) +
		   player->vel_x * cos(cam->theta);

	cam->pos[0] += dt * dx;
	cam->pos[2] += dt * dy;

	// add collision detection, and conversion to game loc + emit turn if needed
	int old_pos_x = player->pos_x;
	int old_pos_y = player->pos_y;
	// TODO flip these to match map convention, or just handle conversion here 
	// (ok to have differing render vs game conventions)
	// player->pos_x = (int)cam->pos[0];
	// player->pos_y = (int)cam->pos[2];
	// 0.5f shift aligns moves with tile *edges*, whereas renderer displaces relative *centers*
	player->pos_x = (int)(cam->pos[2] - 0.5f);
	player->pos_y = (int)(cam->pos[0] - 0.5f);

	int x_shift = player->pos_x - old_pos_x;
	int y_shift = player->pos_y - old_pos_y;
	assert(abs(x_shift) <= 1); // -1, 0, +1
	assert(abs(y_shift) <= 1);
	// translate shift into move
	log_trace("x_shift: %d, y_shift: %d", x_shift, y_shift);
	enum move_direction move = shift_to_move_dir[x_shift + 1][y_shift + 1];
	if (move != MOVE_NONE) {
		// set up move turn
		// TODO: how to handle diagonal movement, two tile crosses very rapidly could annoy player
		turn = malloc(sizeof(struct turn));
		*turn = (struct turn){ .type = TURN_MOVE, .value.move = move };
	}

	return turn;
}
