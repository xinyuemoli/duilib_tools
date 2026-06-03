add_library(HookDll SHARED
    HookDll.cpp
    HookDll.def
    common/ThreadEnum.cpp
    duilib/ModuleFinder.cpp
    hooks/MessageHandler.cpp
)

set_target_properties(HookDll PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

target_compile_options(HookDll PRIVATE /Zi /MP)
target_link_options(HookDll PRIVATE /DEBUG)

target_include_directories(HookDll PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
    F:/project/external32/include
)

target_link_libraries(HookDll PRIVATE
    kernel32
    user32
    psapi
    F:/project/external32/lib/detours.lib
)