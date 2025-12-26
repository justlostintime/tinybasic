// Definitions to manage users logged in to the time share system
#ifndef user_datatypes_h
#define user_datatypes_h

#include "tcp_datatypes.h"
#include <stdbool.h>
#include <stdint.h>
#include "ff.h"

#define USER_MEMORY_SIZE  (16 * 1024)      // 16 KB per user
#define USER_TIMEOUT_MS   (10 * 60 * 1000) // 10 minutes inactivity timeout

#define user_new_connect 0                 // when a new connection before logging in
#define user_wait_loggin 1                 // Set when waiting for a login message
#define user_shell  2                      // currently in shell command mode
#define user_basic  3                      // currently running basic
#define user_help   4                      // currently in help system
#define user_removed 5                     // a user context is to be removed

#define io_none 0
#define io_complete 1
#define io_waiting 2

#define FileType FIL*
#define aByte unsigned char

#define DEBUGON 1                // 1 enables \t Debugging toggle, 0 disables */
#define LOGSIZE 512              // how much to log */

// Pascal habits die hard..
#define true 1
#define false 0

typedef struct user_context {
    struct user_context *next;       // Pointer to next user context in linked list
    struct user_context *prev;       // Pointer to previous user context in linked list
    TCP_SERVER_T state;              // TCP server state for this user
    char username[32];               // This name combined with password defines the root directory for user..no real security at all
    char password[32];
    unsigned char linebuffer[256];   // line buffer that gets information from console as available

    uint64_t last_active_time;       // Timestamp of the last activity for timeout management
    uint64_t active_time_used;       // the number of time slices used by this task/user
    size_t MemorySize;               // Size of the BASIC program memory space
  
    bool logged_in;                  // true if the user is logged in
    bool SystemUser;                 // true if this is a system user (e.g., admin)
    bool BasicInitComplete;          // set when the basic init has been completed
    bool ExitWhenDone;               // when set the interpreter will return when exection completes
    bool persist;                    // set for users that are not deleted
    bool echo;                       // turns the interpreter echo when reading from character on/off

    int16_t WaitingRead;             // waiting for read to complete, 0 no io, 1 data available, 2 waiting
    int16_t WaitingWrite;            // waiting for a write io to complete, 0 no io, 1 completed, 2 waiting
    int16_t level;                       // current processing level of user interface
    int16_t i_Debugging;                 // >0 enables debug code
 
   /* debugging stuff... */
    int16_t i_DebugLog[LOGSIZE];         // quietly logs recent activity
    int16_t i_LogHere;                   // current index in DebugLog
    int16_t i_Watcher, i_Watchee;        // memory watchpoint

    /* Static/global data: */
  
    int16_t i_Lino, i_ILPC;              // current line #, IL program counter
    int16_t i_BP, i_SvPt;                // current, saved TB parse pointer
    int16_t i_SubStk, i_ExpnTop;         // stack pointers
    int16_t i_InLend, i_SrcEnd;          // current input line & TB source end
    int16_t i_UserEnd;                   // end of memory used bu program
    int16_t i_ILend, i_XQhere;           // end of IL code, start of execute loop
    int16_t i_Broken;                    // =true to stop execution or listing
    int16_t  lineIndex;              // where the ring buffer starts and ends
    int16_t  lineReadPos;
    int16_t  pending_console_read;   // Set when an irq for console data available
    int16_t  available_lines;        // this contains the count of lines in the buffer

    aByte *i_Core;                   // Pointer to the BASIC program memory
    FIL *i_inFile;                   // from option '-i' or user menu/button
    FIL *i_oFile;                    // from option '-o' or user menu/button


} user_context_t;

#endif // user_context_h