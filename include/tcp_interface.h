#ifndef TCP_INTERFACE_H
#define TCP_INTERFACE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "lwip/pbuf.h"
#include "lwip/tcp.h"

#include "tcp_datatypes.h"
#include "user.h"


/*
 * tcp_interface.h
 *
 * Minimal header describing the TCP server functions used by picow_tcp_server.c
 * and consumers. Implementations are expected to be in picow_tcp_server.c.
 *
 * All functions are non-blocking unless explicitly documented.
 */

/* Generic callbacks */
typedef void (*tcp_recv_cb_t)(const uint8_t *data, size_t len);
typedef void (*tcp_conn_cb_t)(bool connected);

/* Initialize the TCP server subsystem. Must be called before other functions. */
extern  user_context_t * tcp_server_init(void);

err_t tcp_server_close(void *arg);
err_t tcp_close_client(user_context_t *user );
err_t tcp_close_client_by_pcb(struct tcp_pcb *tpcb );
// extern  err_t tcp_server_result(void *arg, int status, char * msg);
err_t tcp_server_sent(void *arg, struct tcp_pcb *tpcb, u16_t len);
err_t tcp_server_send_data(void *arg, char * msg, int length);
err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p);
err_t tcp_server_poll(void *arg, struct tcp_pcb *tpcb);
void tcp_server_err(void *arg, err_t err);
err_t tcp_server_accept(void *arg, struct tcp_pcb *client_pcb, err_t err);
bool tcp_server_open(void *arg);
err_t tcp_server_send_message(void * arg, char * msg);
err_t tcp_server_send_msg_len(void * arg, char * msg, int len);
err_t tcp_server_flush(void * arg);

#ifdef __cplusplus
}
#endif

#endif /* TCP_INTERFACE_H */