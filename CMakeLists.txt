cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/mod.json" MOD_JSON)
string(JSON MOD_VERSION GET ${MOD_JSON} "version")

project(PaimonThumbnails VERSION ${MOD_VERSION})

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)

add_library(${PROJECT_NAME} SHARED ${SOURCES})

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

if (MSVC)
    add_compile_options(/FS)
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)
# target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/geode-deps) removed to prevent ABI conflicts

# ==========================================
# ARQUITECTURAS MULTIPLATAFORMA
# ==========================================

# iOS y macOS: configurar arquitecturas correctas
if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
    message(STATUS "Building for iOS: arm64")
elseif (APPLE)
    # macOS: ambas arquitecturas (Intel y Apple Silicon)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
    message(STATUS "Building for macOS: arm64 + x86_64")
endif()


# ==========================================
# CONFIGURACIONES POR PLATAFORMA
# ==========================================

if (WIN32)
    # Windows: WinHTTP para HttpClient
    target_link_libraries(${PROJECT_NAME} winhttp)
    # GEODE_IS_WINDOWS ya lo define el SDK, no redefinir
    message(STATUS "Building for Windows")
endif()

if (ANDROID)
    # Android: configuraciones específicas
    target_link_options(${PROJECT_NAME} PRIVATE "-u" "ANativeActivity_onCreate")
    # GEODE_IS_ANDROID ya lo define el SDK, no redefinir
    target_link_libraries(${PROJECT_NAME} log android)
    message(STATUS "Building for Android")
endif()

if (APPLE)
    # macOS/iOS: Frameworks del sistema
    # GEODE_IS_APPLE / GEODE_IS_IOS / GEODE_IS_MACOS ya los define el SDK, no redefinir
    
    if (IOS OR "${CMAKE_SYSTEM_NAME}" STREQUAL "iOS")
        message(STATUS "Building for iOS")
    else()
        message(STATUS "Building for macOS")
    endif()
    
    # CoreGraphics e ImageIO para manipulación de imágenes
    find_library(CORE_GRAPHICS CoreGraphics REQUIRED)
    find_library(IMAGE_IO ImageIO REQUIRED)
    find_library(FOUNDATION Foundation REQUIRED)
    target_link_libraries(${PROJECT_NAME} ${CORE_GRAPHICS} ${IMAGE_IO} ${FOUNDATION})
endif()

setup_geode_mod(${PROJECT_NAME} EXTERNALS geode.node-ids:1.23.0)
