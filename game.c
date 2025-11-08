#include "game.h"
#include "log.h"
#include "render.h"
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

void print_map_pos_info(const struct map_pos_info *visible_map,
			const size_t map_size)
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

void update_player_view(struct player *player, const float mouse_dx,
			const float mouse_dy)
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

// allow W/E and N/S together, but not W+E or N+S at same time, TODO how to enforce?
enum collide_dir {
	COLLIDE_NONE = 0,
	COLLIDE_W = 1 << 0,
	COLLIDE_E = 1 << 1,
	COLLIDE_N = 1 << 2,
	COLLIDE_S = 1 << 3,
};

/*
 * 1 = W
 * 2 = E
 * 4 = N
 * 8 = S
 *
 * 3 = W+E
 * 12 = N+S 
 * 5 = W+N
 * 10 = E+S
 * 15 = all
 */

static enum collide_dir
check_displace_collide_map(const float pos_x, const float pos_y,
			   const struct map_pos_info *visible_map,
			   const size_t visible_map_size)
{
	// IMPORTANT NOTE: we need to flip the collision indices since we are moving towards that direction
	// e.g. moving east, we first hit the west face i.e. ll_x of the cube,
	// so this should trigger COLLIDE_E i.e. we can't move further east
	// also need to ensure that we are in the same plane as the cube, i.e. if almost touching X side, must be within the y bounds or else it doesn't matter
	// e.g.: does not collide even though touching X+Y sides
	/*   @
	 *    XX
	 *    XX
	 */

	log_trace("pos coords %f, %f", pos_x, pos_y);

	enum collide_dir collision = COLLIDE_NONE;
	for (int i = 0; i < visible_map_size; i++) {
		// TODO: expand, several types may be intraversable besides walls e.g. vines or deep water
		// how to handle door? prevent automatic opening, need button instead?
		if (visible_map[i].type != MTYPE_WALL)
			continue;

		log_trace("map coords for i: %d, x,y %f,%f", i,
			  visible_map[i].coord.x, visible_map[i].coord.y);

		vec3 render_coords;
		map_tile_to_render_coords(visible_map[i].coord, render_coords);
		log_trace("render coords for i: %d, x,y %f,%f", i,
			  render_coords[0], render_coords[1]);

		vec4 bbox;
		render_coords_llx_lly_urx_ury(render_coords, bbox);
		log_trace("bbox for i: %d, %f,%f,%f,%f", i, bbox[0], bbox[1],
			  bbox[2], bbox[3]);

		// enforce only W or E for single tile
		float dist_from_center = fabsf(render_coords[1] - pos_y);
		if (fabsf(bbox[0] - pos_x) < PLAYER_BBOX_WIDTH &&
		    dist_from_center < 0.5f) {
			collision |= COLLIDE_E;
		} else if (fabsf(bbox[2] - pos_x) < PLAYER_BBOX_WIDTH &&
			   dist_from_center < 0.5f) {
			collision |= COLLIDE_W;
		}
		// enforce only N or S
		dist_from_center = fabsf(render_coords[0] - pos_x);
		if (fabsf(bbox[3] - pos_y) < PLAYER_BBOX_WIDTH &&
		    dist_from_center < 0.5f) {
			collision |= COLLIDE_S;
		} else if (fabsf(bbox[1] - pos_y) < PLAYER_BBOX_WIDTH &&
			   dist_from_center < 0.5f) {
			collision |= COLLIDE_N;
		}
	}
	return collision;
}

struct turn *update_player_pos(struct player *player, const double dt,
			       const struct map_pos_info *visible_map,
			       const size_t visible_map_size)
{
	struct turn *turn = NULL;

	enum collide_dir check_collide = check_displace_collide_map(
		player->camera.pos[0], player->camera.pos[2], visible_map,
		visible_map_size);

	struct camera *cam = &player->camera;
	float dx = player->vel_y * cos(cam->theta) +
		   player->vel_x * cos(M_PI_2 - cam->theta);
	float dy = -player->vel_y * cos(M_PI_2 - cam->theta) +
		   player->vel_x * cos(cam->theta);

	// implement bbox detection before allowing the camera to move:
	// can also use (or do separate entity vs wall pass thru) to trigger combat during movement (or just disallow move-to-attack might be best for 3d)
	// render-space units:
	float x_disp = dt * dx;
	float y_disp = dt * dy;

	// prevent displacement if colliding in direction of movement
	log_trace("x_disp, y_disp: %f, %f, collide: %d", x_disp, y_disp,
		  check_collide);

	// sign flip here since -y map coords is into the screen
	if (y_disp > 0.0f) {
		if (!(check_collide & COLLIDE_S))
			cam->pos[2] += y_disp;
	} else if (y_disp < 0.0f) {
		if (!(check_collide & COLLIDE_N))
			cam->pos[2] += y_disp;
	}
	if (x_disp < 0.0f) {
		if (!(check_collide & COLLIDE_W))
			cam->pos[0] += x_disp;
	} else if (x_disp > 0.0f) {
		if (!(check_collide & COLLIDE_E))
			cam->pos[0] += x_disp;
	}

	// convert to game-units position
	// 0.5f shift aligns moves with tile *edges*, whereas renderer displaces relative *centers*
	int old_pos_x = player->pos_x;
	int old_pos_y = player->pos_y;
	player->pos_x = (int)(cam->pos[2] - 0.5f);
	player->pos_y = (int)(cam->pos[0] - 0.5f);

	int x_shift = player->pos_x - old_pos_x;
	int y_shift = player->pos_y - old_pos_y;
	assert(abs(x_shift) <= 1); // -1, 0, +1
	assert(abs(y_shift) <= 1);
	log_trace("x_shift: %d, y_shift: %d", x_shift, y_shift);

	// translate shift into move, may emit turn if necessary
	enum move_direction move = shift_to_move_dir[x_shift + 1][y_shift + 1];
	if (move != MOVE_NONE) {
		// set up move turn
		// TODO: how to handle diagonal movement, two tile crosses very rapidly could annoy player
		turn = malloc(sizeof(struct turn));
		*turn = (struct turn){ .type = TURN_MOVE, .value.move = move };
	}

	return turn;
}
