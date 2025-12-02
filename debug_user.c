#include "user.h"

user_context_t *debug_session;

user_context_t *init_debug_session(struct tcp_pcb *server) {
    debug_session = create_user_context(server,NULL,false);
    strcpy(debug_session->username,"debug");
    strcpy(debug_session->password,"iamroot");
    debug_session->level = user_new_connect;
    return debug_session;
}

void debugger_message(char * msg) {

}
