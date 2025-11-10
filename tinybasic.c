#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/timer.h"
#include "hardware/clocks.h"
#include "pico/cyw43_arch.h"

#include "tcp_interface.h"

// SPI Defines
// We are going to use SPI 0, and allocate it to the following GPIO pins
// Pins can be changed, see the GPIO function select table in the datasheet for information on GPIO assignments
#define SPI_PORT spi0
#define PIN_MISO 16
#define PIN_CS   17
#define PIN_SCK  18
#define PIN_MOSI 19


bool root_led_on = true;

// LED control function
void pico_set_led(bool led_on) {
   // Ask the wifi "driver" to set the GPIO on or off
   cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_on);
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


int main()
{
    long counter = 0;
    stdio_init_all();
    sleep_ms(10000);
    printf("Waiting for console to start for testing\n");

    // Initialise the Wi-Fi chip
    if (cyw43_arch_init()) {
        printf("Wi-Fi init failed\n");
        return -1;
    }

    // SPI initialisation. This example will use SPI at 1MHz.
    spi_init(SPI_PORT, 1000*1000);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CS,   GPIO_FUNC_SIO);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    
    // Chip select is active-low, so we'll initialise it to a driven-high state
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);
    // For more examples of SPI use see https://github.com/raspberrypi/pico-examples/tree/master/spi

    // Clock example code - This example prints out the system and usb clock frequencies
    printf("System Clock Frequency is %d Hz\n", clock_get_hz(clk_sys));
    printf("USB Clock Frequency is %d Hz\n", clock_get_hz(clk_usb));
    // For more examples of clocks use see https://github.com/raspberrypi/pico-examples/tree/master/clocks

    // Enable wifi station
    cyw43_arch_enable_sta_mode();

    printf("Connecting to Wi-Fi...\n");
    if (cyw43_arch_wifi_connect_timeout_ms("Q!Waste:Slow", "MON:Cars#", CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("failed to connect.\n");
        return 1;
    } else {
        printf("Connected.\n");
        // Read the ip address in a human readable way
        uint8_t *ip_address = (uint8_t*)&(cyw43_state.netif[0].ip_addr.addr);
        printf("IP address %d.%d.%d.%d\n", ip_address[0], ip_address[1], ip_address[2], ip_address[3]);
    }

    // Start the tcp connection
    TCP_SERVER_T *state = tcp_server_init();
    if (!state) {
        printf("TCP init failed\n");
    } else {
        printf("Starting TCP server\n");
    }
    if (!tcp_server_open(state)) {
        printf("TCP server open failed\n");
        tcp_server_result(state, -1);
    } else {
        printf("TCP server open succeeded\n");
    }

    // Timer example code - This example fires off the callback after 2000ms
    struct repeating_timer timer;
    //add_alarm_in_ms(1000, alarm_callback, NULL, false);
    add_repeating_timer_ms(2000, alarm_callback, NULL, &timer);
    // For more examples of timer use see https://github.com/raspberrypi/pico-examples/tree/master/timer

    while (true) {
        //printf("Hello, world! %d\n", counter++);
        sleep_ms(4000);
    }
}
