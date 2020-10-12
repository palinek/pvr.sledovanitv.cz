cmake_minimum_required(VERSION 3.5)
project(pvr.sledovanitv.cz)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR})

find_package(Kodi REQUIRED)
find_package(JsonCpp REQUIRED)

include_directories(${KODI_INCLUDE_DIR}/.. # Hack way with "/..", need bigger Kodi cmake rework to match right include ways
                    ${JSONCPP_INCLUDE_DIRS})

set(DEPLIBS ${JSONCPP_LIBRARIES})


set(SLEDOVANITV_SOURCES
  src/ApiManager.cpp
  src/Data.cpp)

set(SLEDOVANITV_HEADERS
  src/ApiManager.h
  src/CallLimiter.hh
  src/Data.h)

if(WIN32)
  add_definitions("/wd4996")
endif()

build_addon(pvr.sledovanitv.cz SLEDOVANITV DEPLIBS)
set_property(TARGET pvr.sledovanitv.cz PROPERTY CXX_STANDARD 14)
set_property(TARGET pvr.sledovanitv.cz PROPERTY CXX_STANDARD_REQUIRED ON)

include(CPack)
