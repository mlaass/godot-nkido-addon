cmake_minimum_required(VERSION 3.22)
project(nkido-addon LANGUAGES CXX)

# --- Paths to dependencies (override with -D) ---
set(GODOT_CPP_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../godot-cpp"
    CACHE PATH "Path to godot-cpp")
set(ENKIDO_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../enkido"
    CACHE PATH "Path to enkido (cedar + akkado)")

# --- godot-cpp ---
add_subdirectory(${GODOT_CPP_PATH} godot-cpp EXCLUDE_FROM_ALL)

# --- Cedar sources ---
set(CEDAR_DIR "${ENKIDO_PATH}/cedar")
set(CEDAR_SOURCES
    ${CEDAR_DIR}/src/cedar.cpp
    ${CEDAR_DIR}/src/vm/vm.cpp
    ${CEDAR_DIR}/src/opcodes/minblep_table.cpp
    ${CEDAR_DIR}/src/opcodes/minblep_state.cpp
    ${CEDAR_DIR}/src/io/audio_decoder.cpp
    ${CEDAR_DIR}/src/io/file_loader.cpp
    ${CEDAR_DIR}/src/audio/soundfont.cpp
    ${CEDAR_DIR}/src/dsp/fft.cpp
)

# --- Akkado sources ---
set(AKKADO_DIR "${ENKIDO_PATH}/akkado")
set(AKKADO_SOURCES
    ${AKKADO_DIR}/src/akkado.cpp
    ${AKKADO_DIR}/src/diagnostics.cpp
    ${AKKADO_DIR}/src/lexer.cpp
    ${AKKADO_DIR}/src/parser.cpp
    ${AKKADO_DIR}/src/mini_lexer.cpp
    ${AKKADO_DIR}/src/mini_parser.cpp
    ${AKKADO_DIR}/src/pattern_eval.cpp
    ${AKKADO_DIR}/src/symbol_table.cpp
    ${AKKADO_DIR}/src/analyzer.cpp
    ${AKKADO_DIR}/src/const_eval.cpp
    ${AKKADO_DIR}/src/codegen.cpp
    ${AKKADO_DIR}/src/codegen_arrays.cpp
    ${AKKADO_DIR}/src/codegen_functions.cpp
    ${AKKADO_DIR}/src/codegen_params.cpp
    ${AKKADO_DIR}/src/codegen_patterns.cpp
    ${AKKADO_DIR}/src/codegen_stereo.cpp
    ${AKKADO_DIR}/src/codegen_viz.cpp
    ${AKKADO_DIR}/src/typed_value.cpp
    ${AKKADO_DIR}/src/chord_parser.cpp
    ${AKKADO_DIR}/src/pattern_debug.cpp
    ${AKKADO_DIR}/src/tuning.cpp
    ${AKKADO_DIR}/src/source_map.cpp
    ${AKKADO_DIR}/src/file_resolver.cpp
    ${AKKADO_DIR}/src/import_scanner.cpp
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
