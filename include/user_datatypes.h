// Definitions to manage users logged in to the time share system
#ifndef user_datatypes_h
#define user_datatypes_h

#include <stdbool.h>
#include <stdint.h>
#include "ff.h"
#include "tcp_datatypes.h"

#define USER_MEMORY_SIZE (16 * 1024)        // 16 KB per user
#define USER_TIMEOUT_MS  (10 * 60 * 1000)   // 10 minutes inactivity timeout
#define USER_CONSOLE_BUFFER_SIZE 256
#define USER_NAME_LENGTH 32
#define USER_PASSWORD_LENGTH 32
#define ESC_BUFF_LEN 16                     // used to collect escape sequences as they arrive
#define TELNET_SB_BUFFER_LEN 64             // length of telnet subnegotiation buffer

#define user_new_connect 0                  // when a new connection before logging in
#define user_wait_loggin 1                  // Set when waiting for a login message
#define user_shell  2                       // currently in shell command mode
#define user_basic  3                       // currently running basic
#define user_help   4                       // currently in help system
#define user_removed 5                      // a user context is to be removed
#define user_needs_prompt 6                 // set when leaving basic
#define user_waiting_basic_io 7             // set when basic is waiting for io to complete

#define io_none 0
#define io_complete 1
#define io_waiting 2

#define FileType FIL*
#define aByte unsigned char

#define DEBUGON 1                           // 1 enables \t Debugging toggle, 0 disables
#define LOGSIZE 512                         // how much to log

// Pascal habits die hard..
#define true 1
#define false 0

typedef struct {
    uint16_t cols;
    uint16_t rows;
    uint16_t cursor_x;
    uint16_t cursor_y;
    uint8_t  fg_color;
    uint8_t  bg_color;
} terminal_t;


typedef struct user_context {
    struct user_context *next;              // Pointer to next user context in linked list
    struct user_context *prev;              // Pointer to previous user context in linked list
    TCP_SERVER_T state;                     // TCP server state for this user
    char username[USER_NAME_LENGTH];                      // This name combined with password defines the root directory for user..no real security at all
    char password[USER_PASSWORD_LENGTH];
    unsigned char linebuffer[USER_CONSOLE_BUFFER_SIZE];   // line buffer that gets information from console as available aka ring buffer
    unsigned char escape_buffer[ESC_BUFF_LEN];            // when in escape mode collects the escape sequence
    unsigned char telnet_sb_buffer[TELNET_SB_BUFFER_LEN]; // buffer to collect telnet subnegotiation data

    uint64_t last_active_time;       // Timestamp of the last activity for timeout management
    uint64_t active_time_used;       // the number of time slices used by this task/user
    size_t MemorySize;               // Size of the BASIC program memory space
  
    bool logged_in;                  // true if the user is logged in
    bool SystemUser;                 // true if this is a system user (e.g., admin)
    bool BasicInitComplete;          // set when the basic init has been completed
    bool running;                    // true when the basic interpreter is running
    bool ExitWhenDone;               // when set the interpreter will return when exection completes
    bool persist;                    // set for users that are not deleted
    bool echo;                       // turns the interpreter echo when reading from character on/off
    bool escape_mode;                // set when input is processing escape chars
    bool display_who;                // provides shortcut to display who is logged on

    int16_t escape_index;            // index into the escape index
    int16_t WaitingRead;             // waiting for read to complete, 0 no io, 1 data available, 2 waiting
    int16_t WaitingWrite;            // waiting for a write io to complete, 0 no io, 1 completed, 2 waiting
    int16_t level;                      // current processing level of user interface
    int16_t i_Debugging;                // >0 enables debug code
 
   // debugging stuff...
    int16_t i_DebugLog[LOGSIZE];        // quietly logs IL vm recent activity
    int16_t i_LogHere;                  // current index in DebugLog
    int16_t i_Watcher, i_Watchee;       // memory watchpoint

    // Static/global data:
  
    int16_t i_Lino, i_ILPC;             // current line #, IL program counter
    int16_t i_BP, i_SvPt;               // current, saved TB parse pointer
    int16_t i_SubStk, i_ExpnTop;        // stack pointers
    int16_t i_InLend, i_SrcEnd;         // current input line & TB source end
    int16_t i_UserEnd;                  // end of memory used bu program
    int16_t i_ILend, i_XQhere;          // end of IL code, start of execute loop
    int16_t i_Broken;                   // true to stop execution or listing
    int16_t  lineIndex;                 // where the ring buffer ends or next entry goes
    int16_t  lineReadPos;               // basically the head of the ring buffer
    int16_t  pending_console_read;      // Set when an irq for console data available
    int16_t  available_lines;           // this contains the count of lines in the buffer

    // telnet command process variables
    int16_t  telnet_cmd;
    int16_t  telnet_opt;
    int16_t  telnet_prev;
    int16_t  telnet_state;              // used when processing telnet commands
    int16_t  telnet_sb_index;          // index into the subnegotiation buffer

    // terminal control variables
    terminal_t term;                    // terminal management
    
    int telnet_cmd_count;               // number of the commands sent

    aByte *i_Core;                      // Pointer to the BASIC program memory
    FIL *i_inFile;                      // from option '-i' or user menu/button
    FIL *i_oFile;                       // from option '-o' or user menu/button
   
} user_context_t;


typedef struct active_fifo {
    user_context_t *user;
} active_fifo_t;

#endif // user_context_h