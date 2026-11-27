#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#include <windows.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "VulkanRenderer.hpp"

namespace {

struct Win32AppState {
    std::unique_ptr<VulkanCubeRenderer> renderer;
    bool running = true;
    bool minimized = false;
};

std::filesystem::path resolveShaderDirectory() {
    wchar_t buffer[MAX_PATH] = {};
    DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        return std::filesystem::path(buffer).parent_path() / "shaders";
    }
    return std::filesystem::current_path() / "shaders";
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<Win32AppState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_SIZE:
        if (state) {
            std::uint32_t width = static_cast<std::uint32_t>(LOWORD(lParam));
            std::uint32_t height = static_cast<std::uint32_t>(HIWORD(lParam));
            state->minimized = (wParam == SIZE_MINIMIZED) || (width == 0) || (height == 0);
            if (state->renderer) {
                state->renderer->notifyFramebufferResized(width, height);
            }
        }
        return 0;
    case WM_CLOSE:
        if (state) {
            state->running = false;
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (state) {
            state->running = false;
        }
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

} // namespace

int main() {
    HINSTANCE hInstance = GetModuleHandleW(nullptr);
    constexpr const wchar_t* kClassName = L"SemanticCubeVulkanWindowClass";

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;

    if (!RegisterClassExW(&wc)) {
        std::fprintf(stderr, "RegisterClassExW failed.\n");
        return 1;
    }

    constexpr DWORD kStyle = WS_OVERLAPPEDWINDOW;
    RECT rect{ 0, 0, 640, 640 };
    AdjustWindowRectEx(&rect, kStyle, FALSE, 0);

    HWND hwnd = CreateWindowExW(
        0,
        kClassName,
        L"Semantic 3D Cube \u2014 Vulkan (Win32)",
        kStyle,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        rect.right - rect.left,
        rect.bottom - rect.top,
        nullptr,
        nullptr,
        hInstance,
        nullptr
    );

    if (!hwnd) {
        std::fprintf(stderr, "CreateWindowExW failed.\n");
        return 1;
    }

    Win32AppState state{};
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    RECT clientRect{};
    GetClientRect(hwnd, &clientRect);
    std::uint32_t width = static_cast<std::uint32_t>(clientRect.right - clientRect.left);
    std::uint32_t height = static_cast<std::uint32_t>(clientRect.bottom - clientRect.top);

    std::vector<const char*> instanceExtensions = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME
    };

    auto createSurface = [hInstance, hwnd](VkInstance instance) -> VkSurfaceKHR {
        VkWin32SurfaceCreateInfoKHR surfaceInfo{};
        surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        surfaceInfo.hinstance = hInstance;
        surfaceInfo.hwnd = hwnd;

        VkSurfaceKHR surface = VK_NULL_HANDLE;
        if (vkCreateWin32SurfaceKHR(instance, &surfaceInfo, nullptr, &surface) != VK_SUCCESS) {
            return VK_NULL_HANDLE;
        }
        return surface;
    };

    try {
        state.renderer = std::make_unique<VulkanCubeRenderer>(
            instanceExtensions,
            createSurface,
            width,
            height,
            resolveShaderDirectory()
        );

        WorldState world;
        Observer observer;

        MSG msg{};
        while (state.running) {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    state.running = false;
                    break;
                }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            if (!state.running) {
                break;
            }

            if (state.minimized) {
                WaitMessage();
                continue;
            }

            world.updateSimulation();
            state.renderer->renderFrame(world, observer);
        }

        state.renderer.reset();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Vulkan error: %s\n", e.what());
        state.renderer.reset();
        return 1;
    }

    return 0;
}
