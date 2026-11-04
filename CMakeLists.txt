cmake_minimum_required(VERSION 3.20)

project(MetalCube CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 바이너리를 빌드 폴더(build/debug 또는 build/release) 바로 아래로 출력
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# 실행 파일 타깃
add_executable(metal_rotating_cube metal_rotating_cube.cpp)

# 헤더 검색 경로 (Apple metal-cpp 순수 C++ 바인딩)
target_include_directories(metal_rotating_cube PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/metal-cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/metal-cpp-extensions
)

# macOS 네이티브 프레임워크 연결
target_link_libraries(metal_rotating_cube PRIVATE
    "-framework Metal"
    "-framework MetalKit"
    "-framework Cocoa"
    "-framework QuartzCore"
    "-framework AppKit"
    "-framework Foundation"
)
