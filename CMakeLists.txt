project(pvr.sledovanitv)

cmake_minimum_required(VERSION 2.6)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR})

enable_language(CXX)

find_package(kodi REQUIRED)
find_package(kodiplatform REQUIRED)
find_package(platform REQUIRED)
find_package(JsonCpp REQUIRED)

include_directories(${kodiplatform_INCLUDE_DIRS}
                    ${platform_INCLUDE_DIRS}
                    ${KODI_INCLUDE_DIR}
                    ${JSONCPP_INCLUDE_DIRS})

set(DEPLIBS ${platform_LIBRARIES}
            ${JSONCPP_LIBRARIES})


set(SLEDOVANITV_SOURCES src/client.cpp
		 src/apimanager.cpp
                 src/PVRIptvData.cpp)

build_addon(pvr.sledovanitv SLEDOVANITV DEPLIBS)

include(CPack)
