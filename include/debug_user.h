/*                   GNU AFFERO GENERAL PUBLIC LICENSE
                       Version 3, 19 November 2007

 Copyright (C) 2007 Free Software Foundation, Inc. <https://fsf.org/>
 Everyone is permitted to copy and distribute verbatim copies
 of this license document, but changing it is not allowed. */

#ifndef TINYBASIC_DEBUG_USER_H
#define TINYBASIC_DEBUG_USER_H
#include "user.h"

user_context_t *init_debug_session(struct tcp_pcb *server);
void debugger_message(user_context_t *user,char * msg);

#endif