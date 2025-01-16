
cmake_minimum_required(VERSION 3.31)

project("Oskar's HLT"
    DESCRIPTION "Oskar's Half-Life Compilation Tools"
    LANGUAGES CXX
    VERSION 0.1.0
)

message("${CMAKE_PROJECT_NAME} > Starting configuration for ${CMAKE_PROJECT_DESCRIPTION}")

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tools)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tools)

#================
# Configuration
#================

set(HLT_DIR ${CMAKE_SOURCE_DIR}/src)

#================
# Common
#================

set(COMMON_DIR ${HLT_DIR}/common)

set(COMMON_SOURCES
    ${COMMON_DIR}/bounding_box.cpp
    ${COMMON_DIR}/bsp_file_sizes.cpp
    ${COMMON_DIR}/bspfile.cpp
    ${COMMON_DIR}/cmdlib.cpp
    ${COMMON_DIR}/cmdlinecfg.cpp
    ${COMMON_DIR}/filelib.cpp
    ${COMMON_DIR}/hull_size.cpp
    ${COMMON_DIR}/legacy_character_encodings.cpp
    ${COMMON_DIR}/log.cpp
    ${COMMON_DIR}/mathlib.cpp
    ${COMMON_DIR}/messages.cpp
    ${COMMON_DIR}/scriplib.cpp
    ${COMMON_DIR}/threads.cpp
    ${COMMON_DIR}/utf8.cpp
    ${COMMON_DIR}/winding.cpp
)

set(COMMON_HEADERS
    ${COMMON_DIR}/bounding_box.h
    ${COMMON_DIR}/bsp_file_sizes.h
    ${COMMON_DIR}/bspfile.h
    ${COMMON_DIR}/call_finally.h
    ${COMMON_DIR}/context.h
    ${COMMON_DIR}/cmdlib.h
    ${COMMON_DIR}/cmdlinecfg.h
    ${COMMON_DIR}/cli_option_defaults.h
    ${COMMON_DIR}/entity_key_value.h
    ${COMMON_DIR}/filelib.h
    ${COMMON_DIR}/hlassert.h
    ${COMMON_DIR}/hull_size.h
    ${COMMON_DIR}/legacy_character_encodings.h
    ${COMMON_DIR}/log.h
    ${COMMON_DIR}/map_token_parser.h
    ${COMMON_DIR}/mathlib.h
    ${COMMON_DIR}/mathtypes.h
    ${COMMON_DIR}/messages.h
    ${COMMON_DIR}/planes.h
    ${COMMON_DIR}/scriplib.h
    ${COMMON_DIR}/threads.h
    ${COMMON_DIR}/time_counter.h
    ${COMMON_DIR}/utf8.h
    ${COMMON_DIR}/util.h
    ${COMMON_DIR}/vector_for_overwriting.h
    ${COMMON_DIR}/vector_inplace.h
    ${COMMON_DIR}/wad_structs.h
    ${COMMON_DIR}/wad_texture_name.h
    ${COMMON_DIR}/win32fix.h
    ${COMMON_DIR}/winding.h
    ${COMMON_DIR}/worldspawn_wad_value_parser.h
)

#================
# BSP
#================

set(BSP_DIR ${HLT_DIR}/hlbsp)

set(BSP_SOURCES
    ${BSP_DIR}/brink.cpp
    ${BSP_DIR}/merge.cpp
    ${BSP_DIR}/outside.cpp
    ${BSP_DIR}/portals.cpp
    ${BSP_DIR}/qbsp.cpp
    ${BSP_DIR}/solidbsp.cpp
    ${BSP_DIR}/surfaces.cpp
    ${BSP_DIR}/tjunc.cpp
    ${BSP_DIR}/writebsp.cpp
)

set(BSP_HEADERS
#    ${COMMON_HEADERS}
    ${BSP_DIR}/brink.h
    ${BSP_DIR}/bsp5.h
)

#================
# CSG
#================

set(CSG_DIR ${HLT_DIR}/hlcsg)

set(CSG_SOURCES
    ${CSG_DIR}/brush.cpp
    ${CSG_DIR}/brushunion.cpp
    ${CSG_DIR}/hullfile.cpp
    ${CSG_DIR}/map.cpp
    ${CSG_DIR}/properties.cpp
    ${CSG_DIR}/qcsg.cpp
    ${CSG_DIR}/textures.cpp
    ${CSG_DIR}/wadcfg.cpp
    ${CSG_DIR}/wadpath.cpp
)

set(CSG_HEADERS
#    ${COMMON_HEADERS}
    ${CSG_DIR}/csg.h
    ${CSG_DIR}/wadpath.h
)

#================
# RAD
#================

set(RAD_DIR ${HLT_DIR}/hlrad)

set(RAD_SOURCES
    ${RAD_DIR}/compress.cpp
    ${RAD_DIR}/lerp.cpp
    ${RAD_DIR}/lightmap.cpp
    ${RAD_DIR}/loadtextures.cpp
    ${RAD_DIR}/mathutil.cpp
	${RAD_DIR}/meshdesc.cpp
	${RAD_DIR}/meshtrace.cpp
    ${RAD_DIR}/nomatrix.cpp
    ${RAD_DIR}/progmesh.cpp
    ${RAD_DIR}/qrad.cpp
    ${RAD_DIR}/qradutil.cpp
    ${RAD_DIR}/rad_cli_option_defaults.h
    ${RAD_DIR}/sparse.cpp
	${RAD_DIR}/studio.cpp
    ${RAD_DIR}/trace.cpp
    ${RAD_DIR}/transfers.cpp
    ${RAD_DIR}/transparency.cpp
    ${RAD_DIR}/vismatrix.cpp
    ${RAD_DIR}/vismatrixutil.cpp
)

set(RAD_HEADERS
#    ${COMMON_HEADERS}
    ${RAD_DIR}/compress.h
	${RAD_DIR}/list.h
	${RAD_DIR}/meshdesc.h
	${RAD_DIR}/meshtrace.h
    ${RAD_DIR}/qrad.h
	${RAD_DIR}/studio.h
)

#================
# VIS
#================

set(VIS_DIR ${HLT_DIR}/hlvis)

set(VIS_SOURCES
    ${VIS_DIR}/flow.cpp
    ${VIS_DIR}/vis.cpp
)

set(VIS_HEADERS
#    ${COMMON_HEADERS}
    ${VIS_DIR}/vis.h
)

#================
# RIPENT
#================

set(RIPENT_DIR ${HLT_DIR}/ripent)

set(RIPENT_SOURCES
    ${RIPENT_DIR}/ripent.cpp
)

set(RIPENT_HEADERS
#    ${COMMON_HEADERS}
    ${RIPENT_DIR}/ripent.h
)

#================
# Include
#================

include_directories(
    ${COMMON_DIR}
)

#================
# Definitions
#================

add_compile_definitions(
    PROJECT_DESCRIPTION="${CMAKE_PROJECT_DESCRIPTION}"
    PROJECT_ISSUE_TRACKER="https://github.com/oskarlh/ohlt/issues"
    PROJECT_NAME="${CMAKE_PROJECT_NAME}"
    PROJECT_VERSION="${CMAKE_PROJECT_VERSION}"
)


#================
# System config
#================

if (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    add_compile_options(
        -Wall
        -O2
        -fno-strict-aliasing
        -pthread
        -pipe
    )
    add_compile_definitions(
        PLATFORM_VERSION="Linux"
        VERSION_LINUX
        SYSTEM_POSIX
    )
    message("${CMAKE_PROJECT_NAME} > Configuring for ${CMAKE_SYSTEM_NAME}")
elseif (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    add_compile_definitions(
        PLATFORM_VERSION="MacOS"
        VERSION_MACOS
        SYSTEM_POSIX
    )
    message("${CMAKE_PROJECT_NAME} > Configuring for ${CMAKE_SYSTEM_NAME}")
elseif (${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    add_compile_definitions(
        PLATFORM_VERSION="64-bit Windows"
        SYSTEM_WIN32
        VERSION_WINDOWS
    )
    message("${CMAKE_PROJECT_NAME} > Configuring for ${CMAKE_SYSTEM_NAME}")
else()
    add_compile_definitions(
        PLATFORM_VERSION="???"
    )
    message("${CMAKE_PROJECT_NAME} > WARNING: Unknown system \"${CMAKE_SYSTEM_NAME}\"")
endif()


# Tell MSVC the source files are in UTF-8
add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

#================
# Targets
#================

add_library(common OBJECT ${COMMON_SOURCES} ${COMMON_HEADERS})

add_executable(hlbsp ${BSP_SOURCES} ${BSP_HEADERS} windows.manifest)
add_executable(hlcsg ${CSG_SOURCES} ${CSG_HEADERS} windows.manifest)
add_executable(hlrad ${RAD_SOURCES} ${RAD_HEADERS} windows.manifest)
add_executable(hlvis ${VIS_SOURCES} ${VIS_HEADERS} windows.manifest)
add_executable(ripent ${RIPENT_SOURCES} ${RIPENT_HEADERS} windows.manifest)

set_target_properties(common hlbsp hlcsg hlrad hlvis ripent
    PROPERTIES
        CXX_STANDARD 26
)

target_link_libraries(hlbsp PUBLIC common)
target_link_libraries(hlcsg PUBLIC common)
target_link_libraries(hlrad PUBLIC common)
target_link_libraries(hlvis PUBLIC common)
target_link_libraries(ripent PUBLIC common)
