/*                   GNU AFFERO GENERAL PUBLIC LICENSE
                       Version 3, 19 November 2007
*/

// user fucntions for tinybasic multi-user time share system
#include <stdio.h>
#include <malloc.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/timer.h"
#include "hardware/clocks.h"
#include "pico/cyw43_arch.h"

#include "tcp_interface.h"
#include "FileSystem.h"
#include "telnetserver.h"
#include "user_datatypes.h"
#include "user.h"
#include "terminal.h"
#include "interpreter.h"

extern char __StackLimit, __bss_end__;
extern uint64_t timerIdle;
extern user_context_t *core1_user;
extern user_context_t *core0_user;
extern uint64_t Process_time_core0;            // the total process time used by core 0 ms
extern uint64_t Process_time_core1;            // the total process time used by core 1 ms   
extern char *tinybasicIL;                      // pointer to the tinybasic IL code

const char * status_text[] = {"Wait Con", "Activating","Shell","Tiny Basic","Help","Logoff"};

const char * userprompt = " > ";

user_context_t *ActiveUsers = 0;      // Head of linked list of active users
user_context_t *NewUsers = 0;         // new users to be added to user list
user_context_t *RootUser = 0;         // Pointer to the root user
user_context_t *DebugUser = 0;        // Pointer to the debug user


queue_t shell_queue;                  // core 0 waiting to run shell commands         
queue_t basic_queue;                  // core 1 running basic programs

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
        user->MemorySize = USER_MEMORY_SIZE;          // size of the BASIC program memory space
        // moved core alloc to basic intialization function
        user->i_Broken = false;                       // set to true to stop execution or listing
        user->i_inFile = NULL;                        // from option '-i' or user menu/button
        user->i_oFile = NULL; 
        user->i_Debugging = 0;
        user->i_LogHere = 0;                          // current index in DebugLog
        user->i_Watcher = 0;                          // memory watchpoint
   
    user->logged_in = false;
    user->SystemUser = false;
    user->last_active_time = to_ms_since_boot(get_absolute_time());
    user->lineIndex = 0;                                    // console input buffer pointer
    user->lineReadPos = sizeof(user->linebuffer);
    user->ExitWhenDone = false;                             // don't exit yet     

    if(system_user)  {
         user->level = user_shell;                          // let system user immediatly be shell level
         user->logged_in = true;
         RootUser = user;                                   // when a system user is created then make it the current user
         RootUser->persist = true;
    }

    memset(&user->state, 0, sizeof(TCP_SERVER_T));
    user->state.server_pcb = server_pcb;
    user->state.client_pcb = client_pcb;
    user->SystemUser = system_user;
    user->echo = true;                                      // make sure the system is echoing characters from files

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

user_context_t * find_user_by_username(const char *username, char * password) {
    sem_acquire_blocking(&user_list_sema);
    user_context_t *user = ActiveUsers;
    while (user) {
        if (strcmp(user->username, username) == 0) {
            if(password != NULL) {
                if(strcmp(user->password,password) == 0) {
                    sem_release(&user_list_sema);
                    return user;
                } else {
                    sem_release(&user_list_sema);
                    return NULL;
                }
            }
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

user_context_t *remove_next_user(user_context_t **list, semaphore_t *sema, char *errmsg) {
    if(!sem_try_acquire(sema)) {
        printf("%s, Unable to aquire lock! for  %s\n\r",errmsg);
        return NULL;
    }
    user_context_t *user = *list;

    if( user != NULL) {
        *list = user->next;
        user->next = NULL;
    }

    sem_release(sema);
    return user;
}

bool add_user_to_waiting(user_context_t *new_user){
    return add_to_list(new_user,&NewUsers,&new_user_list_sema,"Waiting Queue of new users");
}

user_context_t *get_next_waiting() {
    return remove_next_user(&NewUsers,&new_user_list_sema,"Waiting Queue of new users");
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

const char charmap[]={"zaqwsxcderfvbgtyhnmjuiklop0987654321ABCDKQAZWSXEDCRFVTGBYHNUJMIKOPK1209QAZwsxdercftybvjygonhdytopmwqudrnopqstruiymv"};
void hash_it(char *buffer, int len) {
    for(int i = 0;i < len; i++){
        buffer[i] = charmap[((int)buffer[i]+i)%(sizeof(charmap)-1)];
    }
}

user_context_t *login_user(user_context_t *user) {
    char buffer[128];
    char userinfo[128];
    bool HomeExists = false;

    if (!user) {
        return NULL;
    }

    char logon_type = ':';
    char logon_delim = ':';
    char username[32];
    char password[32];

    // get the command line
    user_get_line(user,userinfo,sizeof(userinfo));

    // Parse the userinfo string
    if (sscanf(userinfo, "%31[^: ]%c%c%31s", username,&logon_type,&logon_delim,password) == 4) {
    } else {
        return NULL;  // Bad format
    }

    hash_it(password,strlen(password));

    //printf("Parsed login info - Username: '%s', Logon Type: '%c', Password: '%s'\n", username, logon_type, password);

    if (logon_type == ':' && logon_delim == ':') {
        // System user login
        user_context_t *existing_user = find_user_by_username(username,NULL);
        if(existing_user == NULL) {
            char dir_password[128];
            if(exists_home(user,username,dir_password,sizeof(dir_password))) {
                //printf("checking if user exists %s-%s\n",username,password);
                if(strcmp(password,dir_password) != 0) {
                    snprintf(buffer,sizeof(buffer),"Ex - password is incorrect\n\r",username);
                    user_write(user,buffer);
                    user->failed_logins++;
                    return (user_context_t *)NULL; // Invalid password and user combination    
                }
            } else {
                HomeExists = true;
            }
        }

        if (existing_user) {
            if (strncmp(existing_user->password, password, sizeof(existing_user->password)) != 0){
                snprintf(buffer,sizeof(buffer),"Li - passwords do not match\n\r");
                user_write(user,buffer);
                user->failed_logins++;
                return (user_context_t *)NULL; // Invalid password and user combination
            }
            // for now if passwords  match just create a new session for that user, later we can improve this
            if(!existing_user->logged_in) { // if user is not logged in then use that context
                memcpy(&existing_user->state,&user->state,sizeof(TCP_SERVER_T));
                existing_user->logged_in = true;
                tcp_arg(existing_user->state.client_pcb, existing_user);
                user->level = user_removed;
                user->state.client_pcb = NULL;
                user = existing_user;
                if(!user->SystemUser) {
                    // create home directory for user
                    if(!user_create_home_directory(user)) {
                        snprintf(buffer,sizeof(buffer),"Failed to create home directory for user %s\n\r",user->username);
                        user_write(user,buffer);
                        return NULL;
                    }
                }
                update_user_activity(user);
                snprintf(buffer,sizeof(buffer),"Welcome back  %s enjoy your stay\n\r", username);
                user_write(user,buffer);
                return user;
            } else {        // if user is logged in then create a new context for this connection
                user->SystemUser = false;
                user->logged_in = true;
                strncpy(user->username, username, sizeof(user->username) - 1);
                user->username[sizeof(user->username) - 1] = '\0';
                strncpy(user->password, password, sizeof(user->password) - 1);
                user->password[sizeof(user->password) - 1] = '\0';
                if(!user->SystemUser) {
                    // create home directory for user
                    if(!user_create_home_directory(user)) {
                        snprintf(buffer,sizeof(buffer),"Failed to create home directory for user %s\n\r",user->username);
                        user_write(user,buffer);
                        user->level = user_removed;
                        return NULL;
                    }
                }  
                snprintf(buffer,sizeof(buffer),"Welcome with multi logins  %s enjoy your stay!\n\r", username);
                user_write(user,buffer);
                update_user_activity(user);
                return user;
            } 
        } else {        // must be a new user !               
            user->SystemUser = false;
            user->logged_in = true;
            strncpy(user->username, username, sizeof(user->username) - 1);
            user->username[sizeof(user->username) - 1] = '\0';
            strncpy(user->password, password, sizeof(user->password) - 1);
            user->password[sizeof(user->password) - 1] = '\0';
            if(!user->SystemUser && !HomeExists) {
                // create home directory for user
                if(!user_create_home_directory(user)) {
                    snprintf(buffer,sizeof(buffer),"Failed to create home directory for user %s\n\r",user->username);
                    user_write(user,buffer);
                    user->level = user_removed;
                    return NULL;
                }
            }  
            snprintf(buffer,sizeof(buffer),HomeExists ? "Welcome  %s enjoy your stay!\n\r":"Welcome back %s We saved your files for you!\n\r", username);
            user_write(user,buffer);
            update_user_activity(user);
            return user;
        }
    }

    snprintf(buffer,sizeof(buffer),"Login failed: '%s'\n\r", userinfo);
    user_write(user,buffer);
    user->failed_logins++;
    return (user_context_t *)NULL;  // probably a bad logon format
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
    char buffer[128];
    snprintf(buffer,sizeof(buffer),"User %s logging off\n\r",user->username);
    user_write(user,buffer);
    if (user->state.client_pcb != NULL) 
        tcp_output(user->state.client_pcb);  // flush the buffer
    user->level = user_removed;
    return true;
}
//______________________________________________________________________________
// Manage user io
//________________________________________________________________________________
// Process the special commands being passed

void process_escape_commands(user_context_t *user,int16_t value) {

    if(user->escape_index > sizeof(user->escape_buffer)-1) {
        printf("Escape too long sequence recieved : ");
        user->escape_mode = false;
        printf("%s\r\n",user->escape_buffer);
        return;
    }

    user->escape_buffer[user->escape_index++] = value;
    user->escape_buffer[user->escape_index] = '\0';

    switch(value) {
        case 'A':               //  up arrow
        case 'B':               //  down arrow
        case 'C':               //  right arrow
        case 'D':               //  left arrow
            break;

        case 'F':               //  end key
            user->i_Broken = true;
            user->escape_mode = false;
            break;
             
        case '~':               //  most other keys
                user->escape_mode = false;
                //printf("Escape Seq : %s\r\n",user->escape_buffer);
                break;

        case 'H':               //  home key is a shortcut to display who is online
                user->display_who = true;
                user->escape_mode = false;
                break;

        case 'R':               //  returned screen size [rows;col]

                // Parse the response string
                //printf("\r\nScreen size: %d rows, %d columns\r\n", user->term.rows, user->term.cols);
                if (sscanf(user->escape_buffer, "[%hd;%hd", &user->term.rows, &user->term.cols) == 2) {
                    //printf("\r\nScreen size: %d rows, %d columns\r\n", user->term.rows, user->term.cols);
                } else {
                    printf("\r\nFailed to get screen size\r\n");
                }
                user->escape_mode = false;
                break;

        default:
                if(!isprint(value)){
                    printf("Invalid escape sequence character detected %2X %d\r\n",value, value);
                    user->escape_mode = false;
                    printf("Escape Seq : %s\r\n",user->escape_buffer);
                }
    }
}

// returns the number of free bytes in the input buffer
int user_input_buffer_free_space(user_context_t *user) {
    return  USER_CONSOLE_BUFFER_SIZE - user->pending_console_read;
}

// remove or backspace the last character in the buffer 
bool user_remove_char_from_buffer(user_context_t *user) {
    int result = false;               // returns if the character could be removed
    int new_index;
    if(user->pending_console_read == 0) return false;
    user->pending_console_read--;
    new_index = user->lineIndex-1;
    if(new_index < 0) {
        new_index = sizeof(user->linebuffer)-1;
    }

    user->lineIndex = new_index;
    return true;

}

// Add a character to the input line buffer, mark when an \r is recieved to allow task to execute
bool user_add_char_to_input_buffer(user_context_t *user, int value) {
    if(value == '\n') {
        //printf("Ignored \\n\n");
        return false;                // for now ignore \n 
    }

    bool result = false;

    if (user->telnet_state == 0) { /* normal (pass-through) mode */
		if (value == IAC) {
			user->telnet_state = 1;
			return result;
		}
	}

    if(user->telnet_state > 0) {
        telnet_process_state(user,value);
        return result;
    }

    if(value == '\e') {             // receive an escape sequence from the terminal
        user->escape_mode = true;
        user->escape_index = 0;
        return result;
    }

    if(user->escape_mode) {
        process_escape_commands(user,value);
        return false;
    }

    if(user->telnet_prev == 13 && value == 0) { // sometimes telnet sends a 0 after a \r
        user->telnet_prev = 13; 
    } else {
        if(value == '\b' || value == 0x7F) {                        // backspace or delete
            if(user_remove_char_from_buffer(user)) {    // nothing to do if nothing in the buffer
                user_write(user,"\b \b");  
            }                             // erase the character on the console
        } else {
            echoUserChar(user, value);          // echo the character back to the user
            user->telnet_prev = value;
            user->linebuffer[user->lineIndex] = (char)value;
            int newindex  = user->lineIndex+1;   
            //printf("\nadd_char %2x, lineIndex %d, pending count %d, readpos %d\n",value,user->lineIndex,user->pending_console_read,user->lineReadPos);
            if(newindex > sizeof(user->linebuffer)-1) {
                newindex = 0;               // wrap it around
            }
            //printf("\n0 add_char %2x, lineIndex %d, pending count %d, readpos %d\n",value,user->lineIndex,user->pending_console_read,user->lineReadPos);
            if(newindex != user->lineReadPos) {          //the buffer is full what to do!!
                user->lineIndex = newindex;
                user->pending_console_read++;           // tracks the number of bytes available to read
            }else {
                result = true;
            }
        }
    }

    //printf("\n\r1 add_char %2x, lineIndex %d, pending count %d, readpos %d\n\r",value,user->lineIndex,user->pending_console_read,user->lineReadPos);
    
    if(value == '\r') {
        user->WaitingRead = io_complete;
        user->available_lines++;              // turns out the circular buffer may contain more that one line
        result = true;                        // indicates that the io is available
    }

    //printf("\n2 add_char %2x, lineIndex %d, pending count %d, readpos %d\n",value,user->lineIndex,user->pending_console_read,user->lineReadPos);
    return result;   // indicates that the message status complete/not complete
}

// remove the processed line from the input buffer reset all to zero
void user_complete_read_from_input_buffer(user_context_t *user) {
    user->pending_console_read = 0;
    user->lineReadPos = user->lineIndex == 0 ? sizeof(user->lineIndex)-1 : user->lineIndex-1 ;
    user->WaitingRead = io_none;
}

//get the next character, adjust the read state depending upon the data
char getnextchar(user_context_t *user) {
SkipLF:
    int nextread = user->lineReadPos+1;
    if(nextread >= sizeof(user->linebuffer)) nextread=0;
    //printf("Nextread=%d,readpos=%d, index %d :\n ",nextread,user->lineReadPos,user->lineIndex);
    if(user->pending_console_read == 0 || nextread == user->lineIndex) {
        //printf("Exit Pending=%d, index=%d\n",user->pending_console_read,user->lineIndex);
        return '\r';
    }

    user->pending_console_read--;
    user->lineReadPos = nextread;
    char ch = user->linebuffer[nextread];
    if(ch == '\n') goto SkipLF;
    // printf("ch=%2X(%c)\n\r",ch,isprint(ch)?ch:'~');   //debugmod

    //printf("Nextread=%d,readpos=%d, pending=%d, ch=%3X\n",nextread,user->lineReadPos,user->pending_console_read,ch);

    if(ch=='\r') {
        if(user->available_lines > 0) {
            user->available_lines--;
            if(user->available_lines == 0) {
                user->WaitingRead = io_none;
            } else {
                user->WaitingRead = io_complete;
            }
        } else {
            user->WaitingRead = io_none;
        }
    }
    return ch;
}

int user_get_line(user_context_t *user, char *cmdline_buffer,int cmd_length) {
    int counter = 0;
    int pos = 0;
    cmdline_buffer[0] = '\0';
    while(counter < cmd_length-1) {
        char ch = getnextchar(user);
        if(ch == '\r' || ch == '\0') {
           cmdline_buffer[counter] = '\0';
           break;
        }
        cmdline_buffer[counter++] = ch;
        cmdline_buffer[counter]='\0';
    }

    return counter;
    
}

int getUserChar(user_context_t *user) {
    int value;
    value = getnextchar(user);
    return value;
}

bool user_line_available(user_context_t * user){
    return user->available_lines > 0 ? true : false;
}

bool user_char_available( user_context_t *user) {
    return user->pending_console_read > 0 ? true : false;
}

int echoUserChar(user_context_t *user, int c) {
    if(user->state.client_pcb == NULL ) {
        if(c == '\r')
            putchar('\n');
        putchar(c);
    } else {
        if(user->telnet_will_echo) {
            char buff[2];
            buff[0] = (char)c;
            buff[1] = '\n';
            tcp_server_send_msg_len(user, buff,c == '\r' ? 2 : 1);
            tcp_server_flush(user);  // flush the buffer
        }
    }
}

int putUserChar(user_context_t *user, int c) {
     TCP_SERVER_T *state = &user->state;
     //printf("putuserchar %2X\r\n", c);
    if(user->state.client_pcb == NULL ) {
        putchar(c);
    } else {
        char buff[2];
        buff[0] = (char)c;
        buff[1] = '\0';
        tcp_server_send_msg_len(user, buff, 1);
        if(c == '\r') {
            tcp_server_flush(user);  // flush the buffer
        }
    }
}

bool user_write(user_context_t *user, const char *buffer) {
    TCP_SERVER_T *state = &user->state;
    if(user->state.client_pcb == NULL ) {
        printf(buffer);
    } else {
        tcp_server_send_message(user,(char *)buffer);
    }
    return true;
}

void user_flush(user_context_t *user) {
    TCP_SERVER_T *state = &user->state;

    if(user->state.client_pcb != NULL ) {
        tcp_server_flush(user);  // flush the buffer
    } else {
        fflush(stdout);
    }   
}   

void user_push_input_buffer(user_context_t *user, char *input) {
    while(*input) {
        user_add_char_to_input_buffer(user,*(input++));
    }
} 

void clear_console_buffer(user_context_t *user) {
    user->lineReadPos = user->lineIndex -1;
}

// set the corect path for everyone
char *user_set_file_path(user_context_t *user, char *filepath,int pathlen) {
    char fullpath[256];
    if(user->SystemUser) {
        snprintf(fullpath,sizeof(fullpath),"%s",filepath);
    } else {
        snprintf(fullpath,sizeof(fullpath),"/home/%s-%s/%s",user->username,user->password,filepath);
        //now replace all ../ with nothing to prevent directory traversal
        char *p;
        while((p = strstr(fullpath, "../")) != NULL) {
            memmove(p, p + 3, strlen(p + 3) + 1);
        }
    }
    // put the path into the original buffer
    strncpy(filepath,fullpath,pathlen-1);
    return filepath;
}

// check if the home directory exists for the user
// if it exists return true and the full path in fullpath
bool exists_home(user_context_t *user, char *username, char *Dir_password, int dirpwlen) {
    DIR dir;
    FILINFO fno;
    FRESULT res;
    size_t ulen = strlen(username);
    res = f_opendir(&dir, "/home");
    if (res != FR_OK) return false;
    bool found = false;
    for (;;) {
        res = f_readdir(&dir, &fno);
        if (res != FR_OK || fno.fname[0] == 0) break;
        if (!(fno.fattrib & AM_DIR)) continue;
        const char *name = fno.fname;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        if (strncmp(name, username, ulen) == 0) {
            char next = name[ulen];
            if (next == '-' || next == '\0') { 
                found = true; 
                // return the password part for this directory
                if(Dir_password != NULL) strncpy(Dir_password,&fno.fname[ulen+1],dirpwlen-1);
                break; 
            }
        }
    }

    f_closedir(&dir);
    return found;
}

