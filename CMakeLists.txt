cmake_minimum_required(VERSION 3.20)

project(SemanticCube LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC UTF-8 한글 주석 및 문자열 리터럴 인코딩 보호
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# OS별 순수 네이티브 백엔드 자동 활성화 (macOS=Metal, Windows=DX12+Vulkan, Linux=Vulkan)
if(APPLE)
    set(DEFAULT_ENABLE_VULKAN OFF)
else()
    set(DEFAULT_ENABLE_VULKAN ON)
endif()

option(ENABLE_METAL  "Build Metal backend (macOS native)"               ${APPLE})
option(ENABLE_VULKAN "Build Vulkan backend (Windows / Linux native)"    ${DEFAULT_ENABLE_VULKAN})
option(ENABLE_DX12   "Build Direct3D 12 backend (Windows native)"       ${WIN32})

# ==============================================================================
# 1. Metal 백엔드 (macOS 네이티브 — AppKit + MTK::View + metal-cpp)
# ==============================================================================
if(ENABLE_METAL AND APPLE)
    add_executable(metal_rotating_cube
        backends/metal/main_metal.cpp
    )

    target_compile_features(metal_rotating_cube PRIVATE cxx_std_26)

    target_include_directories(metal_rotating_cube PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/backends/metal
        ${CMAKE_CURRENT_SOURCE_DIR}/metal-cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/metal-cpp-extensions
    )

    target_link_libraries(metal_rotating_cube PRIVATE
        "-framework Metal"
        "-framework MetalKit"
        "-framework Cocoa"
        "-framework QuartzCore"
        "-framework AppKit"
        "-framework Foundation"
    )
endif()

# ==============================================================================
# 2. Vulkan 백엔드 (Windows Win32 · Linux X11 네이티브)
# ==============================================================================
if(ENABLE_VULKAN AND NOT APPLE)
    find_package(Vulkan REQUIRED)

    find_program(GLSLANG_VALIDATOR NAMES glslangValidator HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
    find_program(GLSLC_COMPILER NAMES glslc HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")

    set(VK_SHADER_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/backends/vulkan/shaders")
    set(VK_SHADER_OUT_DIR "${CMAKE_BINARY_DIR}/shaders")
    set(VK_VERT_SPV "${VK_SHADER_OUT_DIR}/cube.vert.spv")
    set(VK_FRAG_SPV "${VK_SHADER_OUT_DIR}/cube.frag.spv")

    if(GLSLANG_VALIDATOR)
        add_custom_command(
            OUTPUT "${VK_VERT_SPV}" "${VK_FRAG_SPV}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${VK_SHADER_OUT_DIR}"
            COMMAND "${GLSLANG_VALIDATOR}" -V "${VK_SHADER_SRC_DIR}/cube.vert" -o "${VK_VERT_SPV}"
            COMMAND "${GLSLANG_VALIDATOR}" -V "${VK_SHADER_SRC_DIR}/cube.frag" -o "${VK_FRAG_SPV}"
            DEPENDS "${VK_SHADER_SRC_DIR}/cube.vert" "${VK_SHADER_SRC_DIR}/cube.frag"
            COMMENT "Compiling Vulkan GLSL shaders to SPIR-V (glslangValidator)"
        )
    elseif(GLSLC_COMPILER)
        add_custom_command(
            OUTPUT "${VK_VERT_SPV}" "${VK_FRAG_SPV}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${VK_SHADER_OUT_DIR}"
            COMMAND "${GLSLC_COMPILER}" "${VK_SHADER_SRC_DIR}/cube.vert" -o "${VK_VERT_SPV}"
            COMMAND "${GLSLC_COMPILER}" "${VK_SHADER_SRC_DIR}/cube.frag" -o "${VK_FRAG_SPV}"
            DEPENDS "${VK_SHADER_SRC_DIR}/cube.vert" "${VK_SHADER_SRC_DIR}/cube.frag"
            COMMENT "Compiling Vulkan GLSL shaders to SPIR-V (glslc)"
        )
    else()
        message(FATAL_ERROR "Vulkan enabled, but neither glslangValidator nor glslc was found.")
    endif()

    add_custom_target(vulkan_shaders DEPENDS "${VK_VERT_SPV}" "${VK_FRAG_SPV}")

    if(WIN32)
        add_executable(vulkan_rotating_cube
            backends/vulkan/main_vulkan_win32.cpp
        )
        target_compile_definitions(vulkan_rotating_cube PRIVATE
            VK_USE_PLATFORM_WIN32_KHR
            WIN32_LEAN_AND_MEAN
            NOMINMAX
            UNICODE
            _UNICODE
        )
        target_link_libraries(vulkan_rotating_cube PRIVATE
            Vulkan::Vulkan
            user32
            gdi32
        )
    else()
        find_package(X11 REQUIRED)
        add_executable(vulkan_rotating_cube
            backends/vulkan/main_vulkan_x11.cpp
        )
        target_compile_definitions(vulkan_rotating_cube PRIVATE VK_USE_PLATFORM_XLIB_KHR)
        target_link_libraries(vulkan_rotating_cube PRIVATE
            Vulkan::Vulkan
            X11::X11
        )
    endif()

    add_dependencies(vulkan_rotating_cube vulkan_shaders)

    # Visual Studio 등 멀티-config 생성기에서도 실행 파일 옆 shaders/ 폴더에 SPIR-V 보장
    add_custom_command(TARGET vulkan_rotating_cube POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:vulkan_rotating_cube>/shaders"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${VK_VERT_SPV}" "${VK_FRAG_SPV}" "$<TARGET_FILE_DIR:vulkan_rotating_cube>/shaders/"
        COMMENT "Copying compiled SPIR-V shaders next to vulkan_rotating_cube"
    )

    target_include_directories(vulkan_rotating_cube PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/backends/vulkan
    )
endif()

# ==============================================================================
# 3. Direct3D 12 백엔드 (Windows 네이티브 — Win32 + DXGI + D3D12 + HLSL)
# ==============================================================================
if(ENABLE_DX12 AND WIN32)
    add_executable(dx12_rotating_cube
        backends/dx12/main_dx12.cpp
    )

    target_include_directories(dx12_rotating_cube PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/backends/dx12
    )

    target_compile_definitions(dx12_rotating_cube PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        UNICODE
        _UNICODE
    )

    target_link_libraries(dx12_rotating_cube PRIVATE
        d3d12
        dxgi
        d3dcompiler
        user32
        gdi32
    )
endif()
