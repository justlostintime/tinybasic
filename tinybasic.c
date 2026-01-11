#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/timer.h"
#include "hardware/clocks.h"
#include "pico/cyw43_arch.h"
#include "pico/multicore.h"
#include "pico/sync.h"
#include <malloc.h>

#include "tcp_interface.h"
#include "telnetserver.h"
#include "user_datatypes.h"
#include "ff.h"
#include "f_util.h"
#include "hw_config.h"
#include "sd_card.h"
#include "user_datatypes.h"
#include "user.h"
#include "FileSystem.h"
#include "interpreter.h"
#include "debug_user.h"
#include "terminal.h"

#define multicore_basic 1

// define the length of time basic is given before giving up control
#define TIME_SLICE 100

extern char __StackLimit, __bss_end__;

extern bool SwitchUser;
extern user_context_t *NewUsers;            // new users to be added to user list
extern user_context_t *RootUser;            // Pointer to the root user
extern user_context_t *DebugUser;           // Pointer to the debug user        
extern queue_t basic_queue;                  // core 1 running basic programs

extern semaphore_t user_list_sema;          // protect the user list against bad things
extern semaphore_t new_user_list_sema;      // protect the user list against bad things

uint64_t Process_time_core0 = 0;            // the total process time used by core 0 ms
uint64_t Process_time_core1 = 0;            // the total process time used by core 1 ms
char *tinybasicIL = NULL;                   // pointer to the tinybasic IL code

const char * Greeting = "Welcome to TinyBasic 1.0 Timeshare server\n\rlogon format 'name:password'\n\rDO NOT ENTER ANY PRIVATE OR IDENTIFIABLE INFORMATION!!\n\rLog in : ";
const char * UserLogin = "Log in : ";
const char * UserPrompt = " > ";
bool display_who = false;

user_context_t *core1_user;                 // current shell user
user_context_t *core0_user;                 // current basic user

// Root LED state
bool root_led_on = true;


// LED control function
void pico_set_led(bool led_on) {
   // Ask the wifi "driver" to set the GPIO on or off
   cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_on);
}

void user_being_removed(user_context_t *user) {
    debugger_message(user,"logging Off");
    tcp_close_client(user);
    if(!user->persist) {
        remove_user_from_list(user);
        delete_user_context(user);
    } else {
        user->level = user_new_connect;
        if(user->i_Core != NULL) { free(user->i_Core); user->i_Core = NULL;}
        user->state.client_pcb = NULL;
    }
}   

void Core_1_dipatcher() {

    user_context_t *user;
    active_fifo_t fifo_entry;
    uint64_t timerStart;

    while(true) {
        queue_remove_blocking(&basic_queue, &fifo_entry);
        if(fifo_entry.user->WaitingRead==io_waiting) {          // because of timing issues with other core
           queue_add_blocking(&basic_queue, &fifo_entry);       // go back and wait for nexted queued program
           continue;
        }

        user = fifo_entry.user;
        core1_user = user;
        timerStart = get_absolute_time();

        Interp(user);                                           // go do it

        core1_user = NULL;
        
        uint64_t total = to_ms_since_boot(get_absolute_time()-timerStart);
        user->active_time_used += total;
        Process_time_core1 += total;

        switch(user->level) {

            case user_shell: // we get here when basic exits
                user->level = user_needs_prompt;
                user->running = false;
                break;

            case user_basic:  // requeue if busy, otherwise just continu
                queue_add_blocking(&basic_queue, &fifo_entry);
        }
    }
}
   
void main_user_loop() {
    uint64_t timerStart;
    active_fifo_t fifo_entry;
    while(true) {
        user_context_t *user;
        if(NewUsers){
            //printf("Transfering user(s) from waiting to active:\n");
            while(NewUsers) {
                sleep_ms(1);
                user = get_next_waiting();
                if(user == NULL) break;
                add_user_to_list(user);
            //    printf("%15s uid(%8X) %s\n",(user->username[0] == '\0' ? "New User": user->username) ,
            //              user->state.client_pcb,(user->SystemUser ? "System User" : "Normal User"));
            }   
            //printf(" : Transfer Complete\n");
        }

        user = get_user_list();

        while(user){

            if(user->display_who) {         // always immediate response
                user_who(user);
                user->display_who = false;
            }

            switch(user->level) {
                case user_new_connect:
                    if(user->state.client_pcb) {
                        user_write(user,(char *)Greeting);
                        user->level = user_wait_loggin;         // bump to wait login
                        user->WaitingRead = io_waiting;
                    }
                    break;

                case user_wait_loggin:
                    {
                        user_context_t *new_user;
                        if(user->WaitingRead == io_complete) {      // we have something to proceess
                           // printf("Login attempt from client %s port %d id %8X\n",
                           //        ip4addr_ntoa(&user->state.client_pcb->remote_ip),
                           //        user->state.client_pcb->remote_port,
                           //        user->state.client_pcb);
                            new_user = login_user(user);
                            if(new_user)  {
                                debugger_message(new_user,"Logged in");
                                user = new_user;
                                user->level = user_shell;           // bump to shell available
                                if(!user->SystemUser) {
                                    // create home directory for user if needed
                                    if(!user_create_home_directory(user)) {
                                        char buffer[128];
                                        snprintf(buffer,sizeof(buffer),"Failed to create home directory for user %s\n\r",user->username);
                                        user_write(user,buffer);
                                        user->level=user_removed;
                                    }
                                }

                                terminal_init(user,0,0);                            // get the initial size of the terminal
                                user_write(user,(char *)UserPrompt);

                            } else {
                                user_write(user,(char *)UserLogin);
                            }
                            user->WaitingRead = io_waiting;
                        }
                    }
                    break;

                case user_needs_prompt:                   // this is set when the user is leaving the basic interpreter
                    user_write(user,(char *)UserPrompt);
                    user->WaitingRead = io_waiting;
                    user->level = user_shell;
                    break;

                case user_shell:
                    if(user->WaitingRead == io_complete) { // we have something to proceess
                        timerStart = get_absolute_time();
                        core0_user = user;
                        userShell(user);
                        core0_user = NULL;
                        uint64_t total = (to_ms_since_boot(get_absolute_time() - timerStart));
                        user->active_time_used += total;
                        Process_time_core0 += total;

                        if (user->level == user_shell) {
                            user_write(user,(char *)UserPrompt);
                            user->WaitingRead = io_waiting;
                        }
                    }
                    break;

                case user_basic:
                        if(user->running) {
                            break;
                        } else { 
                            if(!user->BasicInitComplete) UserInitTinyBasic(user,tinybasicIL);
                            user->running = true;
                            fifo_entry.user = user;
                            // printf("Enqueue  not waiting or running");
                            queue_add_blocking(&basic_queue,&fifo_entry);
                        }
                    break;

                case user_removed:    // when a user is to be removed and the connection closed
                    user_being_removed(user);
                    user = get_user_list();
                    continue;

                default:
                   debugger_message(user,"Unknown User states");
                   user->level = user_removed;
            }
            user = user->next;
        }
    }
}

//int64_t alarm_callback(alarm_id_t id, void *user_data) {
bool alarm_callback(struct repeating_timer *t) {
    // Put your timeout handler code in here
    //printf("Alarm fired!");
    // Turn the led on or off
    if (root_led_on == true){
        root_led_on = false;
       // printf(" LED OFF\n");
    } else {
        root_led_on = true;
      //  printf(" LED ON\n");
    }
    pico_set_led(root_led_on);
    return true; // Return true to keep repeating
}

// this timer is the timer used to swicth user instances of the interpreter
bool next_user_callback(struct repeating_timer *t) {
    SwitchUser = true;
}

// called when ever there is console data available
// each byte is placed into the user's line buffer

void Console_Data_Available(void *param) {
    user_context_t *user = param;

    int16_t value = getchar_timeout_us(0);
    if(value == PICO_ERROR_TIMEOUT) {
        //printf("key timeout\n\r");
        return;
    }

    /* if(value == '\b' || value == 0x7F) {                    // backspace or delete
        if(!user_remove_char_from_buffer(user)) return;     // nothing to do if nothing in the buffer
        user_write(user,"\b \b");                           // erase the character on the console
        
    } else {
        //printf("got %2X\n\r",value);

        if((value >= 32 && value <= 126) || value == '\r') { // printable characters only
            // putchar(value);          // echo to the terminal
            // if(value == '\r') putchar( '\n');
        }
    */
        user_add_char_to_input_buffer(user,value);          // we must do something now for basic to work;
    // }
}

int main() {
    FRESULT fr ;
    FIL fil;

    long counter = 0;
    //set_sys_clock_khz(200000, true);

    sem_init(&user_list_sema, 1, 1);
    sem_init(&new_user_list_sema, 1, 1);

    stdio_init_all();

    // set up the active shell queue for core zero and the active basic queue for core 1
    // queue_init(&shell_queue,sizeof(active_fifo_t),QueueLength);
    queue_init(&basic_queue,sizeof(active_fifo_t),QueueLength);

    sleep_ms(5000);  // wait for console to start

/*
    struct mallinfo m = mallinfo();
    uint32_t total_heap_size = &__StackLimit  - &__bss_end__; // adjust if necessary
    uint32_t free_sram = total_heap_size - m.uordblks;
    printf("Base Mem used %u, Heap info: total %u, used %u, free %u\n", 512*1024 - total_heap_size, total_heap_size, m.uordblks, free_sram);
 
   
    printf("Waiting for console to start for testing\n");
    extern char __flash_binary_start;  // defined in linker script
    extern char __flash_binary_end;    // defined in linker script
    uintptr_t start = (uintptr_t) &__flash_binary_start;
    uintptr_t end = (uintptr_t) &__flash_binary_end;
    printf("Binary starts at %08x and ends at %08x, size is %08x\n", start, end, end-start);
*/
    
    // init the filesyste,m
    init_filesys();
    
    char *buffer = malloc(1024);
    char ssid_string[32];
    char password_string[32];

    if(buffer) {
        //printf("Allocated 1K for file system test at %08X\n",buffer);
        fr = f_open(&fil,"/system/wifi.config", FA_READ);
        if (FR_OK != fr) {
            printf("f_open(wifi.config) for read error: %s (%d)\n", FRESULT_str(fr), fr);
        } else {
            UINT bytes_read;
            fr = f_read(&fil, buffer,1023, &bytes_read);
            if (FR_OK != fr) {
            printf("f_read error: %s (%d)\n", FRESULT_str(fr), fr);
            } else {
                //printf("Read from file bytes:%d, Data: %s\n\r", bytes_read,buffer);
                buffer[bytes_read] = '\0'; // Ensure null termination+

                sscanf(buffer,"%31s %31s",ssid_string,password_string);
                //printf("Read from wifi.config bytes:%d, SSID: %s, Password: %s\n\r", bytes_read,ssid_string,password_string);
       
                fr = f_close(&fil);
                if (FR_OK != fr) {
                    printf("f_close error: %s (%d)\n", FRESULT_str(fr), fr);
                }
            }
        }
        free(buffer);
    } else {
        printf("Failed to allocate buffer for reading wifi config\n");
    }


    // Clock example code - This example prints out the system and usb clock frequencies
    // printf("System Clock Frequency is %d Hz\n", clock_get_hz(clk_sys));
    // printf("USB Clock Frequency is %d Hz\n", clock_get_hz(clk_usb));
    // For more examples of clocks use see https://github.com/raspberrypi/pico-examples/tree/master/clocks
    
    init_telnet_server(ssid_string,password_string);  // user and pwd in /system/wifi.config

    // Timer example code - This example fires off the callback after 2000ms
    struct repeating_timer timer,time_slice;
    //add_alarm_in_ms(1000, alarm_callback, NULL, false);
    add_repeating_timer_ms(2000, alarm_callback, NULL, &timer);
    add_repeating_timer_ms(TIME_SLICE, next_user_callback, NULL, &time_slice);

    // For more examples of timer use see https://github.com/raspberrypi/pico-examples/tree/master/timer

    DebugUser = init_debug_session(RootUser->state.server_pcb);
    add_user_to_waiting(DebugUser);

    //printf("waiting for users to log in...\n");
    tinybasicIL = basic_language_config(RootUser);          // setup the basic language configuration
    ConfigureTinyBasic();                // Setup the IL program and DeCaps definitions

    stdio_set_chars_available_callback(Console_Data_Available, RootUser);

#ifdef multicore_basic
        multicore_launch_core1(Core_1_dipatcher);
        user_write(RootUser,(char *)UserPrompt);   // prompt for root user
        main_user_loop();
#else
        user_write(RootUser,(char *)UserPrompt);   // prompt for root user
        main_user_loop();
#endif
}
