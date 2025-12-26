// user fucntions for tinybasic multi-user time share system
#include <stdio.h>
#include <malloc.h>
#include "pico/stdlib.h"
#include "pico/sha256.h"
#include "hardware/spi.h"
#include "hardware/timer.h"
#include "hardware/clocks.h"
#include "pico/cyw43_arch.h"

#include "tcp_interface.h"
#include "FileSystem.h"
#include "telnetserver.h"
#include "user.h"
#include "interpreter.h"

extern char __StackLimit, __bss_end__;
extern uint64_t timerIdle;

const char * status_text[] = {"Wait Con", "Activating","Shell","Tiny Basic","Help","Logoff"};

const char * userprompt = " > ";

user_context_t *ActiveUsers = 0;      // Head of linked list of active users
user_context_t *RunningBasic = 0;     // list of users running basic programs
user_context_t *WaitingIO = 0;        // list of users waiting io to complete
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

    if (!user) {
        return NULL;
    }

    char logon_type = ':';
    char username[32];
    char password[32];

    // get the command line
    user_get_line(user,userinfo,sizeof(userinfo));

    // Parse the userinfo string
    if (sscanf(userinfo, "%31[^:?]%c%31s", username,&logon_type,password) == 3) {
    } else {
        return NULL;  // Bad format
    }

    hash_it(password,strlen(password));

    // printf("Parsed login info - Username: '%s', Logon Type: '%c', Password: '%s'\n", username, logon_type, password);

    if (logon_type == '?') {  // process a new user request
        user_context_t *existing_user = find_user_by_username(username);
        if (existing_user) {
            if(strncmp(existing_user->password, password, sizeof(existing_user->password)) == 0) {
                snprintf(buffer,sizeof(buffer),"Someone is aleady logged in with that user username and password\n\r");
                user_write(user,buffer);
                return NULL; // if password matches someone is already logged in with that username
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
                 snprintf(buffer,sizeof(buffer),"That user is logged in but passwords do not match\n\r");
                 user_write(user,buffer);
                 return (user_context_t *)false; // Invalid password and user combination
                }
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
            } else {                       // if user is logged in then create a new context for this connection
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
                        return NULL;
                    }
                }  
            }
            snprintf(buffer,sizeof(buffer),"Welcome  %s enjoy your stay\n\r", username);
            user_write(user,buffer);
            update_user_activity(user);
            return user;
        }
    }
    
    snprintf(buffer,sizeof(buffer),"Login failed due to bad format: '%s'\n\r", userinfo);
    user_write(user,buffer);
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

// Add a character to the input line buffer, mark whenan \r is recieved to allow task to execute
bool user_add_char_to_input_buffer(user_context_t *user, int value) {
    if(value == '\n') {
        //printf("Ignored \\n\n");
        return false;                // for now ignore \n 
    }
    bool result = false;

    user->linebuffer[user->lineIndex] = (char)value;
    int newindex  = user->lineIndex+1;
 //printf("\nadd_char %2x, lineIndex %d, pending count %d, readpos %d\n",value,user->lineIndex,user->pending_console_read,user->lineReadPos);
    if(newindex > sizeof(user->linebuffer)-1) {
        newindex = 0;               // wrap it around
    }
 //printf("\n0 add_char %2x, lineIndex %d, pending count %d, readpos %d\n",value,user->lineIndex,user->pending_console_read,user->lineReadPos);
    if(newindex != user->lineReadPos) { //the buffer is full what to do!!
            user->lineIndex = newindex;
            user->pending_console_read++;
    } else {
        result = true;
    }
 //printf("\n1 add_char %2x, lineIndex %d, pending count %d, readpos %d\n",value,user->lineIndex,user->pending_console_read,user->lineReadPos);
    
    if(value == '\r') {
        user->WaitingRead = io_complete;
        user->available_lines++;              // turns out the circular buffer may contain more that one line
        result = true;                        // indicates that the io is available
    } 
 //printf("\n2 add_char %2x, lineIndex %d, pending count %d, readpos %d\n",value,user->lineIndex,user->pending_console_read,user->lineReadPos);
    return result;   // indicates that the message statuc complete/not complete
}

// remove the processed line from the input buffer reset all to zero
void user_complete_read_from_input_buffer(user_context_t *user) {
    user->pending_console_read = 0;
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
        snprintf(fullpath,sizeof(fullpath),"/user/%s-%s/%s",user->username,user->password,filepath);
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

//------------------------------------------------------------------------------
// utility function trim leading and trailing spaces from a string
char * strtrim(char *str) {
    int i, begin = 0, end = strlen(str) - 1;

    // Find the index of the first non-space character
    while (begin <= end && isspace((unsigned char) str[begin])) {
        begin++;
    }

    // Find the index of the last non-space character
    while (end >= begin && isspace((unsigned char) str[end])) {
        end--;
    }

    // Shift all non-space characters to the start of the string array
    for (i = begin; i <= end; i++) {
        str[i - begin] = str[i];
    }

    // Null terminate the string at the new end
    str[i - begin] = '\0';
    return str;
}
// Begin user shell commands ___________________________________________________________________________________________


void user_who(user_context_t *send_to) {
    user_context_t *user = get_user_list();
    struct tcp_pcb *tpcb = send_to->state.client_pcb;
    char buffer[256];
    char longname[64];
    unsigned int TotalUserMemory = 0;

    sprintf(buffer,"User Count : %d\n\r",count_active_users());
    user_write(send_to,buffer);
    
    while (user) {
        snprintf(longname,sizeof(longname),"%10s",user->username ? user->username : "No Loggin");
        if(user->state.client_pcb == NULL) {
            snprintf(longname+strlen(longname),sizeof(longname)-strlen(longname)," (Local Console)");
        } else {
            snprintf(longname+strlen(longname),sizeof(longname)-strlen(longname)," (%d.%d.%d.%d:%d)",
                ip4_addr1_16(&user->state.client_pcb->remote_ip),
                ip4_addr2_16(&user->state.client_pcb->remote_ip),
                ip4_addr3_16(&user->state.client_pcb->remote_ip),
                ip4_addr4_16(&user->state.client_pcb->remote_ip),
                user->state.client_pcb->remote_port);
        }

        snprintf(buffer,sizeof(buffer),"%8X, %-35s, %-12s, %-3s, %9llu %9llu, %4s, Mem(%6u), io: wr(%1d) rd(%1d) %c\n\r",
                        user->state.client_pcb,
                        longname,
                        status_text[user->level], 
                        user->logged_in ? "On " : "Off",
                        user->last_active_time , user->active_time_used,
                        user->SystemUser ? "Root" : "User",
                        (user->i_Core ? USER_MEMORY_SIZE: 0)+sizeof(user_context_t),
                        user->WaitingWrite,user->WaitingRead,user->state.client_pcb == user->state.client_pcb ? '*':' '
                    );
        user_write(send_to,buffer);
        TotalUserMemory += (user->i_Core ? USER_MEMORY_SIZE: 0)+sizeof(user_context_t);
        user = user->next;
    }
    snprintf(buffer,sizeof(buffer),"Total User Memory in use : %u bytes cpu time %-9llu\n\r",TotalUserMemory,timerIdle);
    user_write(send_to,buffer);
    return;
}

// help command
const char * user_help_text[] = {"Commands:(case is ignored)\n\r",
                                "  Who - see who is logged on\n\r",
                                "  Free - See amount of available memory\n\r",
                                "  Quit - Log off session\n\r",
                                "  Basic - Warm Start Basic, use : bye to exit Basic and return to shell\r\n",
                                "  Clear - Clear the screen\n\r"
                                "  Dir or LS [dirpath] - List files in directory\n\r",
                                "  Type or Cat <filepath> - display contents of file\n\r",
                                "  Mkdir <dirname> - create a directory\n\r",
                                "  Rmdir <dirname> - remove a directory\n\r",
                                "  Del or rm <path to file> - delete a file\n\r",
                                "  Rename/Mv <sourcefile> <destfile> - rename or move a file\n\r",
                                "  Help - display this help message\n\r",
                                "  Send <username> 'Message Text' - send message to another user\n\r",
                                "Basic program commands:\n\r",
                                "  Load <filename> - Load a Tiny Basic program from file into memory\n\r",
                                "  Save <filename> - Save the current Tiny Basic program memory to a file\n\r",
                                "  List - List the current Tiny Basic program in memory\n\r",
                                "  Run - Run the current Tiny Basic program in memory\n\r",
                                "  FreeMem - display the amount of free memory for basic programs\n\r",
                                "  Library - display a list of programs in the library\n\r",
                                "  Get - get a copy of a program from the library to your local directory\n\r",
                                NULL};
const char * user_help_Sys[] = {"  Broadcast 'Message Text' - send message to all users[Sysuser only]\n\r",
                                "  Force <username> - Force user logoff[Sysuser only]\n\r",
                                "  Kill <task id>   - kill a particular user task by ID\n\r",
                                NULL   
                                };

void user_help_print(user_context_t *send_to){
    user_write(send_to,"\e[2J\e[H"); // clear screen
    user_write(send_to,"\n\rTiny Basic Time Share System Help\n\r");
    int count = 0;
    while(user_help_text[count]) {
        user_write(send_to,user_help_text[count]);
        count++;
    }
    if(send_to->SystemUser)  {
        user_write(send_to,"\n\rSystem User Commands:\n\r");
        count = 0;
        while(user_help_Sys[count]) {
            user_write(send_to,user_help_Sys[count]);
            count++;
        }  
    }
}

// free command
void user_free_space(user_context_t *send_to) {
    char buffer[128];
    struct mallinfo m = mallinfo();
    uint32_t total_heap_size = &__StackLimit  - &__bss_end__; // adjust if necessary
    uint32_t free_sram = total_heap_size - m.uordblks;
    snprintf(buffer,sizeof(buffer),"Base Mem 512K, System:%u, User:%u, used %u, free %u\n\r", 512*1024 - total_heap_size, total_heap_size, m.uordblks, free_sram);
    user_write(send_to,buffer);
}

// user quits/ logs off
void user_quit(user_context_t *user) {
    char buffer[128];
    if(!user->SystemUser)  {
        user_logoff(user);
    } else {
        snprintf(buffer,sizeof(buffer),"System User %s cannot quit session\n\r",user->username);
        user_write(user,buffer);
    }
}

// display a directory listing for the user
// system sees / users see only thier home directory
void user_directory_listing(user_context_t *user, char *buffer, int buffer_length) {
    display_directory(user,  buffer, buffer_length,false);
}

// library management functions
void user_display_library(user_context_t *user){
    const char libcmd[]={"lib /library"};
    display_directory(user, (char *)libcmd , sizeof(libcmd),true);
}

void user_get_library_entry(user_context_t *user, char *buffer, int buffer_length){
    char cmd[20];
    char buf[128];
    char fromname[128];
    char toname[128];
    sscanf(buffer, "%19s %127s",cmd,buf);
    strcpy(fromname,"/library/");
    strcat(fromname,buf);
    strcpy(toname,buf);
    user_set_file_path(user,toname,sizeof(toname));
    Copy_file(user,fromname,toname);
}

// create a directory for the user
void user_create_dir(user_context_t *user, char *cmdline, int buffer_length) {
    char cmd[20];
    char dirname[128];
    sscanf(cmdline, "%19s %127s",cmd,dirname);
    user_create_directory(user,dirname);
}

// delete a directory for the user
void user_delete_dir(user_context_t *user, char *cmdline, int cmd_length) {
    char cmd[20];
    char dirname[128];
    sscanf(cmdline, "%19s %127s",cmd,dirname);
    user_remove_directory(user,dirname);
}

// rename a file for the user
void user_rename_file(user_context_t *user, char *cmdline, int cmd_length) {
    char cmd[20];
    char sourcefile[128];
    char destfile[128];
    sscanf(cmdline, "%19s %127s %127s",cmd,sourcefile,destfile);
    user_rename_user_file(user,sourcefile,destfile);
}

// unknown command handler
void user_unknown_command(user_context_t *user, char *cmdline, int cmd_length) {
    char buffer[64];
    if(cmdline[0] == 0) {
        user_write(user,"\r\n");
        return;
    }
    snprintf(buffer,sizeof(buffer),"Unknown command: '%s'\n\r",cmdline);
    user_write(user,buffer);
}

// cat or type the content of a file
void user_type_file(user_context_t *user, char *cmdline, int cmd_length) {
    FIL fil;
    FRESULT fr;
    char buffer[256];
    char filename[128];
    sscanf(cmdline, "%127s %127s",buffer,filename);
    user_set_file_path(user,filename,sizeof(filename));

    fr = f_open(&fil,filename, FA_READ);
    if (FR_OK != fr) {
        snprintf(buffer,sizeof(buffer),"error: %s (%d)\n\r", filename, FRESULT_str(fr), fr);
        user_write(user,buffer);
        return;
    }
    while (1) {
        UINT br; // number of bytes read
        char read_buffer[129];
        fr = f_read(&fil, read_buffer, 128, &br);
        if (FR_OK != fr) {
            snprintf(buffer,sizeof(buffer),"error: %s (%d)\n\r", filename, FRESULT_str(fr), fr);
            user_write(user,buffer);
            break;
        }
        if (br == 0) {
            break; // end of file
        }
        read_buffer[br] = '\0'; // null terminate
        user_write(user,read_buffer);
    }
    user_write(user,"\n\r");
    f_close(&fil);
}

void user_broadcast_message(user_context_t *user, char *cmdline, int cmd_length) {
    char cmd[20];
    char message[200];
    char buffer[256];
    if(!user->SystemUser) {
        user_write(user,"Broadcast command is only available to System Users\n\r");
        return;
    }

    sscanf(cmdline, "%19s %199[^\n\r]",cmd,message);
    user_context_t *target_user = get_user_list();
    while (target_user) {
        if(target_user->state.client_pcb != NULL) {
            snprintf(buffer,sizeof(buffer),"Broadcast Message from %s: %s\n\r",user->username,message);
            user_write(target_user,buffer);
        }
        target_user = target_user->next;
    }

    snprintf(buffer,sizeof(buffer),"Broadcast Message sent to all connected users\n\r");
    user_write(user,buffer);
} 

void user_send_message(user_context_t *user, char *cmdline, int cmd_length) {
    char cmd[20];
    char target_username[64];
    char message[200];
    char buffer[256];
    sscanf(cmdline, "%19s %63s %199[^\n\r]",cmd,target_username,message);
    user_context_t *target_user = find_user_by_username(target_username);
    if(!target_user) {
        snprintf(buffer,sizeof(buffer),"User %s not found\n\r",target_username);
        user_write(user,buffer);
        return;
    }
    if(target_user->state.client_pcb == NULL && !target_user->SystemUser) {
        snprintf(buffer,sizeof(buffer),"User %s not connected\n\r",target_username);
        user_write(user,buffer);
        return;
    }
    snprintf(buffer,sizeof(buffer),"Message from %s: %s\n\r",user->username,message);
    user_write(target_user,buffer);
    snprintf(buffer,sizeof(buffer),"Message sent to %s\n\r",target_username);
    user_write(user,buffer);
}

void user_force_user(user_context_t *user, char *cmdline, int cmd_length) {
    char cmd[20];
    char target_username[64];
    char buffer[128];
    if(!user->SystemUser) {
        user_write(user,"Force command is only available to System Users\n\r");
        return;
    }

    sscanf(cmdline, "%19s %63s",cmd,target_username);
    user_context_t *target_user = find_user_by_username(target_username);
    if(!target_user) {
        snprintf(buffer,sizeof(buffer),"User %s not found\n\r",target_username);
        user_write(user,buffer);
        return;
    }
    if(target_user->SystemUser) {
        snprintf(buffer,sizeof(buffer),"Cannot force logoff of System User %s\n\r",target_username);
        user_write(user,buffer);
        return;
    }
    target_user->level = user_removed;
    snprintf(buffer,sizeof(buffer),"User %s has been forced to logoff\n\r",target_username);
    user_write(user,buffer);
}

void user_kill_task(user_context_t *user, char *cmdline, int cmd_length) {
    char cmd[20];
    struct tcp_pcb *target_taskid;
    char buffer[128];
    if(!user->SystemUser) {
        user_write(user,"Kill command is only available to System Users\n\r");
        return;
    }
    sscanf(cmdline, "%19s %X",cmd,target_taskid);
    user_context_t *target_task = find_user_by_tcp_pcb(target_taskid);
    if(!target_task) {
        snprintf(buffer,sizeof(buffer),"Task ID %X not found\n\r",target_taskid);
        user_write(user,buffer);
        return;
    }
    if(target_task->SystemUser) {
        snprintf(buffer,sizeof(buffer),"Cannot kill tasks of System User ID=%X\n\r",target_taskid);
        user_write(user,buffer);
        return;
    }
    target_task->level = user_removed;
    snprintf(buffer,sizeof(buffer),"Task %X has been killed\n\r",target_taskid);
    user_write(user,buffer);
}


// set tinybasic define to load from file
void user_basic_load_file(user_context_t *user, char *cmdline, int cmd_length) {
    char cmd[20];
    char filename[128];
    char buffer[256];
    FIL *ifile;

    sscanf(cmdline, "%19s %127s",cmd,filename);
    user_set_file_path(user,filename,sizeof(filename));

    snprintf(buffer,sizeof(buffer),"Loading BASIC program from file %s\n\r",filename);
    user_write(user,buffer);
    if(!user->BasicInitComplete) {              // if not initialized then do so now
        UserInitTinyBasic(user,(char *)0);
    }

    ifile = calloc(1, sizeof(FIL));
    if(ifile) {
        FRESULT fr = f_open(ifile, filename, FA_READ);
        if (FR_OK != fr) {
            snprintf(buffer,sizeof(buffer),"f_open(%s) for read error: %s (%d)\n\r", filename, FRESULT_str(fr), fr);
            user_write(user,buffer);
            free(ifile);
            ifile = NULL;
            return;
        }
        ColdStart(user);            // reset the interpreter to be empty
        clear_console_buffer(user);
        user->i_inFile = ifile;
        user->ExitWhenDone = true;
        user->level = user_basic;
        user->WaitingRead = io_none;
        user->echo=false;
    } else {
        user_write(user,"Failed to allocate file object for BASIC program load\n\r");
    }
}

void user_basic_run_file(user_context_t *user, char *cmdline , int cmd_length) {
    if(!user->BasicInitComplete) {              // if not initialized then do so now
        user_write(user,"No BASIC program loaded\n\r");
        return;
    }

    user_push_input_buffer(user,"RUN\r");            // push the run command onto the input buffer
    user->ExitWhenDone = true;
    user->level=user_basic;                          // run the program
}

void user_basic_list_file(user_context_t *user) {
    if(!user->BasicInitComplete) {              // if not initialized then just return
        user_write(user,"No BASIC program loaded\n\r");
        return;
    }
    ListIt(user,0,0);                                // list the program to the console
}

void user_basic_save_file(user_context_t *user, char *cmdline, int cmd_length) {
    char cmd[20];
    char filename[128];
    char buffer[256];
    FIL *ofile;

    if(!user->BasicInitComplete) {              // if not initialized then do nothing
        user_write(user,"Nothing to save\n\r");
        return;
    }

    sscanf(cmdline, "%19s %127s",cmd,filename);
    user_set_file_path(user,filename,sizeof(filename));
    snprintf(buffer,sizeof(buffer),"Saving BASIC program to file %s\n\r",filename);
    user_write(user,buffer);

    ofile = calloc(1, sizeof(FIL));
    if(ofile) {
        FRESULT fr = f_open(ofile, filename, FA_WRITE | FA_CREATE_ALWAYS);
        if (FR_OK != fr) {
            snprintf(buffer,sizeof(buffer),"error: %s (%d)\n\r", filename, FRESULT_str(fr), fr);
            user_write(user,buffer);
            free(ofile);
            ofile = NULL;
            return;
        }
        user->i_oFile = ofile;
        ListIt(user,0,0);                                // save the listing to the output file
        f_close(ofile);
        free(ofile);
        ofile = NULL;
        user_write(user,"BASIC program save complete\n\r");
    } else {
        user_write(user,"Failed to allocate file object for BASIC program save\n\r");
    }
}  

void user_free_basic_memory(user_context_t *user) {
    char buffer[256];
    if(!user->BasicInitComplete) {
        UserInitTinyBasic(user,(char *)0);
    }
    int freemem = USER_MEMORY_SIZE - Peek2(user,EndProg);
    snprintf(buffer,sizeof(buffer),"Memory : Total %d, free %d, user Program : start %d, end %d\r\n",
                                    USER_MEMORY_SIZE,
                                    freemem,
                                    Peek2(user,UserProg),
                                    Peek2(user,EndProg));         // actual core offset from program space used or total user program length
                                    
    user_write(user,buffer);
}

// The shell command processor
const char *shellcmds[] = {"WHO","FREE","HELP","QUIT","BASIC","DIR","LS","MKDIR","RMDIR","CAT","TYPE","SEND","BROADCAST",
                           "CLEAR","FORCE","LOAD","SAVE","RENAME","MV","RM", "DEL","LIST","RUN","FREEMEM","LIBRARY","GET","KILL",0};
enum {CMD_WHO, CMD_FREE, CMD_HELP, CMD_QUIT, CMD_BASIC, CMD_DIR, CMD_LS, CMD_MKDIR, CMD_RMDIR, CMD_CAT, CMD_TYPE, CMD_SEND, CMD_BROADCAST, CMD_CLEAR,
     CMD_FORCE, CMD_LOAD, CMD_SAVE, CMD_RENAME,CMD_MV,CMD_RM,CMD_DEL,CMD_LIST,CMD_RUN,CMD_FREEMEM,CMD_LIBRARY, CMD_GET, CMD_KILL, CMD_UNKNOWN};

int lookup_shell_command(const char *cmd) {
    for (int i = 0; shellcmds[i] != 0; i++) {
        if (strncasecmp(cmd, shellcmds[i], strlen(cmd)) == 0) {
            return i;
        }
    }
    return CMD_UNKNOWN;
}

void userShell(user_context_t * user) {
    char cmd[20];
    int cmdindex;
    char buffer[256];

    // later this will be a basic program
    user_get_line(user,buffer,sizeof(buffer));
    strtrim(buffer);
    sscanf(buffer, "%19s",cmd);
    cmdindex = lookup_shell_command(cmd);

    DEBUG_printf("User Shell Request from %s id %8X User Type(%-7s) cmd(%s): '%s' \n\r",user->username,
                 user->SystemUser ? user->state.server_pcb:user->state.client_pcb,
                 user->SystemUser ? "System": "User",cmd,buffer);

    switch(cmdindex) {
        case CMD_WHO:
            user_who(user);
            break;
        case CMD_FREE:
            user_free_space(user);
            break;
        case CMD_HELP:
            user_help_print(user);
            break;
        case CMD_QUIT:
            user_quit(user);
            break;
        case CMD_BASIC:
            user->level = user_basic;
            break;
        case CMD_DIR:
        case CMD_LS:
            user_directory_listing(user,buffer,sizeof(buffer));
            break;
        case CMD_MKDIR:
            user_create_dir(user,buffer,sizeof(buffer));
            break;
        case CMD_RMDIR:
        case CMD_RM:
        case CMD_DEL:
            user_delete_dir(user,buffer,sizeof(buffer));
            break;
        case CMD_CAT:
        case CMD_TYPE:
            user_type_file(user,buffer,sizeof(buffer));
            break;
        case CMD_SEND:
            user_send_message(user,buffer,sizeof(buffer));
            break;
        case CMD_BROADCAST:
            user_broadcast_message(user,buffer,sizeof(buffer));
            break;
        case CMD_CLEAR:
            user_write(user,"\e[2J\e[H"); // clear screen
            break;
        case CMD_FORCE:
            user_force_user(user,buffer,sizeof(buffer));
            break;
        case CMD_LOAD:
            user_basic_load_file(user,buffer,sizeof(buffer));
            break; 
        case CMD_SAVE:
            user_basic_save_file(user,buffer,sizeof(buffer));
            break;
        case CMD_RENAME:
        case CMD_MV:
            user_rename_file(user,buffer,sizeof(buffer));;
            break;
        case CMD_LIST:
            user_basic_list_file(user);
            break;
        case CMD_RUN:
            user_basic_run_file(user,buffer,sizeof(buffer));;
            break;
        case CMD_FREEMEM:
            user_free_basic_memory(user);
            break;
        case CMD_LIBRARY:
            user_display_library(user);
            break;
        case CMD_GET:
            user_get_library_entry(user,buffer,sizeof(buffer));
            break;
        case CMD_KILL:
            user_kill_task;
            break;
        default:
            user_unknown_command(user,buffer,sizeof(buffer));
            break;
    }
}
