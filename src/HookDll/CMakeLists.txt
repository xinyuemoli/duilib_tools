add_library(HookDll SHARED
    HookDll.cpp
    HookDll.def
)

set_target_properties(HookDll PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

target_compile_options(HookDll PRIVATE /Zi)
target_link_options(HookDll PRIVATE /DEBUG)

target_include_directories(HookDll PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    F:/project/external32/include
)

target_link_libraries(HookDll PRIVATE
    kernel32
    user32
    psapi
    F:/project/external32/lib/detours.lib
)