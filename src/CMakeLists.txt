include(AMXConfig)
include(AddSAMPPlugin)

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

add_samp_plugin(${PROJECT_NAME} ${PLUGIN_SOURCES})

# amxplugin.cpp is a thin C-style dispatcher and doesn't need (and can't benefit from) the
# fat main.h precompiled header everyone else uses.
set_source_files_properties(${PROJECT_SOURCE_DIR}/lib/pawn/amxplugin.cpp PROPERTIES
	SKIP_PRECOMPILE_HEADERS ON
)

target_precompile_headers(${PROJECT_NAME} PRIVATE main.h)

target_include_directories(${PROJECT_NAME} PRIVATE
	${PROJECT_SOURCE_DIR}/lib/pawn
	${PROJECT_SOURCE_DIR}/lib/boost
	${EIGEN3_INCLUDE_DIR}
)

target_link_libraries(${PROJECT_NAME} PRIVATE OMP-SDK)

target_compile_definitions(${PROJECT_NAME} PRIVATE
	_SILENCE_CXX17_NEGATORS_DEPRECATION_WARNING
	_SILENCE_CXX17_ADAPTOR_TYPEDEFS_DEPRECATION_WARNING
)

if(WIN32)
	target_compile_definitions(${PROJECT_NAME} PRIVATE NOMINMAX)
else()
	target_compile_options(${PROJECT_NAME} PRIVATE -fno-strict-aliasing)
	target_link_options(${PROJECT_NAME} PRIVATE -lrt)
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES
	LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
