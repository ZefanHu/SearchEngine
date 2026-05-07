cmake_minimum_required(VERSION 3.10)
project(SearchEngine CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

include_directories(${CMAKE_SOURCE_DIR}/include)

file(GLOB COMMON_SOURCES  ${CMAKE_SOURCE_DIR}/src/common/*.cpp)
file(GLOB OFFLINE_SOURCES ${CMAKE_SOURCE_DIR}/src/offline/*.cpp)
file(GLOB SERVER_SOURCES  ${CMAKE_SOURCE_DIR}/src/server/*.cpp)

# build_index 单独使用 src/offline/main.cpp，所以从 search_offline 库中排除它
list(REMOVE_ITEM OFFLINE_SOURCES ${CMAKE_SOURCE_DIR}/src/offline/main.cpp)

add_library(search_common  STATIC ${COMMON_SOURCES})
add_library(search_offline STATIC ${OFFLINE_SOURCES})

add_executable(search_server ${SERVER_SOURCES})
target_link_libraries(search_server
    PRIVATE
        search_common
        wfrest
        workflow
        tinyxml2
)

add_executable(build_index src/offline/main.cpp)
target_link_libraries(build_index search_offline search_common wfrest workflow tinyxml2)
