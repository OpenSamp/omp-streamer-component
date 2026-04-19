include(AMXConfig)
include(AddSAMPPlugin)

set(PLUGIN_SOURCES ${PROJECT_SOURCE_DIR}/lib/pawn/amxplugin.cpp)

add_subdirectory(manipulation)
add_subdirectory(natives)
add_subdirectory(utility)

list(APPEND PLUGIN_SOURCES
	callbacks.cpp
	cell.cpp
	chunk-streamer.cpp
	core.cpp
	data.cpp
	grid.cpp
	identifier.cpp
	item.cpp
	openmp_component.cpp
	player.cpp
	streamer_api.cpp
	streamer.cpp
)

add_samp_plugin(${PROJECT_NAME} ${PLUGIN_SOURCES})

target_link_libraries(${PROJECT_NAME} OMP-SDK)

if(WIN32)
	add_definitions(-DNOMINMAX)
else()
	set_property(TARGET ${PROJECT_NAME} APPEND_STRING PROPERTY COMPILE_FLAGS " -fno-strict-aliasing")
	set_property(TARGET ${PROJECT_NAME} APPEND_STRING PROPERTY LINK_FLAGS " -lrt")
endif()

add_definitions(-D_SILENCE_CXX17_NEGATORS_DEPRECATION_WARNING -D_SILENCE_CXX17_ADAPTOR_TYPEDEFS_DEPRECATION_WARNING)

set_target_properties(${PROJECT_NAME} PROPERTIES
	LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

install(TARGETS ${PROJECT_NAME} DESTINATION bin)
