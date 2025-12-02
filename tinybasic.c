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
#include "user_datatypes.h"
#include "ff.h"
#include "f_util.h"
#include "hw_config.h"
#include "sd_card.h"
#include "user.h"

extern char __StackLimit, __bss_end__;

//extern void StartTinyBasic(char * ILtext);
extern void UserInitTinyBasic(user_context_t *user ,char * ILtext);
extern void ConfigureTinyBasic();
extern void RunTinyBasic();
extern bool SwitchUser;
extern user_context_t *CurrentUser;
extern user_context_t *RootUser;            // Pointer to the root user
extern user_context_t *DebugUser;           // Pointer to the debug user

extern semaphore_t user_list_sema;          // protect the user list against bad things

void init_filesys(void);
void init_telnet_server(void);

// Root LED state
bool root_led_on = true;

// LED control function
void pico_set_led(bool led_on) {
   // Ask the wifi "driver" to set the GPIO on or off
   cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_on);
}

// core 1 main task
void core1_entry() {
    printf("Begin using core 1\n");
    sleep_ms(5000);
    while(true) {
        user_context_t *user = get_user_list();
        if(user->logged_in) {
            if (user->level == user_basic) {
                CurrentUser = user;
                RunTinyBasic();
            }
        }
    user = user->next;
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


void Console_Data_Available(void *param) {
    user_context_t *user = param;
    int value = getchar_timeout_us(0);
    user->pending_console_read++;
    user->lineIndex++;
    if (user->lineIndex == 256) user->lineIndex = 0;
    user->linebuffer[user->lineIndex] = (unsigned char)value;
    user->lineLength++;
}


int main()
{
    long counter = 0;
    stdio_init_all();

    sleep_ms(10000);  // wait for console to start
    sem_init(&user_list_sema, 1, 1);

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

    
    // init the filesyste,m
    init_filesys();

    // Clock example code - This example prints out the system and usb clock frequencies
    printf("System Clock Frequency is %d Hz\n", clock_get_hz(clk_sys));
    printf("USB Clock Frequency is %d Hz\n", clock_get_hz(clk_usb));
    // For more examples of clocks use see https://github.com/raspberrypi/pico-examples/tree/master/clocks

    init_telnet_server();
    RootUser = CurrentUser;

    // Timer example code - This example fires off the callback after 2000ms
    struct repeating_timer timer,time_slice;
    //add_alarm_in_ms(1000, alarm_callback, NULL, false);
    add_repeating_timer_ms(2000, alarm_callback, NULL, &timer);
    add_repeating_timer_ms(100, next_user_callback, NULL, &time_slice);

    // For more examples of timer use see https://github.com/raspberrypi/pico-examples/tree/master/timer

    DebugUser = init_debug_session(RootUser->state.server_pcb);
    add_user_to_list(DebugUser);

    printf("waiting for users to log in...\n");

    ConfigureTinyBasic();                      // Setup the IL program and DeCaps definitions

    stdio_set_chars_available_callback(Console_Data_Available, RootUser);

    UserInitTinyBasic(RootUser,(char *)0);     // init tiny basic for root user
    UserInitTinyBasic(DebugUser,(char *)0);    // init tiny basic for DebugUser user

 
   // multicore_launch_core1(core1_entry);

    
    while (true) {
        user_context_t *user = get_user_list();

        while(user){
            if(user->logged_in) {
                if(user->level == user_shell) {
                    if(user->state.recv_len > 0) {
                        userShell(user);
                    }
                } else {
                    if (user->level == user_basic) {
                        if(user->pending_console_read) {
                            char value = user->linebuffer[user->lineIndex];
                            if(user->SystemUser) {
                                putchar(value);
                                if(value == '\r') putchar('\n');
                            }
                            if(value == '\r') {
                                user->WaitingRead = io_complete;
                            } else {
                                user->WaitingRead = io_waiting;
                            }
                            user->pending_console_read--;
                        } else {
                            CurrentUser = user;
                            RunTinyBasic();
                        }
                    }
                }
            }
            user = user->next;
        }
        
        //sleep_ms(500);
    }
}
