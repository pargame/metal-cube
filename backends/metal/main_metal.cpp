#include <memory>

#define NS_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#define MTK_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>
#include <AppKit/AppKit.hpp>
#include <MetalKit/MetalKit.hpp>

#include "core/World.hpp"
#include "core/Observer.hpp"
#include "MetalRenderer.hpp"

// 런타임 프레임 루프 조율자 (Runtime Frame Loop Orchestrator)
class ViewDelegate : public MTK::ViewDelegate {
public:
    explicit ViewDelegate(MTL::Device* pDevice)
        : _pRenderer(std::make_unique<MetalCubeRenderer>(pDevice)) {}

    ~ViewDelegate() override = default;

    void drawInMTKView(MTK::View* pView) override {
        _world.updateSimulation();
        _pRenderer->renderFrame(pView, _world, _observer);
    }

private:
    WorldState _world;
    Observer _observer;
    std::unique_ptr<MetalCubeRenderer> _pRenderer;
};

// [0.1단계 & 0.4단계: macOS AppKit 네이티브 창 및 MTK::View 출력 표면 계약]
class AppDelegate : public NS::ApplicationDelegate {
public:
    ~AppDelegate() override = default;

    void applicationWillFinishLaunching(NS::Notification* pNotification) override {
        auto* pApp = reinterpret_cast<NS::Application*>(pNotification->object());
        pApp->setActivationPolicy(NS::ActivationPolicy::ActivationPolicyRegular);
    }

    void applicationDidFinishLaunching(NS::Notification* pNotification) override {
        CGRect frame = (CGRect){ {150.0, 150.0}, {640.0, 640.0} };

        _pWindow = NS::TransferPtr(NS::Window::alloc()->init(
            frame,
            NS::WindowStyleMaskClosable | NS::WindowStyleMaskTitled | NS::WindowStyleMaskResizable,
            NS::BackingStoreBuffered,
            false
        ));

        _pDevice = NS::TransferPtr(MTL::CreateSystemDefaultDevice());

        _pMtkView = NS::TransferPtr(MTK::View::alloc()->init(frame, _pDevice.get()));
        _pMtkView->setColorPixelFormat(MTL::PixelFormatBGRA8Unorm_sRGB);
        _pMtkView->setClearColor(MTL::ClearColor::Make(0.08, 0.10, 0.14, 1.0));
        _pMtkView->setDepthStencilPixelFormat(MTL::PixelFormatDepth32Float);
        _pMtkView->setClearDepth(1.0);

        _pViewDelegate = std::make_unique<ViewDelegate>(_pDevice.get());
        _pMtkView->setDelegate(_pViewDelegate.get());

        _pWindow->setContentView(_pMtkView.get());
        _pWindow->setTitle(NS::String::string("Semantic 3D Cube — Metal (macOS)", NS::StringEncoding::UTF8StringEncoding));
        _pWindow->makeKeyAndOrderFront(nullptr);

        auto* pApp = reinterpret_cast<NS::Application*>(pNotification->object());
        pApp->activateIgnoringOtherApps(true);
    }

    bool applicationShouldTerminateAfterLastWindowClosed(NS::Application* _) override {
        return true;
    }

private:
    NS::SharedPtr<NS::Window> _pWindow;
    NS::SharedPtr<MTK::View> _pMtkView;
    NS::SharedPtr<MTL::Device> _pDevice;
    std::unique_ptr<ViewDelegate> _pViewDelegate;
};

int main() {
    NS::SharedPtr<NS::AutoreleasePool> pPool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    AppDelegate delegate;
    NS::Application* pApp = NS::Application::sharedApplication();
    pApp->setDelegate(&delegate);
    pApp->run();

    return 0;
}
