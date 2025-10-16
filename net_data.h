#ifndef NET_DATA_H
#define NET_DATA_H

#include "turn.h"

extern char *cur_msg;
extern int msg_idx;

bool net_data_init(void);
bool net_data_exit(void);

const char *turn_to_message(const struct turn *turn);

// call this in void do_turn(turn); which sends message, parses response, and updates game state accordingly
// (separate from a dcss turn since a move may be less than 1 turn of game time)
bool send_turn_message(const char *message);

// call once per call of send_turn_message
const char *get_turn_response(void);

#endif
