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
extern const char * status_text[];             // user status text
extern const char * userprompt;

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
char *convert_time_to_text(char *buffer, int buflen, uint64_t milliseconds) {
    uint64_t ms = milliseconds;

    // Calculate days, hours, minutes, seconds, and remaining milliseconds
    uint32_t days = ms / (1000 * 60 * 60 * 24);
    ms %= (uint64_t)(1000 * 60 * 60 * 24); // Remaining ms after extracting days
    
    uint32_t hours = ms / (1000 * 60 * 60);
    ms %= (uint64_t)(1000 * 60 * 60); // Remaining ms after extracting hours

    uint32_t minutes = ms / (1000 * 60);
    ms %= (uint64_t)(1000 * 60); // Remaining ms after extracting minutes

    uint32_t seconds = ms / 1000;
    ms %= 1000; // Remaining milliseconds

    // Print the result in a readable format (e.g., "01d 02h 03m 04s 005ms")
    snprintf(buffer, buflen,"%02ud days, %02uh hours, %02um minutes, %02us seconds, %03ums",
           days, hours, minutes, seconds, (uint32_t)ms);
    return buffer;
}

// who command
void user_who(user_context_t *send_to) {
    user_context_t *user = get_user_list();
    struct tcp_pcb *tpcb = send_to->state.client_pcb;
    char buffer[256],TimeBuffer[64],TimeBuffer2[64];
    char longname[64];
    unsigned int TotalUserMemory = 0;

    snprintf(buffer,sizeof(buffer),"User Count : %d , Active Basic='*', Shell='+'\n\r",count_active_users());
    user_write(send_to,buffer);
    snprintf(buffer,sizeof(buffer),"Process usage : Core 0 %s\n\rProcess usage : Core 1 %s\n\r Raw time ms: 0=%20lu, 1=%20lu\n\r",
                                     convert_time_to_text(TimeBuffer, sizeof(TimeBuffer),Process_time_core0),
                                     convert_time_to_text(TimeBuffer2, sizeof(TimeBuffer2),Process_time_core1));
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

        char activity = ' ';
        if(user == core1_user) activity = '*';
         else if(user == core0_user) activity = '+';

        snprintf(buffer,sizeof(buffer),"%8X, %-35s, %-12s, %-3s, %9llu %9llu, %4s, Mem(%6u), io: wr(%1d) rd(%1d) %c\n\r",
                        user->state.client_pcb,
                        longname,
                        status_text[user->level], 
                        user->logged_in ? "On " : "Off",
                        user->last_active_time , user->active_time_used,
                        user->SystemUser ? "Root" : "User",
                        (user->i_Core ? USER_MEMORY_SIZE: 0)+sizeof(user_context_t),
                        user->WaitingWrite,user->WaitingRead,activity
                    );
        user_write(send_to,buffer);
        TotalUserMemory += (user->i_Core ? USER_MEMORY_SIZE: 0)+sizeof(user_context_t);
        user = user->next;
    }
    uint64_t IdleTime = to_ms_since_boot(get_absolute_time()) - Process_time_core0 - Process_time_core1;
    snprintf(buffer,sizeof(buffer),"Total User Memory in use : %u bytes, CPU Idle time %s\n\r",TotalUserMemory,
                                convert_time_to_text(TimeBuffer, sizeof(TimeBuffer),IdleTime));
    user_write(send_to,buffer);
    return;
}
// user directory cleanup, remove directory of all disconnected users
void user_cleanup_disconnected_sessions(user_context_t *send_to) {
    {
        DIR dir;
        FILINFO fno;
        FRESULT fr;
        char users_path[] = "/home";
        char fullname[256];
        char msg[256];

        // Open /home directory
        fr = f_opendir(&dir, users_path);
        if (fr != FR_OK) {
            snprintf(msg, sizeof(msg), "Cleanup: failed to open %s (%s)\n\r", users_path, FRESULT_str(fr));
            user_write(send_to, msg);
            return;
        }

        // Iterate entries in /home
        while (1) {
            fr = f_readdir(&dir, &fno);
            char basename[32];
            char password[32];

            if (fr != FR_OK || fno.fname[0] == 0) break;               // error or end
            // skip '.' and '..'
            if (strcmp(fno.fname, ".") == 0 || strcmp(fno.fname, "..") == 0) continue;

            // extract username and password from directory name
            sscanf(fno.fname, "%31[^-]%*c%31s", basename,password);    // prevent overflow and get actuall name
           
            // we only care about directories named after users
            if (!(fno.fattrib & AM_DIR)) continue;

            // fno.fname is the username (directory name)
            user_context_t *u = find_user_by_username(basename,password);
     
            if (u != NULL && u->state.client_pcb != NULL) {
                // keep directory for active users
                continue;
            }

            // Build full path to user directory
            snprintf(fullname, sizeof(fullname), "%s/%s", users_path, fno.fname);
            snprintf(msg, sizeof(msg), "Cleanup: removing directory for user '%s' -> %s\n\r", fno.fname, fullname);
            user_write(send_to, msg);

            // Iterative recursive remove using a stack of paths
            char **stack = NULL;
            size_t stack_len = 0;
            // push root dir
            char *p = malloc(strlen(fullname) + 1);
            if (!p) {
                user_write(send_to, "Cleanup: memory allocation failed\n\r");
                continue;
            }
            strcpy(p, fullname);
            stack = malloc(sizeof(char*));
            if (!stack) { free(p); user_write(send_to, "Cleanup: memory allocation failed\n\r"); continue; }
            stack[0] = p;
            stack_len = 1;

            while (stack_len > 0) {
                // pop
                char *cur = stack[stack_len - 1];
                stack_len--;
                stack = realloc(stack, stack_len * sizeof(char*));

                // Try to open directory
                DIR d2;
                FILINFO finfo;
                FRESULT r2 = f_opendir(&d2, cur);
                if (r2 != FR_OK) {
                    // not a directory or cannot open -> try unlink (file or empty dir)
                    FRESULT ru = f_unlink(cur);
                    if (ru != FR_OK) {
                        snprintf(msg, sizeof(msg), "Cleanup: failed to unlink %s (%s)\n\r", cur, FRESULT_str(ru));
                        user_write(send_to, msg);
                    }
                    free(cur);
                    continue;
                }

                // Directory opened: iterate entries
                bool had_entries = false;
                while (1) {
                    r2 = f_readdir(&d2, &finfo);
                    if (r2 != FR_OK || finfo.fname[0] == 0) break;
                    if (strcmp(finfo.fname, ".") == 0 || strcmp(finfo.fname, "..") == 0) continue;
                    had_entries = true;
                    // build child path
                    char childpath[256];
                    snprintf(childpath, sizeof(childpath), "%s/%s", cur, finfo.fname);
                    if (finfo.fattrib & AM_DIR) {
                        // push directory onto stack
                        char *dup = malloc(strlen(childpath) + 1);
                        if (dup) {
                            strcpy(dup, childpath);
                            stack_len++;
                            stack = realloc(stack, stack_len * sizeof(char*));
                            stack[stack_len - 1] = dup;
                        } else {
                            snprintf(msg, sizeof(msg), "Cleanup: memory alloc failed for %s\n\r", childpath);
                            user_write(send_to, msg);
                        }
                    } else {
                        // file -> unlink
                        FRESULT ru = f_unlink(childpath);
                        if (ru != FR_OK) {
                            snprintf(msg, sizeof(msg), "Cleanup: failed to unlink %s (%s)\n\r", childpath, FRESULT_str(ru));
                            user_write(send_to, msg);
                        }
                    }
                }
                f_closedir(&d2);

                // After processing children, attempt to remove the (now hopefully empty) directory
                FRESULT ru = f_unlink(cur);
                if (ru != FR_OK) {
                    snprintf(msg, sizeof(msg), "Cleanup: failed to remove dir %s (%s)\n\r", cur, FRESULT_str(ru));
                    user_write(send_to, msg);
                }
                free(cur);
            }

            free(stack);
        }

        f_closedir(&dir);
        user_write(send_to, "Cleanup: finished scanning /home\n\r");
    }
}

// help command
const char * user_help_text[] = {"Timeshare Commands (case is ignored), Parameters with [ ] are optional\n\r",
                                "  home or ctrl-home or home to see who is logged in and other system info\n\r",
                                "  end or ctrl-end to stop execution of basic program\n\r",
                                "Shell commands:\n\r",
                                "  Who - see who is logged on\n\r",
                                "  Free - See amount of available memory\n\r",
                                "  Quit - Log off session\n\r",
                                "  Basic - Warm Start Basic, use : bye to exit Basic and return to shell\r\n",
                                "  Cls - Clear the screen\n\r"
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
                                "  Run [<filename>] - Run Currently loaded program or with <filename> Run specified program\n\r",
                                "  FreeMem - display the amount of free memory for your basic program\n\r",
                                "  Library [<EntryName>] - list library or display programs in library \n\r",
                                "  Doc [<EntryName>] - list documents or display entry in Doc Library \n\r",
                                "  Shared [<EntryName>] - list Shared Progs or display progs in shared Library \n\r",
                                "  Get - get a copy of a program from the library to your local directory\n\r",
                                NULL};
const char * user_help_Sys[] = {"  Broadcast 'Message Text' - send message to all users[Sysuser only]\n\r",
                                "  Force <username> - Force user logoff[Sysuser only]\n\r",
                                "  Kill <task id>   - kill a particular user task by ID\n\r",
                                "  Cleanup - cleanup any disconnected user sessions[Sysuser only]\n\r",
                                NULL   
                                };

void user_help_print(user_context_t *send_to){
    terminal_set_colors(send_to,TERM_COLOR_LIGHT_GREEN,TERM_COLOR_BLACK);
    terminal_clear(send_to);
    terminal_puts(send_to,"\n\rTiny Basic Time Share System Help\n\r");
    int count = 0;
    while(user_help_text[count]) {
        terminal_puts(send_to,user_help_text[count]);
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
// root will see / - users see only thier home directory
void user_directory_listing(user_context_t *user, char *dirpath) {
    display_directory(user, dirpath,false);
}

// library management functions
void user_display_directory(user_context_t *user,char *dirpath,char *parm1) {
    char fullpath[128];
    if(parm1[0]) {
        strcpy(fullpath,dirpath);
        strcat(fullpath,"/");
        strcat(fullpath,parm1);
        user_type_file(user,fullpath,true);
    } else {
        display_directory(user, dirpath,true);
    }
}

void user_get_library_entry(user_context_t *user, char *filepath){
    char fromname[128];
    char toname[128];
    strcpy(fromname,"/library/");
    strcat(fromname,filepath);
    strcpy(toname,filepath);
    user_set_file_path(user,toname,sizeof(toname));
    Copy_file(user,fromname,toname);
}

void user_share_file(user_context_t *user, char *filenametoshare){}  

// create a directory for the user
void user_create_dir(user_context_t *user, char *dirname) {
    user_create_directory(user,dirname);
}

// delete a directory for the user
void user_delete_dir(user_context_t *user, char *dirname) {
    user_remove_directory(user,dirname);
}

// rename a file for the user
void user_rename_file(user_context_t *user, char *sourcefile, char *destfile) {
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
void user_type_file(user_context_t *user, char *filePath,bool UseFullPath) {
    FIL fil;
    FRESULT fr;
    char filename[128];
    strncpy(filename,filePath,sizeof(filename)-1);
    char buffer[256];
    if(!UseFullPath) user_set_file_path(user,filename,sizeof(filename)-1);

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
    user_context_t *target_user = find_user_by_username(target_username,NULL);
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

void user_force_user(user_context_t *user, char *target_username) {
    char buffer[128];
    if(!user->SystemUser) {
        user_write(user,"Force command is only available to System Users\n\r");
        return;
    }
    user_context_t *target_user = find_user_by_username(target_username,NULL);
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

void user_kill_task(user_context_t *user, char *target_taskid_str) {
    char buffer[128];
    struct tcp_pcb *target_taskid = NULL;
    if(!user->SystemUser) {
        user_write(user,"Kill command is only available to System Users\n\r");
        return;
    }
    sscanf(target_taskid_str, "%X",target_taskid);
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
void user_basic_load_file(user_context_t *user, char *filepath, bool runafterload) {
    char filename[128];
    char buffer[256];
    FIL *ifile;
    strncpy(filename,filepath,sizeof(filename)-1);
    user_set_file_path(user,filename,sizeof(filename));

    snprintf(buffer,sizeof(buffer),"Loading BASIC program from file %s\n\r",filename);
    user_write(user,buffer);
    if(!user->BasicInitComplete) {              // if not initialized then do so now
        UserInitTinyBasic(user,tinybasicIL);
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
        user->runafterload = runafterload;
    } else {
        user_write(user,"Failed to allocate file object for BASIC program load\n\r");
    }
}

void user_basic_run_file(user_context_t *user, char *program_name) {
    if(program_name[0]) {
        user_basic_load_file(user,program_name,true);     
        return;
    }

    if(!user->BasicInitComplete) {              // if not initialized then do so now
        user_write(user,"No BASIC program loaded\n\r");
        return;
    }
    user_push_input_buffer(user,"RUN\r");            // push the run command onto the input buffer
    user->ExitWhenDone = true;
    user->level=user_basic;                          // run the program
}

void user_basic_list_file(user_context_t *user,int start_line, int end_line) {
    if(!user->BasicInitComplete) {              // if not initialized then just return
        user_write(user,"No BASIC program loaded\n\r");
        return;
    }

    ListIt(user,start_line,end_line);                                // list the program to the console
}

void user_basic_save_file(user_context_t *user, char *filepath) {
    char filename[128];
    char buffer[256];
    FIL *ofile;

    if(!user->BasicInitComplete) {              // if not initialized then do nothing
        user_write(user,"Nothing to save\n\r");
        return;
    }

    strncpy(filename,filepath,sizeof(filename)-1);
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
        user->echo = false;
        ListIt(user,0,0);                                // save the listing to the output file
        f_close(ofile);
        free(ofile);
        ofile = NULL;
        user->echo = true;
        WarmStart(user);
        user_write(user,"BASIC program save complete\n\r");
    } else {
        user_write(user,"Failed to allocate file object for BASIC program save\n\r");
    }
}  


void user_basic_free_memory(user_context_t *user) {
    char buffer[256];
    if(!user->BasicInitComplete) {
        UserInitTinyBasic(user,tinybasicIL);
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
                           "CLS","FORCE","LOAD","SAVE","RENAME","MV","RM", "DEL","LIST","RUN","FREEMEM","LIBRARY","GET","KILL",
                           "DOC","SHARED","SHARE","CLEANUP",0};
enum {CMD_WHO, CMD_FREE, CMD_HELP, CMD_QUIT, CMD_BASIC, CMD_DIR, CMD_LS, CMD_MKDIR, CMD_RMDIR, CMD_CAT, CMD_TYPE, CMD_SEND, CMD_BROADCAST, CMD_CLS,
     CMD_FORCE, CMD_LOAD, CMD_SAVE, CMD_RENAME,CMD_MV,CMD_RM,CMD_DEL,CMD_LIST,CMD_RUN,CMD_FREEMEM,CMD_LIBRARY, CMD_GET, CMD_KILL,
     CMD_DOC, CMD_SHARED, CMD_SHARE, CMD_CLEANUP, CMD_UNKNOWN};

int lookup_shell_command(const char *cmd) {
    for (int i = 0; shellcmds[i] != 0; i++) {
        if (strncasecmp(cmd, shellcmds[i], strlen(cmd)) == 0) {
            return i;
        }
    }
    return CMD_UNKNOWN;
}

void userShell(user_context_t * user) {
    char cmd[20],parm1[64],parm2[64];
    int cmdindex,parm_count;
    char buffer[256];

    // later this will be a basic program
    user_get_line(user,buffer,sizeof(buffer));
    strtrim(buffer);
    parm1[0]='\0'; parm2[0]='\0';
    parm_count = sscanf(buffer,"%19s %63s %63s",cmd,parm1,parm2);
    //printf("Shell command parms(%d): cmd(%s) parm1(%s) parm2(%s)\n",parm_count,cmd,parm1,parm2);
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
            user_directory_listing(user,parm1);
            break;
        case CMD_MKDIR:
            user_create_dir(user,parm1);
            break;
        case CMD_RMDIR:
        case CMD_RM:
        case CMD_DEL:
            user_delete_dir(user,parm1);
            break;
        case CMD_CAT:
        case CMD_TYPE:
            user_type_file(user,parm1,false);
            break;
        case CMD_SEND:
            user_send_message(user,buffer,sizeof(buffer));
            break;
        case CMD_BROADCAST:
            user_broadcast_message(user,buffer,sizeof(buffer));
            break;
        case CMD_CLS:
            user_write(user,"\e[2J\e[H"); // clear screen
            break;
        case CMD_FORCE:
            user_force_user(user,parm1);
            break;
        case CMD_LOAD:
            user_basic_load_file(user,parm1,false);
            break; 
        case CMD_SAVE:
            user_basic_save_file(user,parm1);
            break;

        case CMD_RENAME:
        case CMD_MV:
            user_rename_file(user,parm1,parm2);
            break;

        case CMD_LIST:
            if(parm1[0]) {
                int startline = atoi(parm1);
                int endline = 0;
                if(parm2[0]) {
                    endline = atoi(parm2);
                }
                user_basic_list_file(user,startline,endline);
            } else {    
                user_basic_list_file(user,0,0);
            }
            break;

        case CMD_RUN:
            user_basic_run_file(user,parm1);
            break;
        case CMD_FREEMEM:
            user_basic_free_memory(user);
            break;
        case CMD_LIBRARY:
            user_display_directory(user,"/library",parm1);
            break;
        case CMD_GET:
            user_get_library_entry(user,parm1);
            break;
        case CMD_KILL:
            user_kill_task(user,parm1);
            break;
        case CMD_DOC:              // read/list a document file
            user_display_directory(user,"/documents",parm1);
            break;
        case CMD_SHARED:           // list or load public shared files
            user_display_directory(user,"/shared",parm1);
            break;
        case CMD_SHARE:            // share a file to make it public
            user_share_file(user,parm1);
            break;
        case CMD_CLEANUP:
            if(!user->SystemUser) {
                user_write(user,"Cleanup command is only available to System Users\n\r");
                break;
            }
            user_cleanup_disconnected_sessions(user);
            break;
        default:
            user_unknown_command(user,buffer,sizeof(buffer));
            break;
    }
}
