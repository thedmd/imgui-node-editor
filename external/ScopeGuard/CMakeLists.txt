project(ScopeGuard)

add_library(ScopeGuard INTERFACE)

set(_ScopeGuard_Sources
    ${CMAKE_CURRENT_SOURCE_DIR}/ScopeGuard.h
)

target_include_directories(ScopeGuard INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_sources(ScopeGuard INTERFACE ${_ScopeGuard_Sources})

source_group("ThirdParty\\ScopeGuard" FILES ${_ScopeGuard_Sources})