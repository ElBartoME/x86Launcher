set(CMAKE_TOOLCHAIN_FILE "$ENV{USERPROFILE}/.dos/toolchain-djgpp.cmake")
cmake_minimum_required(VERSION 3.7)

# Skip compiler tests - DJGPP cross-compiler can't run test programs on Windows
set(CMAKE_C_COMPILER_WORKS 1 CACHE BOOL "" FORCE)
set(CMAKE_CXX_COMPILER_WORKS 1 CACHE BOOL "" FORCE)
set(CMAKE_C_COMPILER_FORCED 1 CACHE BOOL "" FORCE)

project(dos-app)
set(CMAKE_GENERATOR Ninja)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)
add_compile_options(-Wall -std=gnu99 -save-temps)
file(GLOB sources CONFIGURE_DEPENDS "src/*.c" "src/*.cpp" "src/*.h" "src/**/*.c" "src/**/*.cpp" "src/**/*.h")
add_executable(main ${sources})
add_custom_target(assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/assets
    ${CMAKE_BINARY_DIR}/assets
)
add_dependencies(main assets)
target_link_libraries(main m)