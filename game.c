#include "game.h"

#include <SDL3/SDL.h>

void game_update_time(struct game_context *ctx)
{
	ctx->time.last_tick = ctx->time.cur_tick;
	ctx->time.cur_tick = SDL_GetTicks();
	ctx->time.dt = (ctx->time.cur_tick - ctx->time.last_tick) / 1000.0;
}
