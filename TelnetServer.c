
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/timer.h"
#include "hardware/clocks.h"
#include "pico/cyw43_arch.h"

#include "tcp_interface.h"
#include "user_datatypes.h"
#include "ff.h"
#include "f_util.h"
#include "hw_config.h"
#include "sd_card.h"
#include "user.h"

int init_telnet_server(char *ssid, char *password)
{
// Initialise the Wi-Fi chip
    if (cyw43_arch_init()) {
        printf("Wi-Fi init failed\n");
        return -1;
        }
    //printf("Wi-Fi init succeeded\n");

    // Enable wifi station
    cyw43_arch_enable_sta_mode();

    //printf("Connecting to Wi-Fi...'%s','%s'\n",ssid,password);

    if (cyw43_arch_wifi_connect_timeout_ms(ssid, password, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("failed to connect.\n");
        return -1;
    } else {
    //    printf("Connected.\n");
        // Read the ip address in a human readable way
        uint8_t *ip_address = (uint8_t*)&(cyw43_state.netif[0].ip_addr.addr);
        printf("IP address %d.%d.%d.%d\n", ip_address[0], ip_address[1], ip_address[2], ip_address[3]);
    }

    // Start the root tcp connection
    user_context_t *user = tcp_server_init();
    if (!user) {
        printf("root TCP init failed\n");
    // } else {
    //    printf("Starting root TCP server\n");
    return 1;
    }

    if (!tcp_server_open(user)) {
        printf("root TCP server open failed\n");
    //   tcp_server_result(user, -1,"init_telnet_server : open failed");
    //} else {
    //    printf("root TCP server open succeeded\n");
    }
    return 0;
}