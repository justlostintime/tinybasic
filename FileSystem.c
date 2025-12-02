
// file system manager
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

void init_filesys(void) {
/*
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
*/
// FatFs example code - This example writes "Hello, world!" to a file on the SD card
printf("Initializing SD card................................................................\n");
    sd_card_t *pSD = sd_get_by_num(0);

    FRESULT fr = f_mount(&pSD->fatfs, pSD->pcName, 1);
    if (FR_OK != fr) panic("f_mount error: %s (%d)\n", FRESULT_str(fr), fr);

    FIL fil;
    const char* const filename = "filename.txt";
    fr = f_unlink(filename);
    if (FR_OK != fr && FR_NO_FILE != fr)
        printf("f_unlink(%s) error: %s (%d)\n", filename, FRESULT_str(fr), fr);  

    fr = f_open(&fil, filename, FA_OPEN_APPEND | FA_WRITE);
    if (FR_OK != fr && FR_EXIST != fr)
        panic("f_open(%s) error: %s (%d)\n", filename, FRESULT_str(fr), fr);

    if (f_printf(&fil, "Hello, world!\n") < 0) {
        printf("f_printf failed\n");
    }
    fr = f_close(&fil);
    if (FR_OK != fr) {
        printf("f_close error: %s (%d)\n", FRESULT_str(fr), fr);
    }
    fr = f_open(&fil,filename, FA_READ);
    if (FR_OK != fr) {
        panic("f_open(%s) for read error: %s (%d)\n", filename, FRESULT_str(fr), fr);
    }

    char *read_buffer;
    uint  bytes_to_read = 512;
    uint  bytes_read;

    printf("File size: %d bytes\n", bytes_to_read);

    read_buffer = malloc(bytes_to_read + 1);

    if (read_buffer == NULL) {
        panic("Failed to allocate memory for reading file\n");  
    }
    fr = f_read(&fil, read_buffer,bytes_to_read-1, &bytes_read);
    if (FR_OK != fr) {
        panic("f_read error: %s (%d)\n", FRESULT_str(fr), fr);
    }
    read_buffer[bytes_read] = '\0'; // Ensure null termination
    printf("Read from file bytes:%d, Data: %s\n\r", bytes_read,read_buffer);
    free(read_buffer);
    fr = f_close(&fil);
    if (FR_OK != fr) {
        printf("f_close error: %s (%d)\n", FRESULT_str(fr), fr);
    }
    printf("File system operations complete. Unmounting SD card.........................\n");
    f_unmount(pSD->pcName);
}
