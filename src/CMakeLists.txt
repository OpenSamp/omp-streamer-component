include(AMXConfig)

set(PLUGIN_SOURCES
	${PROJECT_SOURCE_DIR}/lib/pawn/amxplugin.cpp
	callbacks.cpp
	cell.cpp
	chunk-streamer.cpp
	core.cpp
	data.cpp
	grid.cpp
	identifier.cpp
	item.cpp
	natives_table.cpp
	openmp_component.cpp
	player.cpp
	streamer_api.cpp
	streamer.cpp
)

add_subdirectory(manipulation)
add_subdirectory(natives)
add_subdirectory(utility)

add_library(${PROJECT_NAME} MODULE ${PLUGIN_SOURCES})
set_target_properties(${PROJECT_NAME} PROPERTIES
	PREFIX ""
	LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# amxplugin.cpp is a thin C-style dispatcher; skip PCH for it.
set_source_files_properties(${PROJECT_SOURCE_DIR}/lib/pawn/amxplugin.cpp PROPERTIES
	SKIP_PRECOMPILE_HEADERS ON
)

target_precompile_headers(${PROJECT_NAME} PRIVATE main.h)

target_include_directories(${PROJECT_NAME} PRIVATE
	${PROJECT_SOURCE_DIR}/lib/pawn
	${PROJECT_SOURCE_DIR}/lib/pawn/amx
	${PROJECT_SOURCE_DIR}/lib/boost
	${EIGEN3_INCLUDE_DIR}
)

target_link_libraries(${PROJECT_NAME} PRIVATE OMP-SDK)

target_compile_definitions(${PROJECT_NAME} PRIVATE
	_SILENCE_CXX17_NEGATORS_DEPRECATION_WARNING
	_SILENCE_CXX17_ADAPTOR_TYPEDEFS_DEPRECATION_WARNING
)

# Platform / architecture handling.
if(WIN32)
	target_compile_definitions(${PROJECT_NAME} PRIVATE NOMINMAX)
endif()

if(UNIX AND NOT APPLE)
	target_compile_definitions(${PROJECT_NAME} PRIVATE LINUX)
	target_compile_options(${PROJECT_NAME} PRIVATE -fno-strict-aliasing)
	target_link_options(${PROJECT_NAME} PRIVATE -lrt)
endif()

# 32-bit build: SA-MP servers are x86, and open.mp still ships an x86 variant alongside x64.
# Select at configure time: `-DSTREAMER_ARCH=x86` or `x64`. Default follows the configured
# toolchain (pointer size).
if(NOT DEFINED STREAMER_ARCH)
	if(CMAKE_SIZEOF_VOID_P EQUAL 4)
		set(STREAMER_ARCH "x86")
	else()
		set(STREAMER_ARCH "x64")
	endif()
endif()

if(STREAMER_ARCH STREQUAL "x86")
	if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
		target_compile_options(${PROJECT_NAME} PRIVATE -m32)
		target_link_options(${PROJECT_NAME} PRIVATE -m32)
	endif()
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
	target_compile_options(${PROJECT_NAME} PRIVATE -Wno-attributes)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
	target_compile_options(${PROJECT_NAME} PRIVATE -Wno-ignored-attributes)
endif()

message(STATUS "Streamer: building for ${STREAMER_ARCH} (pointer size: ${CMAKE_SIZEOF_VOID_P})")
