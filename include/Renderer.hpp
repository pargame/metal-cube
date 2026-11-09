#pragma once

#include <cassert>
#include <cstddef>
#include <cstring>
#include <dispatch/dispatch.h>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>
#include <MetalKit/MetalKit.hpp>

#include "World.hpp"
#include "Observer.hpp"
#include "Shaders.hpp"

// [4단계 ~ 9단계: 차원 축소, 이산화, 폐색 해결, 광학 해석, 인지적 매핑, 시간 격리 및 물리 신호 표출]
// 가상 세계 상태(WorldState)와 관찰자(Observer) 정보를 입력받아
// GPU 하드웨어 파이프라인을 통해 최종 화면의 물리적 광신호로 변환하는 렌더러 클래스입니다.
class CubeRenderer {
public:
    // [9단계: 시간적 격리(Triple Buffering)]
    // CPU가 다음 프레임의 세계 상태를 기록하는 동안 GPU가 이전 프레임을 동시에 그릴 수 있도록
    // 3개의 독립된 프레임 버퍼 슬롯을 운용합니다.
    static constexpr int kMaxFramesInFlight = 3;

    explicit CubeRenderer(MTL::Device* pDevice)
        : _pDevice(pDevice->retain()),
          _currentFrameIndex(0) {
        _pCommandQueue = _pDevice->newCommandQueue();

        // 동시에 최대 3개의 프레임만 파이프라인에 진입하도록 제어하는 세마포어 생성
        _inFlightSemaphore = dispatch_semaphore_create(kMaxFramesInFlight);

        buildSpatialOntologyBuffers();
        buildPipelineAndPerceptualMapping();
        buildOcclusionResolver();
    }

    ~CubeRenderer() {
        _pVertexBuffer->release();
        _pIndexBuffer->release();
        for (int i = 0; i < kMaxFramesInFlight; ++i) {
            _pUniformBuffers[i]->release();
        }
        _pPipelineState->release();
        _pDepthState->release();
        _pCommandQueue->release();
        _pDevice->release();
    }

    // 매 프레임 시뮬레이션이 완료된 세계 상태(world)를 관찰자(observer) 시점에서 화면(pView)에 투사합니다.
    void renderFrame(MTK::View* pView, const WorldState& world, const Observer& observer) {
        NS::AutoreleasePool* pPool = NS::AutoreleasePool::alloc()->init();

        // [9단계: 시간적 격리 동기화 (Temporal Isolation Wait)]
        // GPU가 아직 사용 중인 과거 프레임의 Uniform 버퍼를 CPU가 덮어쓰는 경쟁 상태(Race Condition)를 막기 위해,
        // 가용 슬롯이 확보될 때까지 대기합니다.
        dispatch_semaphore_wait(_inFlightSemaphore, DISPATCH_TIME_FOREVER);

        MTL::Buffer* pCurrentUniformBuffer = _pUniformBuffers[_currentFrameIndex];
        _currentFrameIndex = (_currentFrameIndex + 1) % kMaxFramesInFlight;

        // [2단계 ~ 4단계: 기준 좌표계 전이 행렬 스냅샷 구성]
        // 현재 시점의 세계 배치(Model), 관찰자 시점(View), 차원 축소(Projection) 행렬을 각각 산출하여
        // 이번 프레임 전용 Uniform 버퍼 슬롯에 기록합니다.
        CGSize drawableSize = pView->drawableSize();
        float aspectRatio = (drawableSize.height > 0.0f)
                                ? static_cast<float>(drawableSize.width / drawableSize.height)
                                : 1.0f;

        FrameUniforms uniforms;
        uniforms.modelMatrix = world.computeModelMatrix();
        uniforms.viewMatrix = observer.computeViewMatrix();
        uniforms.projectionMatrix = observer.computeProjectionMatrix(aspectRatio);
        std::memcpy(uniforms.lightDirWorld, world.lightDirectionWorld(), sizeof(uniforms.lightDirWorld));

        std::memcpy(pCurrentUniformBuffer->contents(), &uniforms, sizeof(FrameUniforms));

        // GPU 명령 기록을 위한 커맨드 버퍼 생성
        MTL::CommandBuffer* pCmd = _pCommandQueue->commandBuffer();

        // [9단계: 시간적 격리 해제 콜백 등록]
        // GPU가 이번 프레임의 모든 렌더링 명령을 완료한 시점에 세마포어를 반환하여 슬롯을 재사용 가능하게 만듭니다.
        dispatch_semaphore_t semaphore = _inFlightSemaphore;
        pCmd->addCompletedHandler([semaphore](MTL::CommandBuffer*) {
            dispatch_semaphore_signal(semaphore);
        });

        MTL::RenderPassDescriptor* pRpd = pView->currentRenderPassDescriptor();
        if (pRpd) {
            MTL::RenderCommandEncoder* pEnc = pCmd->renderCommandEncoder(pRpd);

            // [4단계, 5단계, 7단계, 8단계 파이프라인 상태 연결]
            pEnc->setRenderPipelineState(_pPipelineState);

            // [6단계: 폐색 해결(깊이 판정) 상태 연결]
            pEnc->setDepthStencilState(_pDepthState);

            // [4단계: 가시 영역 선별 (Back-face Culling)]
            // 반시계 방향(CCW) 정점 순서로 정의된 면을 앞면으로 규정하고,
            // 관찰자의 시선과 반대 방향을 향하는 후면(Back-face)은 보이지 않으므로 광학 연산 전에 즉시 배제합니다.
            pEnc->setFrontFacingWinding(MTL::WindingCounterClockwise);
            pEnc->setCullMode(MTL::CullModeBack);

            // [1단계 기하 버퍼 및 2~4단계 프레임 상태 버퍼 바인딩]
            pEnc->setVertexBuffer(_pVertexBuffer, 0, 0);
            pEnc->setVertexBuffer(pCurrentUniformBuffer, 0, 1);
            pEnc->setFragmentBuffer(pCurrentUniformBuffer, 0, 1);

            // [5단계: 공간 이산화(래스터화) -> 6단계: 폐색 해결 -> 7단계: 광학 해석 -> 8단계: 인지 매핑 실행]
            // 36개 인덱스로 구성된 12개의 삼각형을 GPU 파이프라인에 투입합니다.
            pEnc->drawIndexedPrimitives(
                MTL::PrimitiveTypeTriangle,
                36,
                MTL::IndexTypeUInt16,
                _pIndexBuffer,
                0
            );

            pEnc->endEncoding();

            // [9단계: 프레임 버퍼 교체 및 물리적 신호 표출 (Signal Transduction & Presentation)]
            // 렌더링이 끝난 후면 버퍼(Back Buffer)를 디스플레이 주사율에 맞춰 전면 버퍼로 전환하여
            // 모니터 패널의 발광 소자가 실제 광자를 방출하도록 요청합니다.
            pCmd->presentDrawable(pView->currentDrawable());
        }

        pCmd->commit();
        pPool->release();
    }

private:
    // [1단계: 공간과 개체의 존재론적 데이터의 GPU 메모리 실체화]
    // CPU에 정의된 정육면체 정점·인덱스 데이터와 9단계 시간 격리를 위한 3중 Uniform 버퍼를 할당합니다.
    void buildSpatialOntologyBuffers() {
        _pVertexBuffer = _pDevice->newBuffer(
            kCubeVertices,
            sizeof(kCubeVertices),
            MTL::ResourceStorageModeShared
        );
        _pIndexBuffer = _pDevice->newBuffer(
            kCubeIndices,
            sizeof(kCubeIndices),
            MTL::ResourceStorageModeShared
        );

        for (int i = 0; i < kMaxFramesInFlight; ++i) {
            _pUniformBuffers[i] = _pDevice->newBuffer(
                sizeof(FrameUniforms),
                MTL::ResourceStorageModeShared
            );
        }
    }

    // [4단계, 5단계, 7단계, 8단계: 셰이더 컴파일, 래스터화 규격 및 인지적 색 공간 매핑 설정]
    void buildPipelineAndPerceptualMapping() {
        NS::Error* pError = nullptr;
        NS::String* pSource = NS::String::string(kShaderSource, NS::StringEncoding::UTF8StringEncoding);
        MTL::Library* pLibrary = _pDevice->newLibrary(pSource, nullptr, &pError);
        if (!pLibrary) {
            __builtin_printf("Shader compile error: %s\n", pError->localizedDescription()->utf8String());
            assert(false);
        }

        MTL::Function* pVertFn = pLibrary->newFunction(
            NS::String::string("vertexMain", NS::StringEncoding::UTF8StringEncoding)
        );
        MTL::Function* pFragFn = pLibrary->newFunction(
            NS::String::string("fragmentMain", NS::StringEncoding::UTF8StringEncoding)
        );

        // [1단계 정점 메모리 레이아웃 명세]
        MTL::VertexDescriptor* pVertDesc = MTL::VertexDescriptor::vertexDescriptor();
        // attribute(0): 개체 고유 공간 위치 (localPosition)
        pVertDesc->attributes()->object(0)->setFormat(MTL::VertexFormatFloat3);
        pVertDesc->attributes()->object(0)->setOffset(offsetof(Vertex, position));
        pVertDesc->attributes()->object(0)->setBufferIndex(0);
        // attribute(1): 개체 고유 공간 법선 (localNormal)
        pVertDesc->attributes()->object(1)->setFormat(MTL::VertexFormatFloat3);
        pVertDesc->attributes()->object(1)->setOffset(offsetof(Vertex, normal));
        pVertDesc->attributes()->object(1)->setBufferIndex(0);
        // attribute(2): 표면 고유 반사율 (albedo/color)
        pVertDesc->attributes()->object(2)->setFormat(MTL::VertexFormatFloat3);
        pVertDesc->attributes()->object(2)->setOffset(offsetof(Vertex, color));
        pVertDesc->attributes()->object(2)->setBufferIndex(0);

        pVertDesc->layouts()->object(0)->setStride(sizeof(Vertex));

        MTL::RenderPipelineDescriptor* pPipeDesc = MTL::RenderPipelineDescriptor::alloc()->init();
        pPipeDesc->setVertexFunction(pVertFn);
        pPipeDesc->setFragmentFunction(pFragFn);
        pPipeDesc->setVertexDescriptor(pVertDesc);

        // [8단계: 인지적 색 공간 매핑 및 양자화 (Perceptual Mapping & Quantization)]
        // 7단계(fragmentMain)에서 계산된 물리적 선형 빛 에너지(Linear Radiance)를
        // 인간 시각의 비선형 밝기 인지 특성(sRGB 감마 커브)으로 변환하고 채널당 8비트 정수(0~255)로 양자화합니다.
        pPipeDesc->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormatBGRA8Unorm_sRGB);

        // [6단계: 폐색 해결을 위한 32비트 부동소수점 깊이 버퍼 포맷 연결]
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

    // [6단계: 폐색 해결과 가시 표면 확정 (Occlusion Resolution & Surface Determination)]
    // 동일한 화소(시선) 위치에 여러 삼각형 조각이 겹칠 때, 관찰자로부터의 깊이(Z)가 더 작은(가까운)
    // 표면만 통과시키고(CompareFunctionLess) 깊이 기록을 갱신하여 공간의 전후 인과율을 보존합니다.
    void buildOcclusionResolver() {
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
    MTL::Buffer* _pUniformBuffers[kMaxFramesInFlight];
    MTL::RenderPipelineState* _pPipelineState;
    MTL::DepthStencilState* _pDepthState;

    dispatch_semaphore_t _inFlightSemaphore;
    int _currentFrameIndex;
};
