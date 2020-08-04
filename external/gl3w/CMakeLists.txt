
set(_gl3w_Sources
    Include/GL/gl3w.h
    Include/GL/glcorearb.h
    Source/gl3w.c
)

source_group("" FILES ${_gl3w_Sources})

add_library(gl3w STATIC ${_gl3w_Sources})

target_include_directories(gl3w PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Include)

if (UNIX AND (NOT APPLE))
    # Linux: GL is required to pull glXGetProcAddress
    target_link_libraries(gl3w PRIVATE GL dl)
endif()

set_property(TARGET gl3w PROPERTY FOLDER "ThirdParty")
