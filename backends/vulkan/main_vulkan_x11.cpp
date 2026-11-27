#ifndef VK_USE_PLATFORM_XLIB_KHR
#define VK_USE_PLATFORM_XLIB_KHR
#endif
#include <vulkan/vulkan.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <limits.h>
#include <memory>
#include <string>
#include <vector>

#include "VulkanRenderer.hpp"

namespace {

std::filesystem::path resolveShaderDirectory() {
    char buffer[PATH_MAX] = {};
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len > 0) {
        buffer[len] = '\0';
        return std::filesystem::path(buffer).parent_path() / "shaders";
    }
    return std::filesystem::current_path() / "shaders";
}

} // namespace

int main() {
    // [0.1단계: X11 디스플레이 연결, 창 생성 및 이벤트 마스크·종료 프로토콜 확보]
    Display* display = XOpenDisplay(nullptr);
    if (!display) {
        std::fprintf(stderr, "Failed to open X11 display.\n");
        return 1;
    }

    int screen = DefaultScreen(display);
    std::uint32_t width = 640;
    std::uint32_t height = 640;

    Window window = XCreateSimpleWindow(
        display,
        RootWindow(display, screen),
        150,
        150,
        width,
        height,
        1,
        BlackPixel(display, screen),
        BlackPixel(display, screen)
    );

    XStoreName(display, window, "Semantic 3D Cube — Vulkan (X11)");
    XSelectInput(display, window, ExposureMask | StructureNotifyMask | KeyPressMask);

    Atom wmDeleteMessage = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, window, &wmDeleteMessage, 1);

    XMapWindow(display, window);
    XFlush(display);

    // [0.4단계: X11 Window와 Vulkan 디스플레이 표면(VkSurfaceKHR) 결합 팩토리]
    std::vector<const char*> instanceExtensions = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_XLIB_SURFACE_EXTENSION_NAME
    };

    auto createSurface = [display, window](VkInstance instance) -> VkSurfaceKHR {
        VkXlibSurfaceCreateInfoKHR surfaceInfo{};
        surfaceInfo.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
        surfaceInfo.dpy = display;
        surfaceInfo.window = window;

        VkSurfaceKHR surface = VK_NULL_HANDLE;
        if (vkCreateXlibSurfaceKHR(instance, &surfaceInfo, nullptr, &surface) != VK_SUCCESS) {
            return VK_NULL_HANDLE;
        }
        return surface;
    };

    int exitCode = 0;
    try {
        // [0단계 Bootstrap: Vulkan 렌더러 및 정적 실행 환경 계약 초기화]
        auto renderer = std::make_unique<VulkanCubeRenderer>(
            instanceExtensions,
            createSurface,
            width,
            height,
            resolveShaderDirectory()
        );

        // [1단계 & 3단계: 가상 세계 상태 및 관찰자 초기화]
        WorldState world;
        Observer observer;
        bool running = true;

        // [0.1단계 & 2~9단계: X11 이벤트 폴링 및 런타임 프레임 루프]
        while (running) {
            while (XPending(display) > 0) {
                XEvent event{};
                XNextEvent(display, &event);
                switch (event.type) {
                case ClientMessage:
                    if (static_cast<Atom>(event.xclient.data.l[0]) == wmDeleteMessage) {
                        running = false;
                    }
                    break;
                case ConfigureNotify: {
                    // [3.2 Renegotiation: X11 창 크기 변경 감지 및 스왑체인 재협상 요청]
                    auto newW = static_cast<std::uint32_t>(event.xconfigure.width);
                    auto newH = static_cast<std::uint32_t>(event.xconfigure.height);
                    if (newW != width || newH != height) {
                        width = newW;
                        height = newH;
                        renderer->notifyFramebufferResized(width, height);
                    }
                    break;
                }
                default:
                    break;
                }
            }

            if (!running) {
                break;
            }

            // [2단계: 경과 시간 기반 시간 전이 및 개체 상태 갱신]
            world.updateSimulation();
            // [3~9단계: 시점·사영·이산화·폐색·광학·동기화·표출 실행]
            renderer->renderFrame(world, observer);
        }

        // [3.2 Teardown: X11 창 파괴 전 Vulkan 자원 및 표면 계약 선행 해제]
        renderer.reset();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Vulkan error: %s\n", e.what());
        exitCode = 1;
    }

    // [3.2 Teardown: OS 창 및 X11 디스플레이 연결 해제]
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    return exitCode;
}
