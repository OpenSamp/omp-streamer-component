cmake_minimum_required(VERSION 3.19)
project(streamer)

set(PLUGIN_VERSION "2.9.6")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
	add_definitions("/MP")
endif()

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/lib/cmake-modules")
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/lib/eigen/cmake")

# open.mp SDK (interface target; propagates glm, robin-hood, span-lite, string-view-lite,
# NOMINMAX, GLM_FORCE_*, /fp:strict, etc.)
add_subdirectory(lib/open.mp-sdk)

if(EIGEN3_INCLUDE_DIR)
	file(TO_CMAKE_PATH ${EIGEN3_INCLUDE_DIR} EIGEN3_INCLUDE_DIR)
	list(APPEND CMAKE_MODULE_PATH "${EIGEN3_INCLUDE_DIR}/cmake")
else()
	set(EIGEN3_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/lib/eigen")
endif()

find_package(Eigen3 3.3.4 REQUIRED)

add_subdirectory(src)
