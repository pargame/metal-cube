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
        auto renderer = std::make_unique<VulkanCubeRenderer>(
            instanceExtensions,
            createSurface,
            width,
            height,
            resolveShaderDirectory()
        );

        WorldState world;
        Observer observer;
        bool running = true;

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

            world.updateSimulation();
            renderer->renderFrame(world, observer);
        }

        renderer.reset();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Vulkan error: %s\n", e.what());
        exitCode = 1;
    }

    XDestroyWindow(display, window);
    XCloseDisplay(display);
    return exitCode;
}
