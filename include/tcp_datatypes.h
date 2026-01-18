/*                   GNU AFFERO GENERAL PUBLIC LICENSE
                       Version 3, 19 November 2007

 Copyright (C) 2007 Free Software Foundation, Inc. <https://fsf.org/>
 Everyone is permitted to copy and distribute verbatim copies
 of this license document, but changing it is not allowed. */
#ifndef TCP_DATATYPES_H
#define TCP_DATATYPES_H
#include "lwip/tcp.h"
#define TCP_PORT 65432                     // Port number for the TCP server
#define DEBUG_printf(fmt,...) (void)0
//#define DEBUG_printf printf
#define BUF_SIZE 256
#define TEST_ITERATIONS 10
#define POLL_TIME_S 5

typedef struct TCP_SERVER_T_ {
    struct tcp_pcb *server_pcb;
    struct tcp_pcb *client_pcb;
    bool complete;
    uint8_t buffer_sent[BUF_SIZE];
    uint8_t buffer_recv[BUF_SIZE];
    int sent_len;
    int recv_len;
    int run_count;
    err_t last_err;
} TCP_SERVER_T;

#endif /* TCP_DATATYPES_H */