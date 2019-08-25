cmake_minimum_required(VERSION 3.5)
project(pvr.sledovanitv.cz)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR})

find_package(Kodi REQUIRED)
find_package(JsonCpp REQUIRED)

include_directories(${KODI_INCLUDE_DIR}
                    ${JSONCPP_INCLUDE_DIRS})

set(DEPLIBS ${JSONCPP_LIBRARIES})


set(SLEDOVANITV_SOURCES src/client.cpp
  src/apimanager.cpp
  src/PVRIptvData.cpp)

set(SLEDOVANITV_HEADERS src/client.h
  src/apimanager.h
  src/CallLimiter.hh
  src/PVRIptvData.h)

if(WIN32)
  add_definitions("/wd4996")
endif()

build_addon(pvr.sledovanitv.cz SLEDOVANITV DEPLIBS)
set_property(TARGET pvr.sledovanitv.cz PROPERTY CXX_STANDARD 11)
set_property(TARGET pvr.sledovanitv.cz PROPERTY CXX_STANDARD_REQUIRED ON)

include(CPack)
