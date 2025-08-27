find_path(NLOHMANNJSON_INCLUDE_DIR nlohmann/json.hpp
  PATHS ${CMAKE_PREFIX_PATH})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(JsonCpp REQUIRED_VARS NLOHMANNJSON_INCLUDE_DIR)

mark_as_advanced(NLOHMANNJSON_INCLUDE_DIR)
