
// file system manager
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/timer.h"
#include "hardware/clocks.h"
#include "pico/cyw43_arch.h"

#include "tcp_interface.h"
#include "user.h"
#include "FileSystem.h"


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
//printf("Initializing SD card................................................................\n");
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

    //printf("File size: %d bytes\n", bytes_to_read);

    read_buffer = malloc(bytes_to_read + 1);

    if (read_buffer == NULL) {
        printf("Failed to allocate memory for reading test file\n\r");  
    }
    fr = f_read(&fil, read_buffer,bytes_to_read-1, &bytes_read);
    if (FR_OK != fr) {
        printf("f_read on test file error: %s (%d)\n\r", FRESULT_str(fr), fr);
    }
    read_buffer[bytes_read] = '\0'; // Ensure null termination
    //printf("Read from file bytes:%d, Data: %s\n\r", bytes_read,read_buffer);
    free(read_buffer);
    fr = f_close(&fil);
    if (FR_OK != fr) {
        printf("f_close error: %s (%d)\n\r", FRESULT_str(fr), fr);
    }
    // printf("File system operations complete. Unmounting SD card.........................\n");
    // f_unmount(pSD->pcName);
}

int close_filesys(void) {
    sd_card_t *pSD = sd_get_by_num(0);
    FRESULT fr = f_unmount(pSD->pcName); // Unmount the filesystem
    if (FR_OK != fr) {
        printf("f_unmount(NULL) error: %s (%d)\n\r", FRESULT_str(fr), fr);
        return -1;
    }
    return 0;
}

bool user_create_home_directory(user_context_t *user) {
    char buffer[128];
    char path[128];

    // Create home directory for user
    if (user->SystemUser) {
        // system user does not get a home directory
        return true;
    }

    if(user->username[0] == '\0') {
        snprintf(buffer,sizeof(buffer),"Invalid username '%s' for home directory creation\n\r",user->username);
        return false;
    }

    snprintf(path,sizeof(path),"/user/%s-%s",user->username,user->password);
    
    FRESULT res = f_mkdir(path);
    if (res == FR_OK || res == FR_EXIST) {
        return true;
    } else {
        snprintf(buffer,sizeof(buffer),"Failed to create directory \"%s\". (%u)\n\r", path, res);
        user_write(user,buffer);
        return false;
    }
}   

FRESULT user_create_directory(user_context_t *user, char *dirname) {
    char buffer[128];
    char path[128];
    
    if(user->username[0] == '\0') {
        snprintf(buffer,sizeof(buffer),"No user Name provided!!!! Cannot create directory \"%s\".\n\r",dirname);
        return FR_INVALID_NAME;
    }
    strncpy(path,dirname,sizeof(path));
    user_set_file_path(user,path,sizeof(path));

    FRESULT res = f_mkdir(path);
    if (res == FR_OK || res == FR_EXIST) {
        snprintf(buffer,sizeof(buffer),"Create directory \"%s\" for %s in \"%s\". (%u)\n\r",dirname,user->username, path, res);
        user_write(user,buffer);
        return FR_OK;
    } else {
        snprintf(buffer,sizeof(buffer),"Failed to create directory\"%s\" in \"%s\". (%u)\n\r",dirname, path, res);
        user_write(user,buffer);
        return res;
    }
}

FRESULT user_remove_directory(user_context_t *user, char *dirname) {
    char buffer[128];
    char path[128];
    
    if(user->username[0] == '\0') {
        snprintf(buffer,sizeof(buffer),"No user Name provided!!!! Cannot remove directory \"%s\".\n\r",dirname);
        return FR_INVALID_NAME;
    }

    strncpy(path,dirname,sizeof(path));
    user_set_file_path(user,path,sizeof(path));

    FRESULT res = f_unlink(path);
    if (res == FR_OK) {
        snprintf(buffer,sizeof(buffer),"Removed  \"%s\" for %s in \"%s\". (%u)\n\r",dirname,user->username, path, res);
        user_write(user,buffer);
        return FR_OK;
    } else if(res == FR_DENIED) {
            snprintf(buffer,sizeof(buffer),"Directory \"%s\" not empty for %s in \"%s\". (%u)\n\r",dirname,user->username, path, res);
            user_write(user,buffer);
            return res;
    } else if(res == FR_NO_FILE) {
            snprintf(buffer,sizeof(buffer),"File \"%s\" does not exist for %s in \"%s\". (%u)\n\r",dirname,user->username, path, res);
            user_write(user,buffer);
            return res;
    } else {
        snprintf(buffer,sizeof(buffer),"Unable to delete \"%s\" in \"%s\". (%u)\n\r",dirname, path, res);
        user_write(user,buffer);
        return res;
    }
}

FRESULT user_rename_user_file(user_context_t *user, char *sourcefile, char *destfile) {
    char buffer[128];
    char sourcepath[128];
    char destpath[128];
    
    if(user->username[0] == '\0') {
        snprintf(buffer,sizeof(buffer),"No user Name provided!!!! Cannot rename file \"%s\" to \"%s\".\n\r",sourcefile,destfile);
        return FR_INVALID_NAME;
    }

    strncpy(sourcepath,sourcefile,sizeof(sourcepath));
    user_set_file_path(user,sourcepath,sizeof(sourcepath));

    strncpy(destpath,destfile,sizeof(destpath));
    user_set_file_path(user,destpath,sizeof(destpath));

    FRESULT res = f_rename(sourcepath,destpath);
    if (res == FR_OK) {
        snprintf(buffer,sizeof(buffer),"Renamed file \"%s\" to \"%s\" for %s. (%u)\n\r",sourcefile,destfile,user->username, res);
        user_write(user,buffer);
        return FR_OK;
    } else {
        snprintf(buffer,sizeof(buffer),"Failed to rename file \"%s\" to \"%s\" for %s. (%u)\n\r",sourcefile,destfile,user->username, res);
        user_write(user,buffer);
        return res;
    }
}   

FRESULT display_directory(user_context_t *user, char *cmdline, int cmdlen, bool as_root){
   
    FRESULT res;
    DIR dir;
    FILINFO fno;
    int nfile, ndir;
    char path[128]; 
    char dirname[128];
    dirname[0]='\0';
    path[0]='\0';
    char buffer[128];

    sscanf(cmdline,"%s %s",buffer,dirname);
    strncpy(path,dirname,sizeof(path));
    if(!as_root) user_set_file_path(user,path,sizeof(path));

    snprintf(buffer,sizeof(buffer),"Directory listing for %s\n\r",path);
    user_write(user,buffer);

    // Open the directory
    res = f_opendir(&dir, path); 
    if (res == FR_OK) {
        nfile = ndir = 0;
        for (;;) {
            // Read a directory item
            res = f_readdir(&dir, &fno); 
            // Break on error or end of dir
            if (fno.fname[0] == 0) break; 
            
            if (fno.fattrib & AM_DIR) { 
                // It is a directory
                snprintf(buffer,sizeof(buffer)," <DIR> %s\n\r", fno.fname);
                user_write(user,buffer);
                ndir++;
            } else { 
                // It is a file
                snprintf(buffer,sizeof(buffer),"%10u %s\n\r", (unsigned int)fno.fsize, fno.fname);
                user_write(user,buffer);
                nfile++;
            }
        }
        // Close the directory
        f_closedir(&dir); 
        snprintf(buffer,sizeof(buffer),"%d dirs, %d files.\n\r", ndir, nfile);
        user_write(user,buffer);
    } else {
        snprintf(buffer, sizeof(buffer), "Failed to open \"%s\". (%u)\n\r", path, res);
        user_write(user,buffer);
    }

    return FR_OK;
}
#define BUFFER_SIZE 512

FRESULT Copy_file(user_context_t *user, const char* source_file, const char* dest_file){
    char prbuf[128];
    FRESULT fr;                 // Result code
    FIL src_file, dst_file;     // File objects
    UINT br, bw;                // Bytes read, Bytes written
    BYTE buffer[BUFFER_SIZE];   // Data buffer

    // 1. Open the source file for reading
    fr = f_open(&src_file, source_file, FA_READ);
    if (fr != FR_OK) {
        snprintf(prbuf,sizeof(prbuf),"Failed to open %s: %s %d\n\r", source_file,FRESULT_str(fr), fr);
        user_write(user,prbuf);
        return fr;
    }

    // 2. Open/create the destination file for writing
    // FA_CREATE_ALWAYS ensures a new file is created, overwriting if it exists
    fr = f_open(&dst_file, dest_file, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) {
        snprintf(prbuf,sizeof(prbuf),"Failed to create %s: %s %d\n\r", dest_file,FRESULT_str(fr), fr);
        user_write(user,prbuf);
        f_close(&src_file); // Close the source file before returning
        return fr;
    }

    // 3. Copy data from source to destination file
    
    snprintf(prbuf,sizeof(prbuf),"Copying %s to %s...\n\r", source_file, dest_file);
    user_write(user,prbuf);

    for (;;) {
        // Read a chunk of data from the source file
        fr = f_read(&src_file, buffer, sizeof(buffer), &br);
        if (fr != FR_OK || br == 0) break; // Error or end of file

        // Write the data to the destination file
        fr = f_write(&dst_file, buffer, br, &bw);
        if (fr != FR_OK || bw < br) break; // Error or disk full
    }

    user_write(user,"Get has completed.\n\r");
     // 4. Close both files
    f_close(&dst_file);
    f_close(&src_file);

    if (fr != FR_OK) {
        snprintf(prbuf,sizeof(prbuf),"File copy failed with result code: %s %d\n",FRESULT_str(fr), fr);
        user_write(user,prbuf);
    }

    return fr;
}
