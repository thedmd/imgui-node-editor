project(picojson)

add_library(picojson INTERFACE)

set(_picojson_Sources
    ${CMAKE_CURRENT_SOURCE_DIR}/picojson.h
)

target_include_directories(picojson INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_sources(picojson INTERFACE ${_picojson_Sources})

source_group("ThirdParty\\picojson" FILES ${_picojson_Sources})