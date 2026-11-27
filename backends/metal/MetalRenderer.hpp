#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <print>
#include <semaphore>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>
#include <MetalKit/MetalKit.hpp>

#include "core/World.hpp"
#include "core/Observer.hpp"
#include "MetalShaders.hpp"

// [0단계 & 4~9단계: Metal 백엔드 렌더러]
class MetalCubeRenderer {
public:
    // [9단계: CPU↔GPU 프레임 자원 격리 (In-flight Frame Isolation)]
    // GPU↔Display 출력 버퍼 격리인 CAMetalLayer 드로어블 풀(기본 3개)과는 별개 계층
    static constexpr std::size_t kMaxFramesInFlight = 3;

    explicit MetalCubeRenderer(MTL::Device* pDevice)
        : _pDevice(NS::RetainPtr(pDevice)),
          _pCommandQueue(NS::TransferPtr(_pDevice->newCommandQueue())),
          _inFlightSemaphore(kMaxFramesInFlight),
          _currentFrameIndex(0) {
        buildSpatialOntologyBuffers();
        buildPipelineAndPerceptualMapping();
        buildOcclusionResolver();
    }

    ~MetalCubeRenderer() = default;

    MetalCubeRenderer(const MetalCubeRenderer&) = delete("GPU 파이프라인 및 버퍼 리소스를 독점 소유하는 객체이므로 복사할 수 없습니다.");
    MetalCubeRenderer& operator=(const MetalCubeRenderer&) = delete("GPU 파이프라인 및 버퍼 리소스를 독점 소유하는 객체이므로 복사 대입할 수 없습니다.");

    void renderFrame(MTK::View* pView, const WorldState& world, const Observer& observer) {
        NS::SharedPtr<NS::AutoreleasePool> pPool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

        _inFlightSemaphore.acquire();

        MTL::Buffer* pCurrentUniformBuffer = _pUniformBuffers[_currentFrameIndex].get();
        _currentFrameIndex = (_currentFrameIndex + 1) % kMaxFramesInFlight;

        CGSize drawableSize = pView->drawableSize();
        float aspectRatio = (drawableSize.height > 0.0f)
                                ? static_cast<float>(drawableSize.width / drawableSize.height)
                                : 1.0f;

        FrameUniforms uniforms{
            .modelMatrix = world.computeModelMatrix(),
            .viewMatrix = observer.computeViewMatrix(),
            .projectionMatrix = observer.computeProjectionMatrix(aspectRatio, false),
            .lightDirWorld = world.lightDirectionWorld()
        };

        *static_cast<FrameUniforms*>(pCurrentUniformBuffer->contents()) = uniforms;

        MTL::CommandBuffer* pCmd = _pCommandQueue->commandBuffer();
        pCmd->addCompletedHandler([this](MTL::CommandBuffer* _) {
            _inFlightSemaphore.release();
        });

        MTL::RenderPassDescriptor* pRpd = pView->currentRenderPassDescriptor();
        if (pRpd) {
            MTL::RenderCommandEncoder* pEnc = pCmd->renderCommandEncoder(pRpd);

            pEnc->setRenderPipelineState(_pPipelineState.get());
            pEnc->setDepthStencilState(_pDepthState.get());

            // [4~5단계 경계: 후면 제거 (Back-face Culling)]
            pEnc->setFrontFacingWinding(MTL::WindingCounterClockwise);
            pEnc->setCullMode(MTL::CullModeBack);

            pEnc->setVertexBuffer(_pVertexBuffer.get(), 0, 0);
            pEnc->setVertexBuffer(pCurrentUniformBuffer, 0, 1);
            pEnc->setFragmentBuffer(pCurrentUniformBuffer, 0, 1);

            pEnc->drawIndexedPrimitives(
                MTL::PrimitiveTypeTriangle,
                kCubeIndices.size(),
                MTL::IndexTypeUInt16,
                _pIndexBuffer.get(),
                0
            );

            pEnc->endEncoding();
            pCmd->presentDrawable(pView->currentDrawable());
        }

        pCmd->commit();
    }

private:
    // [0.6단계: 메모리 토폴로지 정렬 및 자원 실체화 (Apple Silicon 통합 메모리 Shared 저장소)]
    void buildSpatialOntologyBuffers() {
        _pVertexBuffer = NS::TransferPtr(_pDevice->newBuffer(
            kCubeVertices.data(),
            sizeof(kCubeVertices),
            MTL::ResourceStorageModeShared
        ));
        _pIndexBuffer = NS::TransferPtr(_pDevice->newBuffer(
            kCubeIndices.data(),
            sizeof(kCubeIndices),
            MTL::ResourceStorageModeShared
        ));

        for (auto& uniformBuffer : _pUniformBuffers) {
            uniformBuffer = NS::TransferPtr(_pDevice->newBuffer(
                sizeof(FrameUniforms),
                MTL::ResourceStorageModeShared
            ));
        }
    }

    // [0.5단계 & 0.7단계: 데이터 ABI 및 불변 실행 상태(PSO) 사전 컴파일]
    void buildPipelineAndPerceptualMapping() {
        NS::Error* pError = nullptr;
        NS::String* pSource = NS::String::string(kMetalShaderSource, NS::StringEncoding::UTF8StringEncoding);
        NS::SharedPtr<MTL::Library> pLibrary = NS::TransferPtr(_pDevice->newLibrary(pSource, nullptr, &pError));
        if (!pLibrary) {
            std::println(stderr, "Shader compile error: {}", pError->localizedDescription()->utf8String());
            assert(false);
        }

        NS::SharedPtr<MTL::Function> pVertFn = NS::TransferPtr(
            pLibrary->newFunction(NS::String::string("vertexMain", NS::StringEncoding::UTF8StringEncoding))
        );
        NS::SharedPtr<MTL::Function> pFragFn = NS::TransferPtr(
            pLibrary->newFunction(NS::String::string("fragmentMain", NS::StringEncoding::UTF8StringEncoding))
        );

        MTL::VertexDescriptor* pVertDesc = MTL::VertexDescriptor::vertexDescriptor();
        pVertDesc->attributes()->object(0)->setFormat(MTL::VertexFormatFloat3);
        pVertDesc->attributes()->object(0)->setOffset(offsetof(Vertex, position));
        pVertDesc->attributes()->object(0)->setBufferIndex(0);
        pVertDesc->attributes()->object(1)->setFormat(MTL::VertexFormatFloat3);
        pVertDesc->attributes()->object(1)->setOffset(offsetof(Vertex, normal));
        pVertDesc->attributes()->object(1)->setBufferIndex(0);
        pVertDesc->attributes()->object(2)->setFormat(MTL::VertexFormatFloat3);
        pVertDesc->attributes()->object(2)->setOffset(offsetof(Vertex, color));
        pVertDesc->attributes()->object(2)->setBufferIndex(0);
        pVertDesc->layouts()->object(0)->setStride(sizeof(Vertex));

        NS::SharedPtr<MTL::RenderPipelineDescriptor> pPipeDesc = NS::TransferPtr(
            MTL::RenderPipelineDescriptor::alloc()->init()
        );
        pPipeDesc->setVertexFunction(pVertFn.get());
        pPipeDesc->setFragmentFunction(pFragFn.get());
        pPipeDesc->setVertexDescriptor(pVertDesc);
        pPipeDesc->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormatBGRA8Unorm_sRGB);
        pPipeDesc->setDepthAttachmentPixelFormat(MTL::PixelFormatDepth32Float);

        _pPipelineState = NS::TransferPtr(_pDevice->newRenderPipelineState(pPipeDesc.get(), &pError));
        if (!_pPipelineState) {
            std::println(stderr, "Pipeline compile error: {}", pError->localizedDescription()->utf8String());
            assert(false);
        }
    }

    // [6단계: 폐색 해결(Z-버퍼 깊이 판정) 상태 객체]
    void buildOcclusionResolver() {
        NS::SharedPtr<MTL::DepthStencilDescriptor> pDepthDesc = NS::TransferPtr(
            MTL::DepthStencilDescriptor::alloc()->init()
        );
        pDepthDesc->setDepthCompareFunction(MTL::CompareFunctionLess);
        pDepthDesc->setDepthWriteEnabled(true);
        _pDepthState = NS::TransferPtr(_pDevice->newDepthStencilState(pDepthDesc.get()));
    }

    NS::SharedPtr<MTL::Device> _pDevice;
    NS::SharedPtr<MTL::CommandQueue> _pCommandQueue;
    NS::SharedPtr<MTL::Buffer> _pVertexBuffer;
    NS::SharedPtr<MTL::Buffer> _pIndexBuffer;
    std::array<NS::SharedPtr<MTL::Buffer>, kMaxFramesInFlight> _pUniformBuffers;
    NS::SharedPtr<MTL::RenderPipelineState> _pPipelineState;
    NS::SharedPtr<MTL::DepthStencilState> _pDepthState;

    std::counting_semaphore<kMaxFramesInFlight> _inFlightSemaphore;
    std::size_t _currentFrameIndex;
};
