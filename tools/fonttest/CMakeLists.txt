# fonttest -- the 4-pixel fonts on the PicoCalc, before calcterm uses them.
# Made from the repository's top level: cmake -B build-fonttest tools/fonttest
cmake_minimum_required(VERSION 3.13)
set(CMAKE_C_STANDARD 11)
set(PICO_BOARD pico2_w CACHE STRING "Board type")
include(${CMAKE_CURRENT_LIST_DIR}/../../pico_sdk_import.cmake)
project(fonttest C CXX ASM)
pico_sdk_init()
set(TOP ${CMAKE_CURRENT_LIST_DIR}/../..)
set(DRV ${TOP}/modules/picocalc-text-starter/drivers)
add_executable(fonttest main.c font4.c ${TOP}/src/termfont.c ${DRV}/font-8x10.c
    ${DRV}/keyboard.c ${DRV}/lcd.c ${DRV}/southbridge.c)
target_include_directories(fonttest PRIVATE ${CMAKE_CURRENT_LIST_DIR}
    ${TOP}/modules/picocalc-text-starter ${DRV} ${TOP}/modules/picocalc-shell/include)
pico_enable_stdio_uart(fonttest 0)
pico_enable_stdio_usb(fonttest 0)
target_link_libraries(fonttest pico_stdlib hardware_gpio hardware_i2c hardware_spi hardware_pio hardware_watchdog)
pico_add_extra_outputs(fonttest)
