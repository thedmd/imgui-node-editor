project(stb_image)

add_library(stb_image INTERFACE)

set(_stb_image_Sources
    ${CMAKE_CURRENT_SOURCE_DIR}/stb_image.h
)

target_include_directories(stb_image INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_sources(stb_image INTERFACE ${_stb_image_Sources})

source_group("ThirdParty\\stb_image" FILES ${_stb_image_Sources})