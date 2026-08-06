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

project(main C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

set(SDIO_RPI2_TARGET no-OS-FatFS-SD-SDIO-SPI-RPi-Pico)
add_subdirectory(lib/SDIO_RPI2/src)

get_target_property(SDIO_RPI2_INCLUDE_DIRS
    ${SDIO_RPI2_TARGET}
    INTERFACE_INCLUDE_DIRECTORIES
)

if(SDIO_RPI2_INCLUDE_DIRS)
    set_target_properties(
        ${SDIO_RPI2_TARGET}
        PROPERTIES
            INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${SDIO_RPI2_INCLUDE_DIRS}"
    )
endif()
 
# Can leave these off for silent mode:
add_compile_definitions(USE_PRINTF USE_DBG_PRINTF)
# add_compile_definitions(USE_PRINTF)

set(APP_SOURCES
    app_log.c
    main.c
    hardware_config.c
    menu.c
    menu_input.c
    oled.c
    oled_font.c
    micro_sd.c
    ps1_card_emulator.c
    ps1_card_bus.c
)

set(APP_WARNING_OPTIONS
    -Wall
    -Wextra
    -Wpedantic
    -Wformat=2
    -Wshadow
    -Wundef
    -Wdouble-promotion
    -Wstrict-prototypes
    -Wmissing-prototypes
    -Wold-style-definition
    -Wconversion
    -Wsign-conversion
    -Wcast-align
    -Wswitch-enum
    -Wvla
    #-Werror
)

# Add executable. Default name is the project name, version 0.1
add_executable(main ${APP_SOURCES})

# Keep strict warnings on project code only; Pico SDK and submodules stay quiet.
set_source_files_properties(
    ${APP_SOURCES}
    PROPERTIES
        COMPILE_OPTIONS "${APP_WARNING_OPTIONS}"
)

pico_set_program_name(main "main")
pico_set_program_version(main "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(main 1) 
pico_enable_stdio_usb(main 0)

# Add the standard library to the build
target_link_libraries(
    main
    pico_stdlib
    pico_multicore
    hardware_spi
    ${SDIO_RPI2_TARGET}
    )

# Add the standard include files to the build
target_include_directories(main PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

pico_add_extra_outputs(main)
