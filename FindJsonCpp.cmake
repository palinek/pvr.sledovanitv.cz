find_path(JSONCPP_INCLUDE_DIRS json/json.h
  PATHS ${CMAKE_PREFIX_PATH}
  PATH_SUFFIXES jsoncpp)
find_library(JSONCPP_LIBRARIES jsoncpp
  PATHS ${CMAKE_PREFIX_PATH})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(JsonCpp REQUIRED_VARS JSONCPP_LIBRARIES JSONCPP_INCLUDE_DIRS)

mark_as_advanced(JSONCPP_INCLUDE_DIRS JSONCPP_LIBRARIES)
