/**
 * Copyright (c) 2022 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <string.h>
#include <stdlib.h>
#include <malloc.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "lwip/pbuf.h"
#include "lwip/tcp.h"

#include "tcp_datatypes.h"
#include "user.h"

extern char __StackLimit, __bss_end__;
extern struct tcp_pcb *tcp_active_pcbs;

#define Initial_Root_State  user_shell

user_context_t * tcp_server_init(void) {
    user_context_t *user  =  create_user_context(NULL, NULL,true);
    if (!user) {
        DEBUG_printf("failed to allocate root\n");
        return NULL;
    }
    return user;
}

int tcp_connection_count() {
        int count = 0;
        struct tcp_pcb *pcb;

        // Iterate through the active TCP PCBs
        for (pcb = tcp_active_pcbs; pcb != NULL; pcb = pcb->next) {
            if (pcb->state == ESTABLISHED) {
                count++;
            }
        }

        DEBUG_printf("*******************************************Active TCP connections: %d\n", count);

        return count;
    }

err_t tcp_close_client_by_pcb(struct tcp_pcb *tpcb ) {
    err_t err = ERR_OK;
    if (tpcb != NULL) {
        tcp_arg(tpcb, NULL);
        tcp_poll(tpcb, NULL, 0);
        tcp_sent(tpcb, NULL);
        tcp_recv(tpcb, NULL);
        tcp_err(tpcb, NULL);
        err = tcp_close(tpcb);
        if (err != ERR_OK) {
            DEBUG_printf("close failed %d, calling abort\n", err);
            tcp_abort(tpcb);
            err = ERR_ABRT;
        }
    }
    return err;
}

err_t tcp_close_client(user_context_t *user ) {
    struct tcp_pcb *tpcb = user->state.client_pcb;
    err_t err = ERR_OK;
    err = tcp_close_client_by_pcb(tpcb);
    return err;
}

err_t tcp_server_close(void *arg) {
    TCP_SERVER_T *state = (TCP_SERVER_T*)arg;
    err_t err = ERR_OK;

    struct tcp_pcb *pcb;
    state->client_pcb = NULL;

    // Iterate through the active TCP PCBs, worry if not matching user context cnt later
    for (pcb = tcp_active_pcbs; pcb != NULL; pcb = pcb->next) {
        if (pcb->state == ESTABLISHED) {
            err = tcp_close_client_by_pcb(pcb);
        }
    }
    
    if (state->server_pcb) {
        tcp_arg(state->server_pcb, NULL);
        tcp_close(state->server_pcb);
        state->server_pcb = NULL;
    }

    return err;
}

err_t tcp_server_result(void *arg, int status, char *msg) {
    TCP_SERVER_T *state = (TCP_SERVER_T*)arg;
    if (status == 0) {
        DEBUG_printf("%s success\n",msg);
    } else {
        DEBUG_printf("%s failed %d\n",msg,  status);
    }
    state->complete = true;
    struct mallinfo m = mallinfo();
    uint32_t total_heap_size = &__StackLimit  - &__bss_end__; // adjust if necessary
    uint32_t free_sram = total_heap_size - m.uordblks;
    DEBUG_printf("Base Mem used %u, Heap info: total %u, used %u, free %u\n", 512*1024 - total_heap_size, total_heap_size, m.uordblks, free_sram);
    tcp_connection_count();
    return ERR_OK; // tcp_server_close(arg);
}

err_t tcp_server_sent(void *arg, struct tcp_pcb *tpcb, u16_t len) {
    user_context_t *user = (user_context_t*)arg;
    TCP_SERVER_T *state = &user->state;
    DEBUG_printf("tcp_server_sent %u for %6X\n", len, tpcb );
    state->sent_len += len;
    user->WaitingWrite = io_none;
    return ERR_OK;
}

// used to send a message to a client when at the level no state or user assigned
err_t tcp_server_sent_no_user(void *arg, struct tcp_pcb *tpcb, u16_t len) {
   //printf("tcb direct error message sent %d bytes",len);
   tcp_close(tpcb);
}

// Added version that send to pcb directly
err_t tcp_server_pcb_message(void * arg, char * msg) {
    struct tcp_pcb *tpcb = arg;

    int buflen = strlen(msg);

    if (buflen > BUF_SIZE-1){
        buflen = BUF_SIZE;
    }

    tcp_arg(tpcb,NULL);
    tcp_sent(tpcb, tcp_server_sent_no_user);

   // printf("tcp_server_send_data - Copied data to buffer\n");
    DEBUG_printf("TCB Writing %ld bytes to client %s port %d id %8X : %s\n", strlen(msg),
                 ip4addr_ntoa(&(tpcb->remote_ip)),tpcb->remote_port,tpcb,msg);

    cyw43_arch_lwip_check();

    err_t err = tcp_write(tpcb, msg, buflen, TCP_WRITE_FLAG_COPY);

    if (err != ERR_OK) {
        DEBUG_printf("Failed to write data %d to %s uid %d\n", err,ip4addr_ntoa(&(tpcb->remote_ip)),tpcb->remote_port);
    }
    return err;
}

// updated to accept null terminated string to send
err_t tcp_server_send_data(void *arg,char *senddata)
{
    user_context_t *user = (user_context_t*)arg;
    TCP_SERVER_T *state = &user->state;
    struct tcp_pcb *tpcb = user->SystemUser ? user->state.server_pcb : user->state.client_pcb;

    int buflen = strlen(senddata);
    //printf("tcp_server_send_data called to send %ld bytes to client %s port %d id %8X\n", buflen,
    //       ip4addr_ntoa(&(tpcb->remote_ip)),tpcb->remote_port,tpcb);

    if (buflen > BUF_SIZE-1){
        buflen = BUF_SIZE;
    }

    strncpy((char *)&state->buffer_sent, senddata, BUF_SIZE);
    state->buffer_sent[buflen] = '\0';
   // printf("tcp_server_send_data - Copied data to buffer\n");
    state->sent_len = 0;    

    DEBUG_printf("Writing %ld bytes to client %s port %d id %8X : %s\n", strlen((char *)&state->buffer_sent),
                 ip4addr_ntoa(&(tpcb->remote_ip)),tpcb->remote_port,tpcb,(char *)&state->buffer_sent);

    // this method is callback from lwIP, so cyw43_arch_lwip_begin is not required, however you
    // can use this method to cause an assertion in debug mode, if this method is called when
    // cyw43_arch_lwip_begin IS needed

    cyw43_arch_lwip_check();
    //printf("tcp_server_send_data - cyw43_arch_lwip_check() complete \n");

    err_t err = tcp_write(tpcb, state->buffer_sent, buflen, TCP_WRITE_FLAG_COPY);

    if (err != ERR_OK) {
        DEBUG_printf("Failed to write data %d to %s uid %d\n", err,ip4addr_ntoa(&(tpcb->remote_ip)),tpcb->remote_port);
    } else {
        user->WaitingWrite = io_waiting;
    }
    return err;
}

// Added version that requires use of the begin and end calls
err_t tcp_server_send_message(void * arg, char * msg) {
    err_t err;
    cyw43_arch_lwip_begin();
    err = tcp_server_send_data(arg,msg);
    cyw43_arch_lwip_end();
    return err;
}

err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    user_context_t *user = (user_context_t*)arg;
    TCP_SERVER_T *state = &user->state;

    if (!p) {
        DEBUG_printf("tcp_server_recv: remote closed %s uid %d error %d\n",ip4addr_ntoa(&(tpcb->remote_ip)),tpcb->remote_port,err);
        if(err == 0) {
            err = ERR_OK;
            DEBUG_printf("Closing connection to %s uid %d\n",ip4addr_ntoa(&(tpcb->remote_ip)),tpcb->remote_port);
            user->level=user_removed;
        } else {
            err = ERR_ABRT;
            DEBUG_printf("Aborting connection to %s uid %d\n",ip4addr_ntoa(&(tpcb->remote_ip)),tpcb->remote_port);
            user->level=user_removed;
        }

        return err;   // tcp_server_result(user, err, "tcp_server_recv: remote closed");
    }
    // this method is callback from lwIP, so cyw43_arch_lwip_begin is not required, however you
    // can use this method to cause an assertion in debug mode, if this method is called when
    // cyw43_arch_lwip_begin IS needed
    cyw43_arch_lwip_check();

    if (p->tot_len > 0) {
        DEBUG_printf("tcp_server_recv %d/%d err %d from %s port %d\n", p->tot_len, state->recv_len, err,ip4addr_ntoa(&(tpcb->remote_ip)),tpcb->remote_port);
        // Receive the buffer
        const uint16_t buffer_left = BUF_SIZE - state->recv_len - 1; // leave space for null terminator
        state->recv_len += pbuf_copy_partial(p, &state->buffer_recv + state->recv_len,p->tot_len > buffer_left ? buffer_left : p->tot_len, 0);
        state->buffer_recv[state->recv_len] = '\0'; // null terminate
        tcp_recved(tpcb, p->tot_len);
    }

    pbuf_free(p);

    DEBUG_printf("Received data from %s port %d id %8X len %d: %s\n",ip4addr_ntoa(&(tpcb->remote_ip)),tpcb->remote_port, 
                tpcb, state->recv_len, (char *)&state->buffer_recv);

    if (state->buffer_recv[ state->recv_len-1] == '\n') state->recv_len--;

    // everything now goes through the add character to console buffer
    for(int i = 0; i < state->recv_len; i++) {
            user_add_char_to_input_buffer(user,state->buffer_recv[i]);
    }

    DEBUG_printf("Basic Program Buffer Recieved : %s\n",user->linebuffer);
    state->recv_len = 0;
    return ERR_OK;
}

err_t tcp_server_poll(void *arg, struct tcp_pcb *tpcb) {
    user_context_t *user = (user_context_t*)arg;
    TCP_SERVER_T *state = &user->state;
    DEBUG_printf("tcp_server_poll_fn\n");
    return tcp_server_result(user, -1,"poll"); // no response is an error?
}

void tcp_server_err(void *arg, err_t err) {
    user_context_t *user = (user_context_t*)arg;
    TCP_SERVER_T *state = &user->state;
    if (err != ERR_ABRT) {
        DEBUG_printf("tcp_client_err_fn %d , client_pcb %8U\n", err, state->client_pcb);
        tcp_server_result(user, err, "tcp_server_err");
    }
}

err_t tcp_server_accept(void *arg, struct tcp_pcb *client_pcb, err_t err) {
    user_context_t *user = (user_context_t*)arg;       // in this case it is the server user root
    TCP_SERVER_T *state = &user->state;                // it is the root users state we see
    if (err != ERR_OK || client_pcb == NULL) {         // check if it just failed all together
        DEBUG_printf("Failure in accept error=%d pcb=%6X Last Error=%d\n",err,client_pcb,state->last_err);
        tcp_server_result(arg, err, "tcp_server_accept");
        return ERR_OK;
    }

    DEBUG_printf("Client connected %s : uid %d : pcbid : %8X\n",ip4addr_ntoa(&(client_pcb->remote_ip)),client_pcb->remote_port,client_pcb);
    
    // create a new user to use this pcb
    user_context_t * new_user = create_user_context(user->state.server_pcb, client_pcb, false);
    if (!new_user) {
        DEBUG_printf("Failed to create user context for %s uid %d\n",ip4addr_ntoa(&(client_pcb->remote_ip)),client_pcb->remote_port);
        tcp_server_pcb_message(client_pcb,"Looks like we are too busy to allow access right now, try again later\n\r");
        return ERR_OK;
    }

    struct mallinfo m = mallinfo();
    uint32_t total_heap_size = &__StackLimit  - &__bss_end__; // adjust if necessary
    uint32_t free_sram = total_heap_size - m.uordblks;
    DEBUG_printf("Base Mem used %u, Heap info: total %u, used %u, free %u\n", 512*1024 - total_heap_size, total_heap_size, m.uordblks, free_sram);

    if(add_user_to_waiting(new_user)) {
        DEBUG_printf("User context added to waiting list for %s uid %d id %8X\n",ip4addr_ntoa(&(client_pcb->remote_ip)),client_pcb->remote_port,client_pcb);
    } else {
        DEBUG_printf("Failed to add user context to waiting list for %s uid %d id %8H\n",ip4addr_ntoa(&(client_pcb->remote_ip)),client_pcb->remote_port,client_pcb);
        delete_user_context(new_user);
        tcp_close_client(user);
        return ERR_MEM;
    }   

    tcp_arg(client_pcb, new_user);
    tcp_sent(client_pcb, tcp_server_sent);
    tcp_recv(client_pcb, tcp_server_recv);
    //tcp_poll(client_pcb, tcp_server_poll, POLL_TIME_S * 2);
    tcp_err(client_pcb, tcp_server_err);
    DEBUG_printf(("Connection counter %d\n"), tcp_connection_count());
    // user_print_info();
    tcp_server_send_data(new_user,"Welcome to Pico timeshare\n\r");
    // cyw43_arch_lwip_end();
}

bool tcp_server_open(void *arg) {
    user_context_t *user = (user_context_t *)arg;
    TCP_SERVER_T *state = &user->state;
    DEBUG_printf("Starting root server at %s on port %u\n", ip4addr_ntoa(netif_ip4_addr(netif_list)), TCP_PORT);

    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb) {
        DEBUG_printf("failed to create root pcb\n");
        return false;
    }

    err_t err = tcp_bind(pcb, NULL, TCP_PORT);
    if (err) {
        DEBUG_printf("root failed to bind to port %u\n", TCP_PORT);
        return false;
    }

    state->server_pcb = tcp_listen_with_backlog_and_err(pcb, 1,&state->last_err);
    if (!state->server_pcb) {
        DEBUG_printf("failed to listen\n");
        if (pcb) {
            tcp_close(pcb);
        }
        return false;
    }

    strncpy(user->username,"root",sizeof(user->username)-1);
    user->logged_in = true;
    user->level = Initial_Root_State;
    
    if(add_user_to_waiting(user)) {
        DEBUG_printf("Root context added to active list id = %8X\n",state->server_pcb );
    } else {
        DEBUG_printf("Failed to add user context to active list for root\n");
        return ERR_MEM;
    }   

    tcp_arg(state->server_pcb, user);
    tcp_accept(state->server_pcb, tcp_server_accept);

    return true;
}

/*
void run_tcp_server_test(void) {
    TCP_SERVER_T *state = tcp_server_init();
    if (!state) {
        return;
    }
    if (!tcp_server_open(state)) {
        tcp_server_result(state, -1, "tcp_server_test: open failed");
        return;
    }
    while(!state->complete) {
        // the following #ifdef is only here so this same example can be used in multiple modes;
        // you do not need it in your code
#if PICO_CYW43_ARCH_POLL
        // if you are using pico_cyw43_arch_poll, then you must poll periodically from your
        // main loop (not from a timer) to check for Wi-Fi driver or lwIP work that needs to be done.
        cyw43_arch_poll();
        // you can poll as often as you like, however if you have nothing else to do you can
        // choose to sleep until either a specified time, or cyw43_arch_poll() has work to do:
        cyw43_arch_wait_for_work_until(make_timeout_time_ms(1000));
#else
        // if you are not using pico_cyw43_arch_poll, then WiFI driver and lwIP work
        // is done via interrupt in the background. This sleep is just an example of some (blocking)
        // work you might be doing.
        sleep_ms(1000);
#endif
    }
    free(state);
}

int tcp_test_main() {
    
    stdio_init_all();
    sleep_ms(10000); // wait for console
    printf("Starting TCP server test\n");

    if (cyw43_arch_init()) {
        printf("failed to initialise\n");
        return 1;
    }

    cyw43_arch_enable_sta_mode();

    printf("Connecting to Wi-Fi...\n");
    if (cyw43_arch_wifi_connect_timeout_ms("Q!Waste:Slow", "MON:Cars#", CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("failed to connect.\n");
        return 1;
    } else {
        printf("Connected.\n");
    }

    sleep_ms(2000); // wait a moment for IP address to settle

    run_tcp_server_test();
    cyw43_arch_deinit();
    return 0;
} 
    */