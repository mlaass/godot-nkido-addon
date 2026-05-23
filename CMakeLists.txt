cmake_minimum_required(VERSION 3.22)
project(nkido-addon LANGUAGES CXX)

# --- Paths to dependencies (override with -D) ---
set(GODOT_CPP_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../godot-cpp"
    CACHE PATH "Path to godot-cpp")
set(NKIDO_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../nkido"
    CACHE PATH "Path to nkido (cedar + akkado)")

# --- godot-cpp ---
add_subdirectory(${GODOT_CPP_PATH} godot-cpp EXCLUDE_FROM_ALL)

# --- Cedar sources ---
set(CEDAR_DIR "${NKIDO_PATH}/cedar")
file(GLOB_RECURSE CEDAR_SOURCES CONFIGURE_DEPENDS
    "${CEDAR_DIR}/src/*.cpp"
)
# Drop network URI handlers — they pull in cpp-httplib which requires
# C++ exceptions, but godot-cpp compiles with -fno-exceptions. The addon
# explicitly does not support http:// / github: URIs (see
# auto_load_required_assets in nkido_audio_stream.cpp).
list(FILTER CEDAR_SOURCES EXCLUDE REGEX ".*/io/handlers/(http|github)_handler\\.cpp$")

# --- Akkado sources ---
set(AKKADO_DIR "${NKIDO_PATH}/akkado")
file(GLOB_RECURSE AKKADO_SOURCES CONFIGURE_DEPENDS
    "${AKKADO_DIR}/src/*.cpp"
)

# --- Extension sources ---
set(EXT_SRC "${CMAKE_CURRENT_SOURCE_DIR}/addons/nkido/src")
set(EXT_SOURCES
    ${EXT_SRC}/register_types.cpp
    ${EXT_SRC}/nkido_akkado_source.cpp
    ${EXT_SRC}/nkido_akkado_source_format_loader.cpp
    ${EXT_SRC}/nkido_akkado_source_format_saver.cpp
    ${EXT_SRC}/nkido_audio_stream.cpp
    ${EXT_SRC}/nkido_audio_stream_playback.cpp
)

# --- Shared library ---
add_library(nkido SHARED
    ${CEDAR_SOURCES}
    ${AKKADO_SOURCES}
    ${EXT_SOURCES}
)

target_compile_features(nkido PRIVATE cxx_std_20)

target_include_directories(nkido PRIVATE
    ${CEDAR_DIR}/include
    ${AKKADO_DIR}/include
    ${CEDAR_DIR}/third_party
    ${EXT_SRC}
)

target_link_libraries(nkido PRIVATE godot-cpp)

# Suppress warnings from cedar/akkado sources (they compile clean but godot-cpp
# enables extra warnings that may fire on third-party code)
if(MSVC)
    set_source_files_properties(${CEDAR_SOURCES} ${AKKADO_SOURCES}
        PROPERTIES COMPILE_OPTIONS "/w")
else()
    set_source_files_properties(${CEDAR_SOURCES} ${AKKADO_SOURCES}
        PROPERTIES COMPILE_OPTIONS "-w")
endif()

# LTO for Release builds
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported)
    if(ipo_supported)
        set_target_properties(nkido PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
endif()

# --- Output naming (matches godot-cpp convention) ---
get_target_property(GODOTCPP_SUFFIX godot-cpp GODOTCPP_SUFFIX)

set(OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/addons/nkido/bin/")

set_target_properties(nkido PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    BUILD_RPATH_USE_ORIGIN ON
    LIBRARY_OUTPUT_DIRECTORY "$<1:${OUTPUT_DIR}>"
    RUNTIME_OUTPUT_DIRECTORY "$<1:${OUTPUT_DIR}>"
    PREFIX "lib"
    OUTPUT_NAME "nkido${GODOTCPP_SUFFIX}"
)

# On Windows, DLLs go to RUNTIME_OUTPUT_DIRECTORY, not LIBRARY
if(WIN32)
    set_target_properties(nkido PROPERTIES PREFIX "")
endif()
