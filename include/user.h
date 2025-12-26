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
#include "interpreter.h"
#include "debug_user.h"

user_context_t * create_user_context(struct tcp_pcb *server_pcb, struct tcp_pcb *client_pcb,bool system_user);
bool delete_user_context(user_context_t *user);
void update_user_activity(user_context_t *user);
user_context_t * find_user_by_tcp_pcb(struct tcp_pcb *pcb);
user_context_t * find_user_by_username(const char *username);

bool add_to_list(user_context_t *new_user, user_context_t **list, semaphore_t *sema, char *errmsg);
bool add_user_to_list(user_context_t *new_user);
bool add_user_to_waiting(user_context_t *new_user);

bool remove_from_list(user_context_t *new_user, user_context_t **list, semaphore_t *sema);
bool remove_user_from_waiting(user_context_t *user);
bool remove_user_from_list(user_context_t *user);
user_context_t *get_next_waiting();


int count_active_users();

user_context_t *login_user(user_context_t *user);
bool end_user_session(struct tcp_pcb *tpcb);
void userShell(user_context_t * user) ;
user_context_t *get_user_list();
user_context_t *get_user_current();
int getUserChar(user_context_t *user);
bool user_write(user_context_t *user, const char *buffer);
int putUserChar(user_context_t *user, int c);
char *user_set_file_path(user_context_t *user, char *filepath,int pathlen);
void user_clear_all_io(user_context_t *user);
void user_push_input_buffer(user_context_t *user, char *input);
bool user_add_char_to_input_buffer(user_context_t *user,int value);
void user_complete_read_from_input_buffer(user_context_t *user);
int user_get_line(user_context_t *user, char *cmdline_buffer,int cmd_length);
bool user_line_available(user_context_t * user);
bool user_char_available( user_context_t *user);
void user_display_library(user_context_t *user);
void user_who(user_context_t *send_to);
 

#ifdef __cplusplus
}
#endif

#endif /* USER_H */