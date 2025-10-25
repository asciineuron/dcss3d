#ifndef TURN_H
#define TURN_H

struct game_context;

#include <stdbool.h>

enum turn_type { TURN_MOVE, TURN_TESTMALLOC, TURN_ERR };

enum move_direction {
	MOVE_NONE, // didn't clear a cell
	MOVE_N,
	MOVE_E,
	MOVE_S,
	MOVE_W,
	MOVE_NE,
	MOVE_SE,
	MOVE_SW,
	MOVE_NW,
	MOVE_WAIT, // '.' key, wait
	MOVE_COUNT
};

union turn_data {
	enum move_direction move;
};

struct turn {
	enum turn_type type;
	union turn_data value;
};

bool do_turn(const struct turn *turn, struct game_context *ctx);

void free_turn(struct turn *turn);

#endif
