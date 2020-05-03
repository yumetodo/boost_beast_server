cmake_minimum_required(VERSION 3.10.0)
enable_language(CXX)
set(CMAKE_CXX_STANDARD 17) # C++17...
set(CMAKE_CXX_STANDARD_REQUIRED ON) #...is required...
set(CMAKE_CXX_EXTENSIONS OFF) #...without compiler extensions like gnu++11

#
# import vcpkg config
#
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
      CACHE STRING "")
endif()
#
# Set our project name
#
project(boost_beast_server)

#
# Source files
#
set(boost_beast_server_src
    src/http_server.cpp
)

#
# find libraries and include
#

# find thread library
find_package(Threads REQUIRED)

# find Boost
# https://cmake.org/cmake/help/v3.7/module/FindBoost.html
set(Boost_USE_MULTITHREADED ON)
set(Boost_USE_STATIC_LIBS OFF)
set(Boost_USE_STATIC_RUNTIME OFF)
set(Boost_USE_DEBUG_RUNTIME OFF)
set(Boost_DEBUG ON)
add_definitions(${Boost_LIB_DIAGNOSTIC_DEFINITIONS})
set(boost_required_components
    atomic chrono container context coroutine date_time exception iostreams regex system thread
)
find_package(Boost 1.72 REQUIRED COMPONENTS ${boost_required_components})
if(NOT Boost_FOUND)
#   Config options
    set(BOOST_ROOT ${BOOST_ROOT} CACHE PATH "Set boost root directory" FORCE)
    set(BOOST_LIBRARYDIR ${BOOST_LIBRARYDIR} CACHE PATH "Set boost library directory" FORCE)
    message(FATAL_ERROR "Fail to find Boost")
endif()
include_directories(${Boost_INCLUDE_DIRS} "include/")

#
# Update compiler waring flags
#
if(MSVC)
  # Force to always compile with W4
  if(CMAKE_CXX_FLAGS MATCHES "/W[0-4]")
    string(REGEX REPLACE "/W[0-4]" "/W4" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
  endif()
elseif(CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX)
  # Update if necessary
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wno-long-long -pedantic")
endif()

#
# Compile
#
add_library(boost_beast_server STATIC ${boost_beast_server_src})

#
# Specify install headers
#
set_target_properties(boost_beast_server PROPERTIES
  PUBLIC_HEADER include/boost_beast_server/http_server.hpp
)
#
# Link
#
target_link_libraries(boost_beast_server Threads::Threads)
target_link_libraries(boost_beast_server Boost::disable_autolinking ${Boost_LIBRARIES} wsock32 ws2_32)

#
# ターゲットのインストール指定
#
install(TARGETS boost_beast_server
    EXPORT boost_beast_server-export                         # EXPORT オプションで エクスポート名を指定する
    LIBRARY        DESTINATION lib            # ライブラリのインストール先
    INCLUDES       DESTINATION include        # エクスポート時にincludeパスに含めるパスを指定する
    PUBLIC_HEADER  DESTINATION include/boost_beast_server)   # ヘッダのインストール先

#
#  boost_beast_server-config.cmake を自動生成してインストールする。
#
install(EXPORT boost_beast_server-export
    FILE boost_beast_server-config.cmake                     # ファイル名を指定する
    DESTINATION share/cmake/boost_beast_server/              # インストール先
    EXPORT_LINK_INTERFACE_LIBRARIES)          # 同時にリンクすべきライブラリをエクスポートする

    add_subdirectory(example)