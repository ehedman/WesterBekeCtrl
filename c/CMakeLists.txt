cmake_minimum_required(VERSION 3.13)
include(pico_sdk_import.cmake)
project(WesterBekeCtrl VERSION 1.0)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

include_regular_expression("^.*(h|cpp|inl|inc)$")

# Add a compilation subdirectory
add_subdirectory(lib/Config)
add_subdirectory(lib/LCD)
add_subdirectory(lib/Fonts)
add_subdirectory(lib/GUI)
add_subdirectory(examples)


# Add a header directory
include_directories(examples)
include_directories(./lib/Config)
include_directories(./lib/GUI)
include_directories(./lib/LCD)

configure_file(wbekectrl.h.in wbekectrl.h)
configure_file(custom.h.in rtc.def)

# Where the pico in BOOTSEL mode automounts on a (Mint) host
set(UF2DESTINATION /media/$ENV{LOGNAME}/RPI-RP2)

add_custom_target(dynDefines
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/custom.cmake
    )

add_custom_command( 
    TARGET dynDefines PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/custom.cmake
    )

# create an executable file
add_executable(main
    main.c
)

add_dependencies(main dynDefines)

target_link_libraries(main
    examples LCD GUI Fonts Config pico_stdlib hardware_spi pico_multicore
    )


# enable usb output, disable uart output
pico_enable_stdio_usb(main 1)
pico_enable_stdio_uart(main 1)

# create map/bin/hex/uf2 file etc.
pico_add_extra_outputs(main)

target_include_directories(examples 
PRIVATE ${PICO_SDK_PATH}/src/host/pico_multicore/include
PRIVATE ${PROJECT_BINARY_DIR}
PRIVATE ${CMAKE_CURRENT_LIST_DIR}
)

install(FILES "${PROJECT_BINARY_DIR}/main.uf2" DESTINATION ${UF2DESTINATION})


