#include "turn.h"
#include "game.h"
#include "log.h"
#include "net_data.h"

#include <assert.h>
#include <stdio.h>

bool do_turn(const struct turn *turn, struct game_context *ctx)
{
	log_trace("doing turn");

	assert(turn);
	bool success = true;

	if (turn->type == TURN_ERR) {
		log_err("failed to process event");
		success = false;
	}

	const char *turn_message = turn_to_message(turn);
	if (!turn_message)
		success = false;

	printf("sending message: \n%s\n", turn_message);

	if (!send_turn_message(turn_message))
		success = false;

	const char *response = get_turn_response();
	if (!response)
		success = false;

	success = process_turn_response(response, ctx);

	if (success)
		++(ctx->time.game_turn);
	return success;
}

void free_turn(struct turn *turn)
{
	if (turn->type == TURN_TESTMALLOC) {
		// free(); // for heap turns, their proper destructor
		// more else if's for other specific turns...
	} else {
		return;
	}
}
