add_executable(Finder
    Finder.cpp
)

target_include_directories(Finder PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(Finder PRIVATE
    kernel32
    user32
    advapi32
    psapi
)