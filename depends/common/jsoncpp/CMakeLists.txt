cmake_minimum_required(VERSION 3.5)
project(jsoncpp)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SOURCES src/lib_json/json_reader.cpp
            src/lib_json/json_value.cpp
            src/lib_json/json_writer.cpp)

include_directories(${PROJECT_SOURCE_DIR}/include)

if(WIN32)
  add_definitions("/wd4996")
endif()

add_library(jsoncpp ${SOURCES})

install(TARGETS jsoncpp DESTINATION ${CMAKE_INSTALL_PREFIX}/lib) 
install(DIRECTORY include/json DESTINATION ${CMAKE_INSTALL_PREFIX}/include/jsoncpp)
