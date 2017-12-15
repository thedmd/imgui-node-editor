
set(_NodeEditor_Sources
    Include/NodeEditor.h
    Source/NodeEditor.cpp

    Source/Editor.h
    Source/Editor.cpp

    Source/Shared/Interop.h
    Source/Shared/Math2D.h
    Source/Shared/Math2D.inl
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${_NodeEditor_Sources})

add_library(NodeEditor STATIC ${_NodeEditor_Sources})

target_include_directories(NodeEditor PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/Include)
#target_include_directories(NodeEditor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source/Shared)

target_link_libraries(NodeEditor PRIVATE ImGui picojson)
