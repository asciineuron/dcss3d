#include "game.h"
#include "log.h"

#include <SDL3/SDL.h>

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
