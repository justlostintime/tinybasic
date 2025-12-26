#ifndef TINYBASIC_DEBUG_USER_H
#define TINYBASIC_DEBUG_USER_H
#include "user.h"

user_context_t *init_debug_session(struct tcp_pcb *server);
void debugger_message(user_context_t *user,char * msg);

#endif