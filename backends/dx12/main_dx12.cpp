#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <cstdio>
#include <exception>
#include <memory>

#include "Dx12Renderer.hpp"

namespace {

struct AppContext {
    WorldState world;
    Observer observer;
    std::unique_ptr<Dx12CubeRenderer> renderer;
    UINT pendingWidth = 640;
    UINT pendingHeight = 640;
    bool needsResize = false;
    bool minimized = false;
};

LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* ctx = reinterpret_cast<AppContext*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (message) {
        case WM_NCCREATE: {
            const auto* createStruct = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }
        case WM_SIZE: {
            if (ctx) {
                const UINT width = LOWORD(lParam);
                const UINT height = HIWORD(lParam);
                ctx->minimized = (wParam == SIZE_MINIMIZED) || (width == 0) || (height == 0);
                if (!ctx->minimized) {
                    ctx->pendingWidth = width;
                    ctx->pendingHeight = height;
                    ctx->needsResize = true;
                }
            }
            return 0;
        }
        case WM_KEYDOWN: {
            if (wParam == VK_ESCAPE) {
                DestroyWindow(hwnd);
            }
            return 0;
        }
        case WM_DESTROY: {
            PostQuitMessage(0);
            return 0;
        }
        default:
            return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

} // namespace

// [0.1단계: OS 프로세스·Win32 네이티브 창·이벤트 루프 및 프레임 구동]
int main() {
    try {
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        constexpr wchar_t kWindowClassName[] = L"SemanticCubeDx12WindowClass";
        constexpr wchar_t kWindowTitle[] = L"Semantic 3D Cube \u2014 Direct3D 12 (Windows)";
        constexpr LONG kInitialWidth = 640;
        constexpr LONG kInitialHeight = 640;

        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(WNDCLASSEXW);
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = windowProc;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.lpszClassName = kWindowClassName;

        if (!RegisterClassExW(&windowClass)) {
            std::fprintf(stderr, "RegisterClassExW failed.\n");
            return 1;
        }

        constexpr DWORD kWindowStyle = WS_OVERLAPPEDWINDOW;
        RECT windowRect{0, 0, kInitialWidth, kInitialHeight};
        AdjustWindowRectEx(&windowRect, kWindowStyle, FALSE, 0);

        AppContext ctx;
        HWND hwnd = CreateWindowExW(
            0,
            kWindowClassName,
            kWindowTitle,
            kWindowStyle,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            windowRect.right - windowRect.left,
            windowRect.bottom - windowRect.top,
            nullptr,
            nullptr,
            instance,
            &ctx
        );

        if (!hwnd) {
            std::fprintf(stderr, "CreateWindowExW failed.\n");
            return 1;
        }

        ctx.renderer = std::make_unique<Dx12CubeRenderer>(
            hwnd,
            static_cast<UINT>(kInitialWidth),
            static_cast<UINT>(kInitialHeight)
        );

        ShowWindow(hwnd, SW_SHOWDEFAULT);
        UpdateWindow(hwnd);

        MSG msg{};
        bool running = true;
        while (running) {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    running = false;
                    break;
                }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }

            if (!running) {
                break;
            }

            if (ctx.minimized) {
                WaitMessage();
                continue;
            }

            if (ctx.needsResize) {
                ctx.renderer->onResize(ctx.pendingWidth, ctx.pendingHeight);
                ctx.needsResize = false;
            }

            // [2단계: 시간 전이] -> [4~9단계: Direct3D 12 렌더링]
            ctx.world.updateSimulation();
            ctx.renderer->renderFrame(ctx.world, ctx.observer);
        }

        ctx.renderer.reset();
        return static_cast<int>(msg.wParam);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Fatal error: %s\n", e.what());
        return 1;
    }
}
