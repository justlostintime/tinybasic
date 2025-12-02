# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(tinybasic C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(tinybasic tinybasic.c picow_tcp_server.c user.c hw_config.c FileSystem.c TelnetServer.c 
               interpreter.c debug_user.c)
               
add_subdirectory(lib/no-OS-FatFS-SD-SPI-RPi-Pico/FatFs_SPI build)

pico_set_program_name(tinybasic "tinybasic")
pico_set_program_version(tinybasic "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(tinybasic 0)
pico_enable_stdio_usb(tinybasic 1)

# Add the standard library to the build
target_link_libraries(tinybasic
        pico_multicore
        pico_stdlib
        )

# Add the standard include files to the build
target_include_directories(tinybasic PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/include
        ${CMAKE_CURRENT_LIST_DIR}/lib/no-OS-FatFS-SD-SPI-RPi-Pico/FatFs_SPI/include
        ${CMAKE_CURRENT_LIST_DIR}/lib/no-OS-FatFS-SD-SPI-RPi-Pico/FatFs_SPI/sd_driver
        ${CMAKE_CURRENT_LIST_DIR}/lib/no-OS-FatFS-SD-SPI-RPi-Pico/FatFs_SPI/ff15/source
        
)

# Add any user requested libraries
target_link_libraries(tinybasic 
        hardware_spi
        hardware_timer
        hardware_clocks
        pico_cyw43_arch_lwip_threadsafe_background
        FatFs_SPI
        )

pico_add_extra_outputs(tinybasic)

