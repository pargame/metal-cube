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

#include "World.hpp"
#include "Observer.hpp"
#include "Renderer.hpp"

// 런타임 프레임 루프 조율자 (Runtime Frame Loop Orchestrator)
// 디스플레이 주사 주기에 맞추어 호출되며,
// [1~2단계: 세계 상태의 시간 전이(Simulation)]와 [3~9단계: 관찰 및 화면 표출(Visualization)]을
// 독립된 단계로 명확히 구분하여 실행합니다.
class ViewDelegate : public MTK::ViewDelegate {
public:
    explicit ViewDelegate(MTL::Device* pDevice)
        : _pRenderer(std::make_unique<CubeRenderer>(pDevice)) {}

    ~ViewDelegate() override = default;

    void drawInMTKView(MTK::View* pView) override {
        // [1단계 & 2단계: 공간 상태의 시간 전이]
        // 화면 렌더링 여부나 방식에 종속되지 않고, 경과 시간(dt)에 따라 가상 세계의 상태를 먼저 갱신합니다.
        _world.updateSimulation();

        // [3단계 ~ 9단계: 관찰자 기준 시점 변환, 차원 축소, 이산화, 폐색 해결, 광학 해석, 인지 매핑 및 신호 표출]
        // 갱신된 세계 상태(_world)를 관찰자(_observer)의 시선으로 관측하여 화면(pView)의 물리적 화소로 변환합니다.
        _pRenderer->renderFrame(pView, _world, _observer);
    }

private:
    WorldState _world;                         // 1~2단계: 가상 공간의 존재론적 데이터 및 시간 전이 상태
    Observer _observer;                        // 3~4단계: 공간을 관측하는 관찰자(카메라) 및 차원 축소 규격
    std::unique_ptr<CubeRenderer> _pRenderer;  // 4~9단계: GPU 하드웨어 렌더링 파이프라인 및 동기화 표출기
};

// 운영체제 창(Window) 및 디스플레이 출력 표면(MTK::View) 초기화 델리게이트
// 가상 공간의 결과가 최종적으로 도달할 물리적 화면 영역(Screen Space)과 버퍼 포맷을 준비합니다.
class AppDelegate : public NS::ApplicationDelegate {
public:
    // NS::SharedPtr와 std::unique_ptr를 통한 완전한 RAII 관리로 수동 해제 코드가 불필요합니다.
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

        // [5단계 & 6단계 & 8단계 & 9단계: 화면 공간(Screen Space) 화소 격자 및 하드웨어 버퍼 규격 설정]
        _pMtkView = NS::TransferPtr(MTK::View::alloc()->init(frame, _pDevice.get()));
        // 8단계: 인지적 색 공간 매핑(sRGB 감마) 및 8비트 양자화 렌더 타겟 포맷 지정
        _pMtkView->setColorPixelFormat(MTL::PixelFormatBGRA8Unorm_sRGB);
        _pMtkView->setClearColor(MTL::ClearColor::Make(0.08, 0.10, 0.14, 1.0));
        // 6단계: 폐색 해결(Z-버퍼)을 위한 32비트 부동소수점 깊이 버퍼 포맷 및 초기 깊이(1.0 = 가장 먼 곳) 지정
        _pMtkView->setDepthStencilPixelFormat(MTL::PixelFormatDepth32Float);
        _pMtkView->setClearDepth(1.0);

        _pViewDelegate = std::make_unique<ViewDelegate>(_pDevice.get());
        _pMtkView->setDelegate(_pViewDelegate.get());

        _pWindow->setContentView(_pMtkView.get());
        _pWindow->setTitle(NS::String::string("Metal-CPP Rotating 3D Cube", NS::StringEncoding::UTF8StringEncoding));
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
