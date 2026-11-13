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
set(sdkVersion 2.3.1)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(tremo_device C CXX ASM)
option(TREMO_MQTT "Habilita Wi-Fi/MQTT na Pico W" ON)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

set(FREERTOS_KERNEL_PATH "${CMAKE_CURRENT_LIST_DIR}/../FreeRTOS-Kernel" CACHE PATH "Caminho do clone do FreeRTOS Kernel")
set(FREERTOS_CONFIG_FILE_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/include")
add_subdirectory("${FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/RP2040" freertos)

# Add executable. Default name is the project name, version 0.1

add_executable(tremo_device main.c
            src/rtos_hooks.c
            src/display_task.c
            src/input_task.c
            src/matrix.c
            src/ssd1306_i2c.c
            src/display.c
            src/mpu6050_i2c.c
            src/patterns.c
            src/pal.c
            src/prm.c
            src/rti.c
            src/mot.c
            src/monitoring.c
            src/controls.c
            src/evaluation.c
            src/telemetry.c
            src/communication.c
            src/session.c
            src/assessment.c
            src/app.c)

pico_set_program_name(tremo_device "tremo_device")
pico_set_program_version(tremo_device "0.1")

# Generate PIO header
pico_generate_pio_header(tremo_device ${CMAKE_CURRENT_LIST_DIR}/ws2818b.pio)

# Modify the below lines to enable/disable output over UART/USB
# GPIOs 0 e 1 estão reservados para o I2C0 dos MPUs.
pico_enable_stdio_uart(tremo_device 0)
pico_enable_stdio_usb(tremo_device 1)
# Limita a espera por um monitor USB lento durante a navegação.
target_compile_definitions(tremo_device PRIVATE PICO_STDIO_USB_STDOUT_TIMEOUT_US=1000 TREMO_FREERTOS_ENABLED=1)

# Add the standard library to the build
target_link_libraries(tremo_device
        pico_stdlib
        pico_rand
        FreeRTOS-Kernel-Heap4)

# Add the standard include files to the build
target_include_directories(tremo_device PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        include
)

# Add any user requested libraries
target_link_libraries(tremo_device 
        hardware_pio
        hardware_i2c
        hardware_adc
        hardware_clocks
        )

pico_add_extra_outputs(tremo_device)


if(TREMO_MQTT)
    target_compile_definitions(tremo_device PRIVATE TREMO_MQTT_ENABLED=1)
    set(TREMO_NETWORK_CONFIG_DIR "${CMAKE_CURRENT_LIST_DIR}/config" CACHE PATH "Diretorio da configuracao local de rede")
    target_include_directories(tremo_device PRIVATE ${TREMO_NETWORK_CONFIG_DIR} config)
    target_link_libraries(tremo_device pico_cyw43_arch_lwip_poll pico_lwip_mqtt pico_unique_id)
endif()
