# calcterm -- a VT100 terminal for the PicoCalc, over serial or telnet
# (SSH and an FTP client to follow).
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

# a Bluetooth keyboard (picocalc-extras' btkbd), alongside telnet's Wi-Fi; it
# needs the Pico SDK's newer BTstack, as picocalc-extras says
set(BTKBD_WITH_WIFI ON)
include(${MODULES}/picocalc-extras/bluetooth/bluetooth.cmake)

# Wi-Fi and TCP for telnet: picocalc-extras' net/ (with its lwipopts.h), which
# reads /ntp/ntp.cfg with the shell's own parser
include(${MODULES}/picocalc-extras/net/net.cmake)
picocalc_net(${MODULES}/picocalc-shell)
# SSH: libssh2 (a submodule, at its 1.11.1 tag) with the SDK's Mbed TLS
include(${MODULES}/picocalc-extras/ssh/ssh.cmake)
picocalc_ssh(${MODULES}/libssh2)

add_executable(calcterm
    src/main.c src/vt.c src/screen.c src/keys.c src/link.c src/menu.c
    src/settings.c src/shot.c src/termfont.c src/font4.c src/zmodem.c src/xfer.c ${DRV}/font-8x10.c
    src/telnet.c src/hosts.c
    ${DRV}/keyboard.c ${DRV}/lcd.c ${DRV}/southbridge.c
)
target_include_directories(calcterm PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${MODULES}/picocalc-text-starter
    ${DRV}
    ${MODULES}/picocalc-shell/include
    ${MODULES}/picocalc-shell/src/core)
pico_set_program_name(calcterm "calcterm")
pico_set_program_version(calcterm "0.6")
# which build this is, on the sign-on line and in the menu: the commit, and
# its date (so two builds of the same version can be told apart on the PicoCalc)
execute_process(COMMAND git log -1 --format=%h%x20%cs WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    OUTPUT_VARIABLE CALCTERM_BUILD OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if (NOT CALCTERM_BUILD)
    set(CALCTERM_BUILD "unknown")
endif()
target_compile_definitions(calcterm PRIVATE CALCTERM_BUILD="${CALCTERM_BUILD}")
# the Pico's UART0 is the terminal's line, so nothing else may print to it
pico_enable_stdio_uart(calcterm 0)
pico_enable_stdio_usb(calcterm 0)
target_link_libraries(calcterm picocalc_fs picocalc_net picocalc_ssh
    pico_stdlib hardware_gpio hardware_i2c hardware_spi hardware_pio hardware_uart
    hardware_watchdog hardware_powman
    picocalc_btkbd)				# a Bluetooth keyboard
# what the router lists calcterm as
target_compile_definitions(calcterm PRIVATE CYW43_HOST_NAME="calcterm")
pico_add_extra_outputs(calcterm)
