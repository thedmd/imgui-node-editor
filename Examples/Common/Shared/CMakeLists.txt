
set(_Shared_Sources
    Include/Builders.h
    Include/Drawing.h
    Include/Widgets.h
    Include/ScopeGuard.h

    Source/Builders.cpp
    Source/Drawing.cpp
    Source/Widgets.cpp
)

set(_Shared_NodeEditor_Sources
    ${CMAKE_SOURCE_DIR}/NodeEditor/Source/Shared/Interop.h
    ${CMAKE_SOURCE_DIR}/NodeEditor/Source/Shared/Math2D.h
    ${CMAKE_SOURCE_DIR}/NodeEditor/Source/Shared/Math2D.inl
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${_Shared_Sources})
source_group(TREE ${CMAKE_SOURCE_DIR}/NodeEditor/Source FILES ${_Shared_NodeEditor_Sources})

add_library(Shared STATIC ${_Shared_Sources} ${_Shared_NodeEditor_Sources})

target_include_directories(Shared PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Include
    ${CMAKE_SOURCE_DIR}/NodeEditor/Source/Shared
)

target_link_libraries(Shared PRIVATE ImGui NodeEditor)

set_property(TARGET Shared PROPERTY FOLDER "Examples/Common")