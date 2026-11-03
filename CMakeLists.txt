# calcterm -- a VT100 terminal for the PicoCalc, over serial for now
# (telnet, SSH and an FTP client to follow).
cmake_minimum_required(VERSION 3.13)
set(CMAKE_C_STANDARD 11)
set(PICO_BOARD pico2_w CACHE STRING "Board type")
include(pico_sdk_import.cmake)
project(calcterm C CXX ASM)
pico_sdk_init()

set(MODULES ${CMAKE_CURRENT_LIST_DIR}/modules)
set(DRV ${MODULES}/picocalc-text-starter/drivers)
include(${MODULES}/picocalc-extras/fatfs/fatfs.cmake)
picocalc_fatfs(${MODULES}/picocalc-text-starter)	# the SD card, for the settings

add_executable(calcterm
    src/main.c src/vt.c src/screen.c src/keys.c src/link.c src/menu.c
    src/settings.c src/shot.c src/termfont.c ${DRV}/font-8x10.c
    ${DRV}/keyboard.c ${DRV}/lcd.c ${DRV}/southbridge.c
)
target_include_directories(calcterm PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${MODULES}/picocalc-text-starter
    ${DRV}
    ${MODULES}/picocalc-shell/include)
pico_set_program_name(calcterm "calcterm")
pico_set_program_version(calcterm "0.1")
# the Pico's UART0 is the terminal's line, so nothing else may print to it
pico_enable_stdio_uart(calcterm 0)
pico_enable_stdio_usb(calcterm 0)
target_link_libraries(calcterm picocalc_fs
    pico_stdlib hardware_gpio hardware_i2c hardware_spi hardware_pio hardware_uart
    hardware_watchdog hardware_powman)
pico_add_extra_outputs(calcterm)
