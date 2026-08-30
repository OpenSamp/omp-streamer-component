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
	streamer_component_api.cpp
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

# Link-time optimization for Release only (cross-TU inlining/devirtualization on the hot
# streaming loop; Debug stays fast). Gracefully skipped if the toolchain can't do IPO.
include(CheckIPOSupported)
check_ipo_supported(RESULT _streamer_ipo_ok OUTPUT _streamer_ipo_msg LANGUAGES CXX)
if(_streamer_ipo_ok)
	set_property(TARGET ${PROJECT_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
else()
	message(WARNING "Streamer: IPO/LTO unavailable, building without it: ${_streamer_ipo_msg}")
endif()

# amxplugin.cpp is a thin C-style dispatcher; skip PCH for it.
set_source_files_properties(${PROJECT_SOURCE_DIR}/lib/pawn/amxplugin.cpp PROPERTIES
	SKIP_PRECOMPILE_HEADERS ON
)

target_precompile_headers(${PROJECT_NAME} PRIVATE main.h)

# C++20/clang override shadow for a handful of vendored boost 1.75 headers that
# are ill-formed on modern clang (see patches/boost-cxx20/README). Must come
# BEFORE lib/boost so these copies win the include lookup; the submodule stays clean.
target_include_directories(${PROJECT_NAME} BEFORE PRIVATE
	${PROJECT_SOURCE_DIR}/patches/boost-cxx20
)

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
	# C++20 removes std::unary_negate / std::binary_negate that the vendored Eigen 3.3.4
	# still references. Re-enable them in MSVC's stdlib until we bump Eigen to 3.4.
	_HAS_DEPRECATED_NEGATORS=1
	_HAS_DEPRECATED_ADAPTOR_TYPEDEFS=1
	# Boost pulls a few removed/changed std facilities that still show up in C++20 builds.
	_HAS_DEPRECATED_RESULT_OF=1
)

# Strict warnings. Our source compiles clean at /W4 / -Wall -Wextra; noise comes
# almost entirely from third-party headers (boost/eigen/glm/open.mp-sdk) so we
# blanket-disable their top offenders.
if(MSVC)
	target_compile_options(${PROJECT_NAME} PRIVATE
		/W4
		# Enable C++ exceptions explicitly. MSVC gets /EHsc by default, but clang-cl
		# does NOT, so without this boost switches to its BOOST_NO_EXCEPTIONS mode and
		# leaves boost::throw_exception undefined -> link error. Keeps clang-cl and
		# MSVC behaviour identical (boost::geometry throws rather than terminating).
		/EHsc
		# Third-party spam leaking through PCH:
		/wd4127 # conditional expression is constant (boost::geometry template chain)
		/wd4201 # nonstandard extension: nameless struct/union (glm/open.mp types)
		/wd4244 # conversion, possible loss of data (AMX cell <-> int mixing)
		/wd4267 # conversion from size_t, possible loss of data
		/wd4702 # unreachable code (boost macros)
	)
else()
	target_compile_options(${PROJECT_NAME} PRIVATE
		-Wall
		-Wextra
		-Wno-unused-parameter
		-Wno-sign-compare
	)
endif()

# Optional AddressSanitizer build: -DSTREAMER_ASAN=ON.
option(STREAMER_ASAN "Build with AddressSanitizer" OFF)
if(STREAMER_ASAN)
	if(MSVC)
		target_compile_options(${PROJECT_NAME} PRIVATE /fsanitize=address)
	else()
		target_compile_options(${PROJECT_NAME} PRIVATE -fsanitize=address -fno-omit-frame-pointer)
		target_link_options(${PROJECT_NAME} PRIVATE -fsanitize=address)
	endif()
	message(STATUS "Streamer: AddressSanitizer enabled")
endif()

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
