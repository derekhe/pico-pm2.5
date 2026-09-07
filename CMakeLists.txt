cmake_minimum_required(VERSION 3.20)

set(sdkVersion 2.3.1)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.1)
set(PICO_BOARD pico CACHE STRING "Board type")

option(PM25_HOST_TESTS "Build hardware-independent host tests" OFF)

if(PM25_HOST_TESTS)
    project(pico_pm25_host_tests LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    add_library(pm25_host_core
        src/core.cpp
        src/canvas.cpp
        src/ui.cpp
    )
    target_include_directories(pm25_host_core PUBLIC include generated)
    add_executable(pm25_tests tests/test_core.cpp)
    target_link_libraries(pm25_tests PRIVATE pm25_host_core)
    enable_testing()
    add_test(NAME pm25_core_tests COMMAND pm25_tests)
    return()
endif()

include(pico_sdk_import.cmake)

project(pico_pm25 VERSION 1.0.0 LANGUAGES C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

pico_sdk_init()

add_executable(pico_pm25
    src/main.cpp
    src/core.cpp
    src/canvas.cpp
    src/ui.cpp
    src/lcd.cpp
)
target_include_directories(pico_pm25 PRIVATE include generated)
target_compile_options(pico_pm25 PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>
)
target_compile_definitions(pico_pm25 PRIVATE
    PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS=0
)
target_link_libraries(pico_pm25 PRIVATE
    pico_stdlib
    hardware_dma
    hardware_pwm
    hardware_spi
    hardware_uart
)
pico_enable_stdio_uart(pico_pm25 0)
pico_enable_stdio_usb(pico_pm25 1)
pico_set_program_name(pico_pm25 "Pico PM2.5 Display")
pico_set_program_version(pico_pm25 "1.0.0")
pico_add_extra_outputs(pico_pm25)
