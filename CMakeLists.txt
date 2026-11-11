cmake_minimum_required(VERSION 3.16)
project(SearchEngine VERSION 2.0.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# 全局编译设置
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # 给 clangd / IDE 用

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

option(SEARCH_BUILD_TESTS  "Build unit tests"                       ON)
option(SEARCH_BUILD_SERVER "Build the wfrest HTTP server if found"  ON)

# 只对“我们自己的代码”开启严格告警；第三方头文件用 SYSTEM 方式引入，不产生告警
add_library(search_warnings INTERFACE)
target_compile_options(search_warnings INTERFACE -Wall -Wextra -Wpedantic -Wshadow)

# ---------------------------------------------------------------------------
# 依赖
# ---------------------------------------------------------------------------
find_package(Threads REQUIRED)

# 仓库内置的 header-only 第三方库（cppjieba / nlohmann json），保持原样不修改
add_library(search_third_party INTERFACE)
target_include_directories(search_third_party SYSTEM INTERFACE ${CMAKE_SOURCE_DIR}/third_party)

# tinyxml2：优先用系统安装的（apt install libtinyxml2-dev），找不到再自动拉取源码
find_package(tinyxml2 CONFIG QUIET)
if(NOT TARGET tinyxml2::tinyxml2)
    find_library(TINYXML2_LIBRARY tinyxml2)
    find_path(TINYXML2_INCLUDE_DIR tinyxml2.h)
    if(TINYXML2_LIBRARY AND TINYXML2_INCLUDE_DIR)
        add_library(tinyxml2::tinyxml2 UNKNOWN IMPORTED)
        set_target_properties(tinyxml2::tinyxml2 PROPERTIES
            IMPORTED_LOCATION "${TINYXML2_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${TINYXML2_INCLUDE_DIR}")
    else()
        message(STATUS "tinyxml2 not found on system, fetching from GitHub ...")
        include(FetchContent)
        set(tinyxml2_BUILD_TESTING OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(tinyxml2
            GIT_REPOSITORY https://github.com/leethomason/tinyxml2.git
            GIT_TAG        10.0.0
            GIT_SHALLOW    TRUE)
        FetchContent_MakeAvailable(tinyxml2)
    endif()
endif()

# ---------------------------------------------------------------------------
# search_core：离线与在线共用的核心库（不依赖任何网络框架，可移植）
# ---------------------------------------------------------------------------
add_library(search_core STATIC
    src/common/Config.cpp
    src/common/Log.cpp
    src/common/MappedFile.cpp
    src/common/BinaryWriter.cpp
    src/common/Tokenizer.cpp
    src/index/PageStore.cpp
    src/index/InvertedIndex.cpp
    src/online/TrieTree.cpp
    src/online/KeyRecommender.cpp
    src/online/WebSearcher.cpp
    src/online/SearchService.cpp
)
target_include_directories(search_core PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(search_core
    PUBLIC  search_third_party Threads::Threads
    PRIVATE search_warnings)

# ---------------------------------------------------------------------------
# search_offline：离线建库管线
# ---------------------------------------------------------------------------
add_library(search_offline STATIC
    src/offline/HtmlCleaner.cpp
    src/offline/CorpusReader.cpp
    src/offline/SimHash.cpp
    src/offline/IndexBuilder.cpp
    src/offline/IndexPipeline.cpp
)
target_link_libraries(search_offline
    PUBLIC  search_core
    PRIVATE tinyxml2::tinyxml2 search_warnings)

# ---------------------------------------------------------------------------
# 可执行程序
# ---------------------------------------------------------------------------
add_executable(build_index apps/build_index.cpp)
target_link_libraries(build_index PRIVATE search_offline search_warnings)

# 不依赖 HTTP 框架的命令行查询工具：与 search_server 走完全相同的 SearchService
add_executable(search_cli apps/search_cli.cpp)
target_link_libraries(search_cli PRIVATE search_core search_warnings)

# HTTP 服务依赖 Sogou Workflow + wfrest（仅 Linux/macOS）。找不到时跳过，不影响其它目标
if(SEARCH_BUILD_SERVER)
    find_library(WFREST_LIBRARY   wfrest)
    find_library(WORKFLOW_LIBRARY workflow)
    find_path(WFREST_INCLUDE_DIR   wfrest/HttpServer.h)
    find_path(WORKFLOW_INCLUDE_DIR workflow/WFFacilities.h)
    if(WFREST_LIBRARY AND WORKFLOW_LIBRARY AND WFREST_INCLUDE_DIR AND WORKFLOW_INCLUDE_DIR)
        find_package(OpenSSL QUIET)
        find_package(ZLIB QUIET)
        add_executable(search_server apps/search_server.cpp src/server/HttpServer.cpp)
        target_include_directories(search_server SYSTEM PRIVATE
            ${WFREST_INCLUDE_DIR} ${WORKFLOW_INCLUDE_DIR})
        target_link_libraries(search_server PRIVATE
            search_core search_warnings ${WFREST_LIBRARY} ${WORKFLOW_LIBRARY})
        if(OpenSSL_FOUND)
            target_link_libraries(search_server PRIVATE OpenSSL::SSL OpenSSL::Crypto)
        endif()
        if(ZLIB_FOUND)
            target_link_libraries(search_server PRIVATE ZLIB::ZLIB)
        endif()
    else()
        message(WARNING "wfrest/workflow not found: target 'search_server' is skipped. "
                        "build_index and search_cli are still available.")
    endif()
endif()

# MinGW 下静态链接运行时，避免 PATH 里混入其它版本的 libstdc++ DLL
if(MINGW)
    foreach(tgt build_index search_cli)
        target_link_options(${tgt} PRIVATE -static)
    endforeach()
endif()

# ---------------------------------------------------------------------------
# 单元测试（零依赖的轻量断言框架，见 tests/unit_tests.cpp）
# ---------------------------------------------------------------------------
if(SEARCH_BUILD_TESTS)
    enable_testing()
    add_executable(unit_tests tests/unit_tests.cpp)
    target_link_libraries(unit_tests PRIVATE search_offline search_warnings)
    set_target_properties(unit_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
    if(MINGW)
        target_link_options(unit_tests PRIVATE -static)
    endif()
    add_test(NAME unit_tests COMMAND unit_tests)
endif()
