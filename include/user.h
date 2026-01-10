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
#include "pico/util/queue.h"
#include "user_datatypes.h"
#include "interpreter.h"
#include "debug_user.h"


#define QueueLength 32                      // length of each of the queues
#define Initial_Root_State  user_shell      // the intial state for the root is shell commands

user_context_t * create_user_context(struct tcp_pcb *server_pcb, struct tcp_pcb *client_pcb,bool system_user);
bool delete_user_context(user_context_t *user);
void update_user_activity(user_context_t *user);
user_context_t * find_user_by_tcp_pcb(struct tcp_pcb *pcb);
user_context_t * find_user_by_username(const char *username, char *password);

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
int echoUserChar(user_context_t *user, int c);
void user_flush(user_context_t *user);

char *user_set_file_path(user_context_t *user, char *filepath,int pathlen);

void user_clear_all_io(user_context_t *user);
void user_push_input_buffer(user_context_t *user, char *input);
bool user_add_char_to_input_buffer(user_context_t *user,int value);
void user_complete_read_from_input_buffer(user_context_t *user);
int user_get_line(user_context_t *user, char *cmdline_buffer,int cmd_length);
bool user_line_available(user_context_t * user);
bool user_char_available( user_context_t *user);
void user_display_directory(user_context_t *user,char *dirpath,char *parm1);
void user_type_file(user_context_t *user, char *filepath, bool UseFullPath);
void user_basic_load_file(user_context_t *user, char *filepath, bool runafterload);
void user_basic_save_file(user_context_t *user, char *filepath);
void user_basic_run_file(user_context_t *user, char *program_name);
void user_basic_list_file(user_context_t *user, int startline, int endline);
void user_basic_free_memory(user_context_t *user);
void user_quit(user_context_t *user);
void user_help_print(user_context_t *user);
void user_free_space(user_context_t *user);
int user_input_buffer_free_space(user_context_t *user);
bool user_remove_char_from_buffer(user_context_t *user);
void user_who(user_context_t *send_to);
bool user_logoff(user_context_t *user);
void clear_console_buffer(user_context_t *user);
bool exists_home(user_context_t *user, char *username, char *password, int dirpwlen);
bool user_create_home_directory(user_context_t *user);
 

#ifdef __cplusplus
}
#endif

#endif /* USER_H */