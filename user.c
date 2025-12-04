// user fucntions for tinybasic multi-user time share system
#include <stdio.h>
#include <malloc.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/timer.h"
#include "hardware/clocks.h"
#include "pico/cyw43_arch.h"

#include "tcp_interface.h"
#include "user.h"

extern char __StackLimit, __bss_end__;
extern void UserInitTinyBasic(user_context_t *user, char * ILtext);

const char * status_text[] = {"New Connect", "Waiting Activation","Shell","Tiny Basic","Help","Ending"};

const char * userprompt = " > ";

user_context_t *ActiveUsers = 0;      // Head of linked list of active users
user_context_t *CurrentUser = 0;      // Pointer to current user for processing
user_context_t *NewUsers = 0;         // new users to be added to user list
user_context_t *RootUser = 0;         // Pointer to the root user
user_context_t *DebugUser = 0;        // Pointer to the debug user

semaphore_t user_list_sema;           // protect the user list against bad things
semaphore_t new_user_list_sema;       // protect the user list against bad things

bool SwitchUser = false;              // return to switch to the next user

// get the user list
user_context_t *get_user_list() {
    sem_acquire_blocking(&user_list_sema);
    user_context_t *result = ActiveUsers;
    sem_release(&user_list_sema);
    return result;
}

user_context_t *get_user_current() {
    return CurrentUser;
}

uint32_t user_free_mem() {
    struct mallinfo m = mallinfo();
    uint32_t total_heap_size = &__StackLimit  - &__bss_end__; // adjust if necessary
    uint32_t free_sram = total_heap_size - m.uordblks;
    return free_sram;
}

// Function to create a new user context
user_context_t * create_user_context(struct tcp_pcb *server_pcb, struct tcp_pcb *client_pcb,bool system_user) {

    if(user_free_mem() <= sizeof(user_context_t)+USER_MEMORY_SIZE) {
        printf("User Create context, not enough memory to create context\n Have : %d, Needs %d\n",user_free_mem(),sizeof(user_context_t));
        return NULL;
    }

    user_context_t *user = (user_context_t *)calloc(1,sizeof(user_context_t));
    if (!user) {
        return NULL;
    }
        user->MemorySize = USER_MEMORY_SIZE;
        user->i_Core = (aByte *)calloc(1,user->MemorySize);
        if (!user->i_Core) {
            free(user);
            return NULL;
        }
        user->i_Broken = false;                       // set to true to stop execution or listing
        user->i_inFile = NULL;                        // from option '-i' or user menu/button
        user->i_oFile = NULL; 
        user->i_Debugging = 0;
        user->i_LogHere = 0;                          // current index in DebugLog
        user->i_Watcher = 0;                          // memory watchpoint
   
    user->logged_in = false;
    user->SystemUser = false;
    user->last_active_time = to_ms_since_boot(get_absolute_time());
    user->lineIndex = 255;                                // console input ring buffer pointer

    if(system_user)  {
         user->level = user_basic;                        // let system user immediatly be basic level
         user->logged_in = true;
         CurrentUser = user;                              // when a system user is created then make it the current user
    }

    memset(&user->state, 0, sizeof(TCP_SERVER_T));
    user->state.server_pcb = server_pcb;
    user->state.client_pcb = client_pcb;
    user->SystemUser = system_user;

    return user;
}

// Function to delete a user context and free associated memory
bool delete_user_context(user_context_t *user) {
    if (!user) {
        return false;
    }
    if (user->i_Core) {
        free(user->i_Core);
    }
    free(user);
    return true;
}

// Function to update the last active time for a user
void update_user_activity(user_context_t *user) {
    if (user) {
        user->last_active_time = to_ms_since_boot(get_absolute_time());
    }
}

bool is_user_timed_out(user_context_t *user) {
    if (!user) {
        return true;
    }
    uint64_t current_time = to_ms_since_boot(get_absolute_time());
    return (current_time - user->last_active_time) > USER_TIMEOUT_MS;            // lets fix this to make it simple
}

user_context_t * find_user_by_username(const char *username) {
    sem_acquire_blocking(&user_list_sema);
    user_context_t *user = ActiveUsers;
    while (user) {
        if (strcmp(user->username, username) == 0) {
            sem_release(&user_list_sema);
            return user;
        }
        user = user->next;
    }
    sem_release(&user_list_sema);
    return NULL;
}

user_context_t * find_user_by_tcp_pcb(struct tcp_pcb *pcb) {
    sem_acquire_blocking(&user_list_sema);
    user_context_t *user = ActiveUsers;
    while (user) {
        if (user->state.client_pcb == pcb) {
            sem_release(&user_list_sema);
            return user;
        }
        user = user->next;
    }
    sem_release(&user_list_sema);
    return NULL;
}



bool add_to_list(user_context_t *new_user, user_context_t **list, semaphore_t *sema,char * errmsg)  {  
    if(!sem_try_acquire(sema)) {
        printf("%s, Unable to aquire lock!\n");
        return false;
    }

    user_context_t **head = list;

    if (!head || !new_user) {
        sem_release(sema);
        return false;
    }

    new_user->next = *head;

    if (*head) {
        (*head)->prev = new_user;
    }
    *head = new_user;
    new_user->prev = NULL;

    sem_release(sema);
    return true;
}

bool add_user_to_waiting(user_context_t *new_user){
    return add_to_list(new_user,&NewUsers,&new_user_list_sema,"Waiting Queue");
}

bool add_user_to_list(user_context_t *new_user) {
    return add_to_list(new_user,&ActiveUsers,&user_list_sema,"User Queue");
}

int count_active_users() {
    sem_acquire_blocking(&user_list_sema);
    int count = 0;
    user_context_t *user = ActiveUsers;
    while (user) {
        count++;
        user = user->next;
    }
    sem_release(&user_list_sema);
    return count;
}


void user_print_status(user_context_t *user){
    printf("Username: %-20s, Logged In: %-3s, System User: %-3s, Status: %-12s, ID %8X", user->username[0] == 0 ? "No User" : user->username,
                user->logged_in ? "Yes" : "No ", user->SystemUser ? "Yes" : "No ", status_text[user->level], 
                user->SystemUser ? user->state.server_pcb : user->state.client_pcb);
    printf(" Last Active Time: %llu ms\n", user->last_active_time  );
}

void user_print_info() {
    user_context_t *user = get_user_list();
    printf("Active Users: Count %d\n", count_active_users());
    while (user) {
        user_print_status(user);
        user = user->next;
    }
}   

bool remove_user_from_list(user_context_t *user) {
    return remove_from_list(user,&ActiveUsers,&user_list_sema);
}

bool remove_user_from_waiting(user_context_t *user) {
    return remove_from_list(user,&NewUsers,&new_user_list_sema);
}

bool remove_from_list(user_context_t *user, user_context_t **list, semaphore_t *sema) {
    sem_acquire_blocking(sema);
    user_context_t *users = *list;

    if (!users || !user) {
        sem_release(sema);
        return false;
    }

    // Check if the user to be removed is the head of the list
    if(user == users) {
        *list = user->next;
        if (*list) {
            (*list)->prev = NULL;
        }
        sem_release(sema);
        return true;
    }

    // Iterate through the list to find the user
    while (users) {
        if (users == user) {
            if (users->prev) {
                users->prev->next = users->next;
            }
            if (users->next) {
                users->next->prev = users->prev;
            }
            sem_release(sema);
            return true;
        }
        users = users->next;
    }

    sem_release(sema);
    return false;
}


user_context_t *login_user(user_context_t *user, char *userinfo) {
    if (!user) {
        return false;
    }
    //printf("Login attempt with info: '%s'\n", userinfo);

    char logon_type = ':';
    char username[32];
    char password[32];

    // Parse the userinfo string
    if (sscanf(userinfo, "%31[^:?]%c%31s", username,&logon_type,password) == 3) {
    } else {
   //     printf("Login failed due to bad format: '%s'\n", userinfo);
        return false;  // Bad format
    }   
    printf("Parsed login info - Username: '%s', Logon Type: '%c', Password: '%s'\n", username, logon_type, password);

    if (logon_type == '?') {  // process a new user request
        user_context_t *existing_user = find_user_by_username(username);
        if (existing_user) {
            if(strncmp(existing_user->password, password, sizeof(existing_user->password)) == 0) {
                tcp_server_send_message(user,"Someone is aleady logged in with that user username and password\n\r");
                return false; // if password matches someone is already logged in with that username
            }
        }

        strncpy(user->username, username, sizeof(user->username) - 1);
        user->username[sizeof(user->username) - 1] = '\0';
        strncpy(user->password, password, sizeof(user->password) - 1);
        user->password[sizeof(user->password) - 1] = '\0';
        user->logged_in = true;
        user->SystemUser = false;
        update_user_activity(user);
        return user;

    } else {
        if (logon_type == ':') {
            // System user login
            user_context_t *existing_user = find_user_by_username(username);
            if (existing_user) {
                if (strncmp(existing_user->password, password, sizeof(existing_user->password)) != 0){
                 tcp_server_send_message(user,"That user is logged in but passwords do not match\n\r");
                 return (user_context_t *)false; // Invalid password and user combination
                }
            }
            // for now if passwords  match just create a new session for that user, later we can improve this
            if(!existing_user->logged_in) { // if user is not logged in then use that context
                memcpy(&existing_user->state,&user->state,sizeof(TCP_SERVER_T));
                existing_user->logged_in = true;
                tcp_arg(existing_user->state.client_pcb, existing_user);
                user->level = user_removed;
                user = existing_user;
            } else {                       // if user is logged in then create a new context for this connection
                user->SystemUser = false;
                user->logged_in = true;
                strncpy(user->username, username, sizeof(user->username) - 1);
                user->username[sizeof(user->username) - 1] = '\0';
                strncpy(user->password, password, sizeof(user->password) - 1);
                user->password[sizeof(user->password) - 1] = '\0';
            }

            if(!user->BasicInitComplete) {
                 printf("Start Basic Initilization\n");
                 UserInitTinyBasic(user,(char *)0);
                 printf("Complete Basic Initilization\n");
            }

            tcp_server_send_message(user,"Welcome to Tiny Basic Time share system\n");
            update_user_activity(user);
            return user;
        }
    }

    printf("Login failed due to bad format: '%s'\n", userinfo);
    tcp_server_send_message(user,"Invalid login request format!\n");
    return (user_context_t *)false;  // probably a bad logon format
}

bool end_user_session(struct tcp_pcb *tpcb){
    user_context_t *user = find_user_by_tcp_pcb(tpcb);
    if (user) {
        user->level = user_removed;
        return true;
    }
    return false;
}

bool user_logoff(user_context_t *user) {
    printf("user_logoff called :%s",user->username);
    user->level = user_removed;
    return true;
}

int getUserChar(user_context_t *user) {
    int value;
    if(user->lineLength == 0) { 
        user->WaitingRead = io_none;
        return '\r';
    }

    value = user->linebuffer[user->lineReadPos];
    if(++user->lineReadPos >= 256) user->lineReadPos = 0;
    user->lineLength--;
    if(user->lineLength == 0 ) user->WaitingRead = io_none;
    return value;
}

int putUserChar(user_context_t *user, int c) {
     TCP_SERVER_T *state = &user->state;
    if(user->SystemUser) {
        putchar(c);
    } else {
        char buff[2];
        buff[0] = (char)c;
        buff[1] = '\0';
        tcp_server_send_message(user,buff);
        if(c=='\r') {
            tcp_output(state->client_pcb);
        }
    }
}

void user_who(user_context_t *send_to) {
    user_context_t *user = get_user_list();
    struct tcp_pcb *tpcb = send_to->state.client_pcb;
    char buffer[256];

    sprintf(buffer,"User Count : %d\n",count_active_users());
    tcp_server_send_message(send_to,buffer);

    while (user) {
        sprintf(buffer,"%-20s, Status : %-12s, Logged In : %3s, Last Activity : %llu\n\r",
                       (user->username[0] == '\0' ?  "Waiting Loggon" : user->username ),
                        status_text[user->level], 
                        user->logged_in ? "Yes" : "No",
                        user->last_active_time );
        tcp_server_send_message(send_to,buffer);
        user = user->next;
    }
    return;
}
const char user_help_text[] = {"Commands:(case is ignored)\n\rWho - see who is logged on\n\rFree - See amount of available memory\n\rQuit - Terminate session\n\rBasic - Start Basic\r\n"};

void user_help_print(user_context_t *send_to){
    tcp_server_send_message(send_to,(char *)user_help_text);
}

void user_free_space(user_context_t *send_to) {
    char buffer[256];
    struct mallinfo m = mallinfo();
    uint32_t total_heap_size = &__StackLimit  - &__bss_end__; // adjust if necessary
    uint32_t free_sram = total_heap_size - m.uordblks;
    sprintf(buffer,"Base Mem 512K, System:%u, User:%u, used %u, free %u\n", 512*1024 - total_heap_size, total_heap_size, m.uordblks, free_sram);
    tcp_server_send_message(send_to,buffer);
}

void user_quit(user_context_t *user) {
    user_logoff(user);
}

void userShell(user_context_t * user) {
    // later this will be a basic program
    strupr(user->state.buffer_recv);
    DEBUG_printf("User Shell Request from %s id %8X %-7s : %s ",user->username,
                 user->SystemUser ? user->state.server_pcb:user->state.client_pcb,
                 user->SystemUser ? "System": "User",user->state.buffer_recv);

    if(user->state.recv_len >= 3 && strncmp(user->state.buffer_recv,"WHO",3)==0){
        user_who(user);
    } else if(user->state.recv_len >= 3 && strncmp(user->state.buffer_recv,"FREE",4)==0){
        user_free_space(user);
    } else if(user->state.recv_len >= 3 && strncmp(user->state.buffer_recv,"HELP",4)==0){
        user_help_print(user);
    } else if(user->state.recv_len >= 3 && strncmp(user->state.buffer_recv,"QUIT",3)==0){
        user_quit(user);
    } else if(user->state.recv_len >= 3 && strncmp(user->state.buffer_recv,"BASIC",3)==0){
        user->level = user_basic;
        return;
    } else {
        tcp_server_send_message(user,"Unknown Command\n\r");
    }



}
