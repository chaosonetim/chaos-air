cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Compiler options
set(STM32_MCU_FLAGS  "-mcpu=cortex-m55 -mfpu=fpv5-d16 -mfloat-abi=hard " )

#Linker options
set(STM32_LINKER_SCRIPT STM32N657XX_LRUN_ns.ld)
set(STM32_LINKER_OPTION  )

# Set the project name
set(CMAKE_PROJECT_NAME chaos_air_AppliNonSecure)
project(${CMAKE_PROJECT_NAME})

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Include mx-generated
include("mx-generated.cmake")

option(CAMERA_SNAPSHOT "Capture one IMX335 frame instead of starting ThreadX" OFF)
option(CAMERA_FULL_FRAME "Resize the full sensor frame to RGB565 on snapshot pipe1" OFF)
option(CAMERA_AUTO_ISP "Run ST ISP auto-exposure, auto-white-balance and gamma" OFF)
option(CAMERA_COMPARE_SHARPENING "Pause before sharpening for a same-frame comparison export" OFF)
if(CAMERA_COMPARE_SHARPENING AND NOT CAMERA_AUTO_ISP)
    message(FATAL_ERROR "CAMERA_COMPARE_SHARPENING requires CAMERA_AUTO_ISP")
endif()
if(CAMERA_AUTO_ISP AND NOT CAMERA_FULL_FRAME)
    message(FATAL_ERROR "CAMERA_AUTO_ISP requires CAMERA_FULL_FRAME")
endif()
if(CAMERA_FULL_FRAME AND NOT CAMERA_SNAPSHOT)
    message(FATAL_ERROR "CAMERA_FULL_FRAME requires CAMERA_SNAPSHOT")
endif()
if(CAMERA_SNAPSHOT)
    target_compile_definitions(stm32cubemx INTERFACE CAMERA_SNAPSHOT)
    if(CAMERA_FULL_FRAME)
        target_compile_definitions(stm32cubemx INTERFACE CAMERA_FULL_FRAME)
    endif()
    if(CAMERA_AUTO_ISP)
        target_compile_definitions(stm32cubemx INTERFACE CAMERA_AUTO_ISP)
        if(CAMERA_COMPARE_SHARPENING)
            target_compile_definitions(stm32cubemx INTERFACE CAMERA_COMPARE_SHARPENING)
        endif()
        target_include_directories(stm32cubemx INTERFACE ../Middlewares/ST/isp/Inc)
        target_sources(${CMAKE_PROJECT_NAME} PRIVATE
            ../Middlewares/ST/isp/Src/isp_core.c
            ../Middlewares/ST/isp/Src/isp_services.c
            ../Middlewares/ST/isp/Src/isp_algo.c
            ../Middlewares/ST/isp/Src/isp_ae_algo.c
            ../Middlewares/ST/isp/Src/isp_awb_algo.c)
    endif()
    target_include_directories(stm32cubemx INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/../Drivers/BSP/Components/imx335)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        Core/Src/camera_snapshot.c
        ../Drivers/BSP/Components/imx335/imx335.c
        ../Drivers/BSP/Components/imx335/imx335_reg.c)
    target_sources(STM32_Drivers PRIVATE
        ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_i2c.c
        ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_i2c_ex.c
        ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_uart.c
        ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_uart_ex.c
        ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_dcmipp.c)
    get_target_property(app_sources ${CMAKE_PROJECT_NAME} SOURCES)
    list(FILTER app_sources EXCLUDE REGEX "(app_threadx|app_azure_rtos|app_netxduo|tx_initialize_low_level)\\.")
    set_property(TARGET ${CMAKE_PROJECT_NAME} PROPERTY SOURCES "${app_sources}")
    set_property(TARGET netxduo threadx PROPERTY EXCLUDE_FROM_ALL TRUE)
    set(CAMERA_SECURE_IMPORT "" CACHE FILEPATH "Snapshot secure build's CMSE import object")
    if(NOT EXISTS "${CAMERA_SECURE_IMPORT}")
        message(FATAL_ERROR "Build the snapshot secure image first and set CAMERA_SECURE_IMPORT")
    endif()
    set_property(TARGET ${CMAKE_PROJECT_NAME} PROPERTY LINK_LIBRARIES
        "STM32_Drivers;${TOOLCHAIN_LINK_LIBRARIES};${CAMERA_SECURE_IMPORT}")
    target_link_options(${CMAKE_PROJECT_NAME} PRIVATE -Wl,--defsym=__camera_snapshot=1)
endif()


# In order to use CMake for cross-compiling
set(CMAKE_CXX_COMPILER_FORCED "true")

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined libraries
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Validate that STM32CubeMX code is compatible with C standard
if((CMAKE_C_STANDARD EQUAL 90) OR (CMAKE_C_STANDARD EQUAL 99))
    message(ERROR "Generated code requires C11 or higher")
endif()





