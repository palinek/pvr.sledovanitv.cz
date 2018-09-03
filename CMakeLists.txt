project(pvr.sledovanitv.cz)

cmake_minimum_required(VERSION 2.6)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR})

enable_language(CXX)

find_package(Kodi REQUIRED)
find_package(kodiplatform REQUIRED)
find_package(p8-platform REQUIRED)
find_package(JsonCpp REQUIRED)
find_package(OpenSSL REQUIRED) # OpenSSL libs are linked directly by kodi

include_directories(${kodiplatform_INCLUDE_DIRS}
                    ${p8-platform_INCLUDE_DIRS}
                    ${KODI_INCLUDE_DIR}
                    ${JSONCPP_INCLUDE_DIRS}
                    ${OPENSSL_INCLUDE_DIRS}
                    ${OPENSSL_INCLUDE_DIR})

set(DEPLIBS ${p8-platform_LIBRARIES}
            ${JSONCPP_LIBRARIES}
            ${OPENSSL_LIBRARIES})


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

include(CPack)
