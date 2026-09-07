# Local SDK selector used by both the command line and the official VS Code extension.
if(NOT DEFINED PICO_SDK_PATH)
    if(DEFINED ENV{PICO_SDK_PATH})
        set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
    elseif(WIN32 AND DEFINED ENV{USERPROFILE})
        file(TO_CMAKE_PATH "$ENV{USERPROFILE}/.pico-sdk/sdk/2.3.1" PICO_SDK_PATH)
    elseif(DEFINED ENV{HOME})
        file(TO_CMAKE_PATH "$ENV{HOME}/.pico-sdk/sdk/2.3.1" PICO_SDK_PATH)
    endif()
endif()

if(NOT EXISTS "${PICO_SDK_PATH}/external/pico_sdk_import.cmake")
    message(FATAL_ERROR
        "Pico SDK 2.3.1 was not found. Install it with the official Raspberry Pi Pico VS Code extension, "
        "or set PICO_SDK_PATH.")
endif()
include("${PICO_SDK_PATH}/external/pico_sdk_import.cmake")
