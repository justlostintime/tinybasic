#ifndef TCP_DATATYPES_H
#define TCP_DATATYPES_H

#define TCP_PORT 4242
#define DEBUG_printf printf
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