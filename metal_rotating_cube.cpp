#include <cassert>
#include <cmath>
#include <chrono>

#define NS_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#define MTK_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>
#include <AppKit/AppKit.hpp>
#include <MetalKit/MetalKit.hpp>

// 1. 3D 수학 유틸리티 (4x4 행렬 - Metal Column-Major 호환)
struct Matrix4x4 {
    float m[16]; // m[col * 4 + row]

    static Matrix4x4 identity() {
        Matrix4x4 r = {};
        r.m[0] = 1.0f; r.m[5] = 1.0f; r.m[10] = 1.0f; r.m[15] = 1.0f;
        return r;
    }

    static Matrix4x4 rotationX(float rad) {
        Matrix4x4 r = identity();
        float c = std::cos(rad), s = std::sin(rad);
        r.m[5] = c;  r.m[6] = s;
        r.m[9] = -s; r.m[10] = c;
        return r;
    }

    static Matrix4x4 rotationY(float rad) {
        Matrix4x4 r = identity();
        float c = std::cos(rad), s = std::sin(rad);
        r.m[0] = c;  r.m[2] = -s;
        r.m[8] = s;  r.m[10] = c;
        return r;
    }

    static Matrix4x4 translation(float x, float y, float z) {
        Matrix4x4 r = identity();
        r.m[12] = x; r.m[13] = y; r.m[14] = z;
        return r;
    }

    // Metal 전용 원근 투영 (NDC Z: [0, 1])
    static Matrix4x4 perspective(float fovyRad, float aspect, float nearZ, float farZ) {
        Matrix4x4 r = {};
        float tanHalfFov = std::tan(fovyRad * 0.5f);
        r.m[0] = 1.0f / (aspect * tanHalfFov);
        r.m[5] = 1.0f / tanHalfFov;
        r.m[10] = farZ / (nearZ - farZ);
        r.m[11] = -1.0f;
        r.m[14] = (farZ * nearZ) / (nearZ - farZ);
        return r;
    }

    static Matrix4x4 multiply(const Matrix4x4& a, const Matrix4x4& b) {
        Matrix4x4 r = {};
        for (int c = 0; c < 4; ++c) {
            for (int row = 0; row < 4; ++row) {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k) {
                    sum += a.m[k * 4 + row] * b.m[c * 4 + k];
                }
                r.m[c * 4 + row] = sum;
            }
        }
        return r;
    }
};

struct Vertex {
    float position[3];
    float normal[3];
    float color[3];
};

struct Uniforms {
    Matrix4x4 modelViewProjection;
    Matrix4x4 model;
    float lightDir[4]; // 16바이트 정렬
};

// 2. 큐브 지오메트리 데이터 (각 면마다 4개씩 총 24개 정점 + 36개 인덱스)
const Vertex kCubeVertices[24] = {
    // 앞면 (Front, +Z) - 빨강 계열
    { {-0.7f, -0.7f,  0.7f}, { 0,  0,  1}, {0.95f, 0.25f, 0.25f} },
    { { 0.7f, -0.7f,  0.7f}, { 0,  0,  1}, {0.95f, 0.25f, 0.25f} },
    { { 0.7f,  0.7f,  0.7f}, { 0,  0,  1}, {0.95f, 0.25f, 0.25f} },
    { {-0.7f,  0.7f,  0.7f}, { 0,  0,  1}, {0.95f, 0.25f, 0.25f} },

    // 뒷면 (Back, -Z) - 청록 계열
    { { 0.7f, -0.7f, -0.7f}, { 0,  0, -1}, {0.15f, 0.85f, 0.85f} },
    { {-0.7f, -0.7f, -0.7f}, { 0,  0, -1}, {0.15f, 0.85f, 0.85f} },
    { {-0.7f,  0.7f, -0.7f}, { 0,  0, -1}, {0.15f, 0.85f, 0.85f} },
    { { 0.7f,  0.7f, -0.7f}, { 0,  0, -1}, {0.15f, 0.85f, 0.85f} },

    // 윗면 (Top, +Y) - 초록 계열
    { {-0.7f,  0.7f,  0.7f}, { 0,  1,  0}, {0.25f, 0.90f, 0.35f} },
    { { 0.7f,  0.7f,  0.7f}, { 0,  1,  0}, {0.25f, 0.90f, 0.35f} },
    { { 0.7f,  0.7f, -0.7f}, { 0,  1,  0}, {0.25f, 0.90f, 0.35f} },
    { {-0.7f,  0.7f, -0.7f}, { 0,  1,  0}, {0.25f, 0.90f, 0.35f} },

    // 아랫면 (Bottom, -Y) - 노랑 계열
    { {-0.7f, -0.7f, -0.7f}, { 0, -1,  0}, {0.95f, 0.85f, 0.20f} },
    { { 0.7f, -0.7f, -0.7f}, { 0, -1,  0}, {0.95f, 0.85f, 0.20f} },
    { { 0.7f, -0.7f,  0.7f}, { 0, -1,  0}, {0.95f, 0.85f, 0.20f} },
    { {-0.7f, -0.7f,  0.7f}, { 0, -1,  0}, {0.95f, 0.85f, 0.20f} },

    // 오른쪽면 (Right, +X) - 파랑 계열
    { { 0.7f, -0.7f,  0.7f}, { 1,  0,  0}, {0.25f, 0.45f, 0.95f} },
    { { 0.7f, -0.7f, -0.7f}, { 1,  0,  0}, {0.25f, 0.45f, 0.95f} },
    { { 0.7f,  0.7f, -0.7f}, { 1,  0,  0}, {0.25f, 0.45f, 0.95f} },
    { { 0.7f,  0.7f,  0.7f}, { 1,  0,  0}, {0.25f, 0.45f, 0.95f} },

    // 왼쪽면 (Left, -X) - 보라 계열
    { {-0.7f, -0.7f, -0.7f}, {-1,  0,  0}, {0.85f, 0.25f, 0.85f} },
    { {-0.7f, -0.7f,  0.7f}, {-1,  0,  0}, {0.85f, 0.25f, 0.85f} },
    { {-0.7f,  0.7f,  0.7f}, {-1,  0,  0}, {0.85f, 0.25f, 0.85f} },
    { {-0.7f,  0.7f, -0.7f}, {-1,  0,  0}, {0.85f, 0.25f, 0.85f} }
};

const uint16_t kCubeIndices[36] = {
    0, 1, 2,  2, 3, 0,       // 앞면
    4, 5, 6,  6, 7, 4,       // 뒷면
    8, 9, 10, 10, 11, 8,     // 윗면
    12, 13, 14, 14, 15, 12,  // 아랫면
    16, 17, 18, 18, 19, 16,  // 오른쪽면
    20, 21, 22, 22, 23, 20   // 왼쪽면
};

// 3. Metal 셰이더 소스 (MSL: 정점 변환 및 조명 계산)
const char* kShaderSource = R"(
    #include <metal_stdlib>
    using namespace metal;

    struct VertexInput {
        float3 position [[attribute(0)]];
        float3 normal   [[attribute(1)]];
        float3 color    [[attribute(2)]];
    };

    struct VertexOutput {
        float4 position [[position]];
        float3 normal;
        float3 color;
    };

    struct Uniforms {
        float4x4 modelViewProjection;
        float4x4 model;
        float4   lightDir;
    };

    vertex VertexOutput vertexMain(VertexInput in [[stage_in]],
                                  constant Uniforms& uniforms [[buffer(1)]]) {
        VertexOutput out;
        out.position = uniforms.modelViewProjection * float4(in.position, 1.0);
        out.normal = (uniforms.model * float4(in.normal, 0.0)).xyz;
        out.color = in.color;
        return out;
    }

    fragment float4 fragmentMain(VertexOutput in [[stage_in]],
                                 constant Uniforms& uniforms [[buffer(1)]]) {
        float3 N = normalize(in.normal);
        float3 L = normalize(uniforms.lightDir.xyz);

        // 람베르트 코사인 법칙 (디퓨즈 조명) + 은은한 앰비언트 광
        float diff = max(dot(N, L), 0.0);
        float3 ambient = float3(0.25);
        float3 litColor = in.color * (ambient + diff * 0.75);

        return float4(litColor, 1.0);
    }
)";

// 4. 렌더러 (Metal 파이프라인 관리 및 큐브 그리기)
class CubeRenderer {
public:
    CubeRenderer(MTL::Device* pDevice)
        : _pDevice(pDevice->retain()), _angle(0.0f) {
        _pCommandQueue = _pDevice->newCommandQueue();
        buildBuffers();
        buildPipeline();
        buildDepthStencil();
    }

    ~CubeRenderer() {
        _pVertexBuffer->release();
        _pIndexBuffer->release();
        _pUniformBuffer->release();
        _pPipelineState->release();
        _pDepthState->release();
        _pCommandQueue->release();
        _pDevice->release();
    }

    void draw(MTK::View* pView) {
        NS::AutoreleasePool* pPool = NS::AutoreleasePool::alloc()->init();

        // 1. 회전 애니메이션 업데이트 (매 프레임 각도 증가)
        _angle += 0.015f;

        // 2. MVP 행렬 계산
        Matrix4x4 modelRotX = Matrix4x4::rotationX(_angle * 0.8f);
        Matrix4x4 modelRotY = Matrix4x4::rotationY(_angle * 1.2f);
        Matrix4x4 model = Matrix4x4::multiply(modelRotY, modelRotX);

        Matrix4x4 view = Matrix4x4::translation(0.0f, 0.0f, -3.2f); // 카메라 거리

        CGSize size = pView->drawableSize();
        float aspect = (size.height > 0.0f) ? (float)size.width / (float)size.height : 1.0f;
        Matrix4x4 proj = Matrix4x4::perspective(60.0f * (3.14159265f / 180.0f), aspect, 0.1f, 100.0f);

        Matrix4x4 viewProj = Matrix4x4::multiply(proj, view);
        Matrix4x4 mvp = Matrix4x4::multiply(viewProj, model);

        Uniforms uniforms;
        uniforms.modelViewProjection = mvp;
        uniforms.model = model;
        uniforms.lightDir[0] = 0.5f;
        uniforms.lightDir[1] = 1.0f;
        uniforms.lightDir[2] = 0.8f;
        uniforms.lightDir[3] = 0.0f;

        memcpy(_pUniformBuffer->contents(), &uniforms, sizeof(Uniforms));

        // 3. 렌더링 커맨드 인코딩
        MTL::CommandBuffer* pCmd = _pCommandQueue->commandBuffer();
        MTL::RenderPassDescriptor* pRpd = pView->currentRenderPassDescriptor();

        if (pRpd) {
            MTL::RenderCommandEncoder* pEnc = pCmd->renderCommandEncoder(pRpd);
            pEnc->setRenderPipelineState(_pPipelineState);
            pEnc->setDepthStencilState(_pDepthState);
            pEnc->setCullMode(MTL::CullModeBack); // 뒷면 버리기 (Culling)

            pEnc->setVertexBuffer(_pVertexBuffer, 0, 0);
            pEnc->setVertexBuffer(_pUniformBuffer, 0, 1);
            pEnc->setFragmentBuffer(_pUniformBuffer, 0, 1);

            // 36개 인덱스로 삼각형 12개 그리기!
            pEnc->drawIndexedPrimitives(
                MTL::PrimitiveTypeTriangle,
                36,
                MTL::IndexTypeUInt16,
                _pIndexBuffer,
                0
            );

            pEnc->endEncoding();
            pCmd->presentDrawable(pView->currentDrawable());
        }

        pCmd->commit();
        pPool->release();
    }

private:
    void buildBuffers() {
        _pVertexBuffer = _pDevice->newBuffer(kCubeVertices, sizeof(kCubeVertices), MTL::ResourceStorageModeShared);
        _pIndexBuffer  = _pDevice->newBuffer(kCubeIndices, sizeof(kCubeIndices), MTL::ResourceStorageModeShared);
        _pUniformBuffer = _pDevice->newBuffer(sizeof(Uniforms), MTL::ResourceStorageModeShared);
    }

    void buildPipeline() {
        NS::Error* pError = nullptr;
        NS::String* pSource = NS::String::string(kShaderSource, NS::StringEncoding::UTF8StringEncoding);
        MTL::Library* pLibrary = _pDevice->newLibrary(pSource, nullptr, &pError);
        if (!pLibrary) {
            __builtin_printf("Shader compile error: %s\n", pError->localizedDescription()->utf8String());
            assert(false);
        }

        MTL::Function* pVertFn = pLibrary->newFunction(NS::String::string("vertexMain", NS::StringEncoding::UTF8StringEncoding));
        MTL::Function* pFragFn = pLibrary->newFunction(NS::String::string("fragmentMain", NS::StringEncoding::UTF8StringEncoding));

        // 정점 속성 포맷 정의
        MTL::VertexDescriptor* pVertDesc = MTL::VertexDescriptor::vertexDescriptor();
        // position
        pVertDesc->attributes()->object(0)->setFormat(MTL::VertexFormatFloat3);
        pVertDesc->attributes()->object(0)->setOffset(offsetof(Vertex, position));
        pVertDesc->attributes()->object(0)->setBufferIndex(0);
        // normal
        pVertDesc->attributes()->object(1)->setFormat(MTL::VertexFormatFloat3);
        pVertDesc->attributes()->object(1)->setOffset(offsetof(Vertex, normal));
        pVertDesc->attributes()->object(1)->setBufferIndex(0);
        // color
        pVertDesc->attributes()->object(2)->setFormat(MTL::VertexFormatFloat3);
        pVertDesc->attributes()->object(2)->setOffset(offsetof(Vertex, color));
        pVertDesc->attributes()->object(2)->setBufferIndex(0);

        pVertDesc->layouts()->object(0)->setStride(sizeof(Vertex));

        MTL::RenderPipelineDescriptor* pPipeDesc = MTL::RenderPipelineDescriptor::alloc()->init();
        pPipeDesc->setVertexFunction(pVertFn);
        pPipeDesc->setFragmentFunction(pFragFn);
        pPipeDesc->setVertexDescriptor(pVertDesc);
        pPipeDesc->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormatBGRA8Unorm_sRGB);
        pPipeDesc->setDepthAttachmentPixelFormat(MTL::PixelFormatDepth32Float);

        _pPipelineState = _pDevice->newRenderPipelineState(pPipeDesc, &pError);
        if (!_pPipelineState) {
            __builtin_printf("Pipeline compile error: %s\n", pError->localizedDescription()->utf8String());
            assert(false);
        }

        pVertFn->release();
        pFragFn->release();
        pPipeDesc->release();
        pLibrary->release();
    }

    void buildDepthStencil() {
        MTL::DepthStencilDescriptor* pDepthDesc = MTL::DepthStencilDescriptor::alloc()->init();
        pDepthDesc->setDepthCompareFunction(MTL::CompareFunctionLess);
        pDepthDesc->setDepthWriteEnabled(true);
        _pDepthState = _pDevice->newDepthStencilState(pDepthDesc);
        pDepthDesc->release();
    }

    MTL::Device* _pDevice;
    MTL::CommandQueue* _pCommandQueue;
    MTL::Buffer* _pVertexBuffer;
    MTL::Buffer* _pIndexBuffer;
    MTL::Buffer* _pUniformBuffer;
    MTL::RenderPipelineState* _pPipelineState;
    MTL::DepthStencilState* _pDepthState;
    float _angle;
};

// 5. MetalKit 뷰 델리게이트
class ViewDelegate : public MTK::ViewDelegate {
public:
    ViewDelegate(MTL::Device* pDevice) : _pRenderer(new CubeRenderer(pDevice)) {}
    virtual ~ViewDelegate() override { delete _pRenderer; }
    virtual void drawInMTKView(MTK::View* pView) override { _pRenderer->draw(pView); }
private:
    CubeRenderer* _pRenderer;
};

// 6. Cocoa 애플리케이션 델리게이트 (순수 C++로 창 생성)
class AppDelegate : public NS::ApplicationDelegate {
public:
    ~AppDelegate() {
        _pMtkView->release();
        _pWindow->release();
        _pDevice->release();
        delete _pViewDelegate;
    }

    virtual void applicationWillFinishLaunching(NS::Notification* pNotification) override {
        NS::Application* pApp = reinterpret_cast<NS::Application*>(pNotification->object());
        pApp->setActivationPolicy(NS::ActivationPolicy::ActivationPolicyRegular);
    }

    virtual void applicationDidFinishLaunching(NS::Notification* pNotification) override {
        CGRect frame = (CGRect){ {150.0, 150.0}, {640.0, 640.0} };

        _pWindow = NS::Window::alloc()->init(
            frame,
            NS::WindowStyleMaskClosable | NS::WindowStyleMaskTitled | NS::WindowStyleMaskResizable,
            NS::BackingStoreBuffered,
            false
        );

        _pDevice = MTL::CreateSystemDefaultDevice();

        _pMtkView = MTK::View::alloc()->init(frame, _pDevice);
        _pMtkView->setColorPixelFormat(MTL::PixelFormatBGRA8Unorm_sRGB);
        _pMtkView->setClearColor(MTL::ClearColor::Make(0.08, 0.10, 0.14, 1.0)); // 차분한 다크 네이비 배경
        _pMtkView->setDepthStencilPixelFormat(MTL::PixelFormatDepth32Float);
        _pMtkView->setClearDepth(1.0);

        _pViewDelegate = new ViewDelegate(_pDevice);
        _pMtkView->setDelegate(_pViewDelegate);

        _pWindow->setContentView(_pMtkView);
        _pWindow->setTitle(NS::String::string("Metal-CPP Rotating 3D Cube", NS::StringEncoding::UTF8StringEncoding));
        _pWindow->makeKeyAndOrderFront(nullptr);

        NS::Application* pApp = reinterpret_cast<NS::Application*>(pNotification->object());
        pApp->activateIgnoringOtherApps(true);
    }

    virtual bool applicationShouldTerminateAfterLastWindowClosed(NS::Application*) override {
        return true;
    }

private:
    NS::Window* _pWindow = nullptr;
    MTK::View* _pMtkView = nullptr;
    MTL::Device* _pDevice = nullptr;
    ViewDelegate* _pViewDelegate = nullptr;
};

// 메인 엔트리 포인트
int main() {
    NS::AutoreleasePool* pPool = NS::AutoreleasePool::alloc()->init();

    AppDelegate delegate;
    NS::Application* pApp = NS::Application::sharedApplication();
    pApp->setDelegate(&delegate);
    pApp->run();

    pPool->release();
    return 0;
}
