// Definitions to manage users logged in to the time share system
#ifndef user_datatypes_h
#define user_datatypes_h

#include "tcp_datatypes.h"

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

#define FileType FILE*
#define aByte unsigned char

#define DEBUGON 1                // 1 enables \t Debugging toggle, 0 disables */
#define LOGSIZE 4096             // how much to log */

// Pascal habits die hard..
#define true 1
#define false 0

typedef struct user_context {
    struct user_context *next;       // Pointer to next user context in linked list
    struct user_context *prev;       // Pointer to previous user context in linked list
    TCP_SERVER_T state;              // TCP server state for this user
    char username[32];               // This name combined with password defines the root directory for user..no real security at all
    char password[32];
   
    bool logged_in;                  // true if the user is logged in
    bool SystemUser;                 // true if this is a system user (e.g., admin)
    unsigned char WaitingRead;       // waiting for read to complete, 0 no io, 1 data available, 2 waiting
    unsigned char WaitingWrite;      // waiting for a write io to complete, 0 no io, 1 completed, 2 waiting
    int level;                       // current processing level of user interface
    uint64_t last_active_time;       // Timestamp of the last activity for timeout management
    bool BasicInitComplete;          // set when the basic init has been completed

    int i_Debugging;                 // >0 enables debug code
 
   /* debugging stuff... */
    int i_DebugLog[LOGSIZE];         // quietly logs recent activity
    int i_LogHere;                   // current index in DebugLog
    int i_Watcher, i_Watchee;        // memory watchpoint

    /* Static/global data: */
    aByte *i_Core;                   // Pointer to the BASIC program memory
    size_t MemorySize;               // Size of the BASIC program memory space
    int i_Lino, i_ILPC;              // current line #, IL program counter
    int i_BP, i_SvPt;                // current, saved TB parse pointer
    int i_SubStk, i_ExpnTop;         // stack pointers
    int i_InLend, i_SrcEnd;          // current input line & TB source end
    int i_UserEnd;                   // ???
    int i_ILend, i_XQhere;           // end of IL code, start of execute loop
    int i_Broken;                    // =true to stop execution or listing
    FileType i_inFile;               // from option '-i' or user menu/button
    FileType i_oFile;                // from option '-o' or user menu/button

    unsigned char linebuffer[256];   // line buffer that gets information from console as available
    int  lineLength;                 // length of complete input line
    int  lineIndex;                  // where the ring buffer starts and ends
    int  lineReadPos;                // position to read from
    int  pending_console_read;       // Set when an irq for console data available

} user_context_t;

#endif // user_context_h