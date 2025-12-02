#ifndef USER_H
#define USER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <string.h>
#include "lwip/tcp.h"
#include "user_datatypes.h"
#include "pico/stdio.h"
#include "pico/multicore.h"
#include "pico/sync.h"

user_context_t * create_user_context(struct tcp_pcb *server_pcb, struct tcp_pcb *client_pcb,bool system_user);
bool delete_user_context(user_context_t *user);
void update_user_activity(user_context_t *user);
user_context_t * find_user_by_tcp_pcb(struct tcp_pcb *pcb);
user_context_t * find_user_by_username(const char *username);
bool add_user_to_list(user_context_t *new_user);
int count_active_users();
void user_print_info();
bool remove_user_from_list(user_context_t *user);
user_context_t *login_user(user_context_t *user, char *userinfo);
bool end_user_session(struct tcp_pcb *tpcb);
void userShell(user_context_t * user) ;
user_context_t *get_user_list();
user_context_t *get_user_current();
user_context_t *init_debug_session(struct tcp_pcb *server);
void debugger_message(char * msg);
int getUserChar(user_context_t *user);

#ifdef __cplusplus
}
#endif

#endif /* USER_H */