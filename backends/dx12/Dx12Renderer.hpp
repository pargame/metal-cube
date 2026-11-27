#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <array>
#include <cassert>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <stdexcept>

#include "core/FrameUniforms.hpp"
#include "core/Observer.hpp"
#include "core/World.hpp"
#include "Dx12Shaders.hpp"

// [0단계 & 4~9단계: Direct3D 12 백엔드 렌더러]
class Dx12CubeRenderer {
public:
    // [9단계: CPU↔GPU 프레임 자원 격리 및 DXGI Flip-Model 스왑체인 버퍼 수]
    static constexpr UINT kFrameCount = 3;
    // Direct3D 12 Constant Buffer는 256바이트 경계 정렬이 필수
    static constexpr std::size_t kAlignedUniformSize = (sizeof(FrameUniforms) + 255u) & ~std::size_t{255u};
    static_assert(kAlignedUniformSize == 256, "Aligned FrameUniforms slot must be 256 bytes for DX12 CBV.");

    // DXGI Flip-Model 스왑체인은 비-sRGB 백버퍼로 생성하고, RTV 생성 시 _SRGB 뷰 포맷을 지정해야 함
    static constexpr DXGI_FORMAT kSwapChainFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
    static constexpr DXGI_FORMAT kRenderTargetViewFormat = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    static constexpr DXGI_FORMAT kDepthStencilFormat = DXGI_FORMAT_D32_FLOAT;
    static constexpr std::array<float, 4> kClearColor = {0.08f, 0.10f, 0.14f, 1.0f};

    // [계층 A. 정적 환경 계약 (Bootstrap — 실행 시 1회)]
    explicit Dx12CubeRenderer(HWND hwnd, UINT width, UINT height)
        : _width(width > 0 ? width : 1),
          _height(height > 0 ? height : 1) {
        createDevice();
        createCommandChannel();
        createSwapChainAndHeaps(hwnd);
        createRenderTargetViews();
        createDepthStencilBuffer();
        createRootSignatureAndPipeline();
        createSyncObjects();
        createGeometryAndUniformBuffers();
    }

    // [Teardown: GPU 완료 대기 후 매핑·이벤트·ComPtr 자원 역순 해제]
    ~Dx12CubeRenderer() {
        try {
            flushGpu();
        } catch (...) {
        }

        if (_uniformBuffer && _uniformMappedData) {
            _uniformBuffer->Unmap(0, nullptr);
            _uniformMappedData = nullptr;
        }
        if (_fenceEvent) {
            CloseHandle(_fenceEvent);
            _fenceEvent = nullptr;
        }
    }

    Dx12CubeRenderer(const Dx12CubeRenderer&) = delete;
    Dx12CubeRenderer& operator=(const Dx12CubeRenderer&) = delete;

    // [계층 B. 동적 재협상 (Renegotiation — 창 크기 변경 시 출력 표면·RTV·DSV 재생성)]
    void onResize(UINT width, UINT height) {
        if (width == 0 || height == 0) {
            return;
        }
        if (width == _width && height == _height) {
            return;
        }

        flushGpu();

        for (auto& rt : _renderTargets) {
            rt.Reset();
        }
        _depthStencilBuffer.Reset();

        throwIfFailed(
            _swapChain->ResizeBuffers(kFrameCount, width, height, kSwapChainFormat, 0),
            "IDXGISwapChain3::ResizeBuffers"
        );

        _width = width;
        _height = height;
        _frameIndex = _swapChain->GetCurrentBackBufferIndex();

        createRenderTargetViews();
        createDepthStencilBuffer();
    }

    // [계층 C. 런타임 프레임 루프 (Per-Frame: 4~9단계 실행)]
    void renderFrame(const WorldState& world, const Observer& observer) {
        // [0.8 & 9단계] 현재 프레임 슬롯의 이전 GPU 작업 완료를 대기해 명령 할당자 및 상수 버퍼 경합 차단
        waitForFenceValue(_fenceValues[_frameIndex]);

        const float aspectRatio = (_height > 0)
                                      ? static_cast<float>(_width) / static_cast<float>(_height)
                                      : 1.0f;

        // [2~4단계] CPU→GPU 프레임 상수(FrameUniforms) 갱신 (D3D12는 NDC +Y up이므로 flipY = false)
        const FrameUniforms uniforms{
            world.computeModelMatrix(),
            observer.computeViewMatrix(),
            observer.computeProjectionMatrix(aspectRatio, false),
            world.lightDirectionWorld()
        };

        std::memcpy(
            _uniformMappedData + static_cast<std::size_t>(_frameIndex) * kAlignedUniformSize,
            &uniforms,
            sizeof(FrameUniforms)
        );

        auto& allocator = _commandAllocators[_frameIndex];
        throwIfFailed(allocator->Reset(), "ID3D12CommandAllocator::Reset");
        throwIfFailed(_commandList->Reset(allocator.Get(), _pipelineState.Get()), "ID3D12GraphicsCommandList::Reset");

        // [0.8단계: 명시적 자원 상태 전환 PRESENT -> RENDER_TARGET]
        transitionResource(
            _renderTargets[_frameIndex].Get(),
            D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_RENDER_TARGET
        );

        // [5단계: 뷰포트 및 시저 영역 설정]
        const D3D12_VIEWPORT viewport{
            0.0f,
            0.0f,
            static_cast<float>(_width),
            static_cast<float>(_height),
            0.0f,
            1.0f
        };
        const D3D12_RECT scissorRect{
            0,
            0,
            static_cast<LONG>(_width),
            static_cast<LONG>(_height)
        };
        _commandList->RSSetViewports(1, &viewport);
        _commandList->RSSetScissorRects(1, &scissorRect);

        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = _rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtvHandle.ptr += static_cast<SIZE_T>(_frameIndex) * _rtvDescriptorSize;
        const D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = _dsvHeap->GetCPUDescriptorHandleForHeapStart();

        _commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
        _commandList->ClearRenderTargetView(rtvHandle, kClearColor.data(), 0, nullptr);
        _commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

        _commandList->SetGraphicsRootSignature(_rootSignature.Get());
        const D3D12_GPU_VIRTUAL_ADDRESS cbvAddress =
            _uniformBuffer->GetGPUVirtualAddress() + static_cast<UINT64>(_frameIndex) * kAlignedUniformSize;
        _commandList->SetGraphicsRootConstantBufferView(0, cbvAddress);

        _commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        _commandList->IASetVertexBuffers(0, 1, &_vertexBufferView);
        _commandList->IASetIndexBuffer(&_indexBufferView);
        _commandList->DrawIndexedInstanced(static_cast<UINT>(kCubeIndices.size()), 1, 0, 0, 0);

        // [0.8단계: 명시적 자원 상태 전환 RENDER_TARGET -> PRESENT]
        transitionResource(
            _renderTargets[_frameIndex].Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PRESENT
        );

        throwIfFailed(_commandList->Close(), "ID3D12GraphicsCommandList::Close");

        ID3D12CommandList* commandLists[] = {_commandList.Get()};
        _commandQueue->ExecuteCommandLists(1, commandLists);

        // [9단계: GPU↔Display 출력 버퍼 교체 (VSync 동기화)]
        throwIfFailed(_swapChain->Present(1, 0), "IDXGISwapChain3::Present");

        const UINT64 signalValue = _nextFenceValue++;
        throwIfFailed(_commandQueue->Signal(_fence.Get(), signalValue), "ID3D12CommandQueue::Signal");
        _fenceValues[_frameIndex] = signalValue;
        _frameIndex = _swapChain->GetCurrentBackBufferIndex();
    }

    void flushGpu() {
        if (!_commandQueue || !_fence) {
            return;
        }
        const UINT64 fenceToWaitFor = _nextFenceValue++;
        throwIfFailed(_commandQueue->Signal(_fence.Get(), fenceToWaitFor), "ID3D12CommandQueue::Signal (flushGpu)");
        waitForFenceValue(fenceToWaitFor);
        _fenceValues.fill(fenceToWaitFor);
    }

private:
    static void throwIfFailed(HRESULT hr, const char* context) {
        if (FAILED(hr)) {
            char buffer[256];
            std::snprintf(
                buffer,
                sizeof(buffer),
                "Direct3D 12 error in %s (HRESULT = 0x%08X)",
                context,
                static_cast<unsigned int>(hr)
            );
            throw std::runtime_error(buffer);
        }
    }

    void transitionResource(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = resource;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter = after;
        _commandList->ResourceBarrier(1, &barrier);
    }

    void waitForFenceValue(UINT64 fenceValue) {
        if (fenceValue != 0 && _fence->GetCompletedValue() < fenceValue) {
            throwIfFailed(_fence->SetEventOnCompletion(fenceValue, _fenceEvent), "ID3D12Fence::SetEventOnCompletion");
            WaitForSingleObjectEx(_fenceEvent, INFINITE, FALSE);
        }
    }

    // [0.2단계: 연산 주체(GPU) 탐색 및 논리 장치 획득]
    void createDevice() {
        UINT dxgiFactoryFlags = 0;
#if defined(_DEBUG)
        {
            Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
                debugController->EnableDebugLayer();
                dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
            }
        }
#endif

        throwIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&_factory)), "CreateDXGIFactory2");

        Microsoft::WRL::ComPtr<IDXGIAdapter1> selectedAdapter;
        for (UINT adapterIndex = 0;; ++adapterIndex) {
            Microsoft::WRL::ComPtr<IDXGIAdapter1> candidate;
            if (_factory->EnumAdapterByGpuPreference(
                    adapterIndex,
                    DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                    IID_PPV_ARGS(&candidate)
                ) == DXGI_ERROR_NOT_FOUND) {
                break;
            }

            DXGI_ADAPTER_DESC1 desc{};
            candidate->GetDesc1(&desc);
            if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
                continue;
            }

            if (SUCCEEDED(D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr))) {
                selectedAdapter = candidate;
                break;
            }
        }

        if (!selectedAdapter) {
            throwIfFailed(_factory->EnumWarpAdapter(IID_PPV_ARGS(&selectedAdapter)), "IDXGIFactory4::EnumWarpAdapter");
        }

        throwIfFailed(
            D3D12CreateDevice(selectedAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&_device)),
            "D3D12CreateDevice"
        );
    }

    // [0.3단계: 비동기 명령 채널 개설]
    void createCommandChannel() {
        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        queueDesc.NodeMask = 0;

        throwIfFailed(_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&_commandQueue)), "CreateCommandQueue");

        for (UINT i = 0; i < kFrameCount; ++i) {
            throwIfFailed(
                _device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&_commandAllocators[i])),
                "CreateCommandAllocator"
            );
        }

        throwIfFailed(
            _device->CreateCommandList(
                0,
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                _commandAllocators[0].Get(),
                nullptr,
                IID_PPV_ARGS(&_commandList)
            ),
            "CreateCommandList"
        );
        throwIfFailed(_commandList->Close(), "ID3D12GraphicsCommandList::Close (initial)");
    }

    // [0.4단계: OS 디스플레이 표면 결합 및 출력 순환 버퍼(SwapChain) 구성]
    void createSwapChainAndHeaps(HWND hwnd) {
        DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
        swapChainDesc.Width = _width;
        swapChainDesc.Height = _height;
        swapChainDesc.Format = kSwapChainFormat;
        swapChainDesc.Stereo = FALSE;
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.SampleDesc.Quality = 0;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.BufferCount = kFrameCount;
        swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
        swapChainDesc.Flags = 0;

        Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain1;
        throwIfFailed(
            _factory->CreateSwapChainForHwnd(_commandQueue.Get(), hwnd, &swapChainDesc, nullptr, nullptr, &swapChain1),
            "CreateSwapChainForHwnd"
        );
        throwIfFailed(_factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER), "MakeWindowAssociation");
        throwIfFailed(swapChain1.As(&_swapChain), "IDXGISwapChain1::As(IDXGISwapChain3)");

        _frameIndex = _swapChain->GetCurrentBackBufferIndex();

        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
        rtvHeapDesc.NumDescriptors = kFrameCount;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        throwIfFailed(_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&_rtvHeap)), "CreateDescriptorHeap (RTV)");
        _rtvDescriptorSize = _device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
        dsvHeapDesc.NumDescriptors = 1;
        dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        throwIfFailed(_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&_dsvHeap)), "CreateDescriptorHeap (DSV)");
    }

    // [0.4 & 8단계: 백버퍼에 sRGB RTV를 결합해 하드웨어 감마 인코딩 활성화]
    void createRenderTargetViews() {
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = _rtvHeap->GetCPUDescriptorHandleForHeapStart();

        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
        rtvDesc.Format = kRenderTargetViewFormat;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        rtvDesc.Texture2D.MipSlice = 0;
        rtvDesc.Texture2D.PlaneSlice = 0;

        for (UINT i = 0; i < kFrameCount; ++i) {
            throwIfFailed(_swapChain->GetBuffer(i, IID_PPV_ARGS(&_renderTargets[i])), "IDXGISwapChain3::GetBuffer");
            _device->CreateRenderTargetView(_renderTargets[i].Get(), &rtvDesc, rtvHandle);
            rtvHandle.ptr += _rtvDescriptorSize;
        }
    }

    // [0.6 & 6단계: 폐색 해결용 깊이 버퍼(D32_FLOAT) 실체화]
    void createDepthStencilBuffer() {
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

        D3D12_RESOURCE_DESC depthDesc{};
        depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        depthDesc.Alignment = 0;
        depthDesc.Width = _width;
        depthDesc.Height = _height;
        depthDesc.DepthOrArraySize = 1;
        depthDesc.MipLevels = 1;
        depthDesc.Format = kDepthStencilFormat;
        depthDesc.SampleDesc.Count = 1;
        depthDesc.SampleDesc.Quality = 0;
        depthDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE clearValue{};
        clearValue.Format = kDepthStencilFormat;
        clearValue.DepthStencil.Depth = 1.0f;
        clearValue.DepthStencil.Stencil = 0;

        throwIfFailed(
            _device->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &depthDesc,
                D3D12_RESOURCE_STATE_DEPTH_WRITE,
                &clearValue,
                IID_PPV_ARGS(&_depthStencilBuffer)
            ),
            "CreateCommittedResource (DepthStencil)"
        );

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
        dsvDesc.Format = kDepthStencilFormat;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
        _device->CreateDepthStencilView(_depthStencilBuffer.Get(), &dsvDesc, _dsvHeap->GetCPUDescriptorHandleForHeapStart());
    }

    // [0.5 & 0.7단계: CPU↔GPU 데이터 ABI(Root Signature + Input Layout) 및 불변 PSO 사전 컴파일]
    void createRootSignatureAndPipeline() {
        D3D12_ROOT_PARAMETER rootParam{};
        rootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        rootParam.Descriptor.ShaderRegister = 0;
        rootParam.Descriptor.RegisterSpace = 0;
        rootParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC rootSigDesc{};
        rootSigDesc.NumParameters = 1;
        rootSigDesc.pParameters = &rootParam;
        rootSigDesc.NumStaticSamplers = 0;
        rootSigDesc.pStaticSamplers = nullptr;
        rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
        Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
        HRESULT hr = D3D12SerializeRootSignature(
            &rootSigDesc,
            D3D_ROOT_SIGNATURE_VERSION_1,
            &signatureBlob,
            &errorBlob
        );
        if (FAILED(hr)) {
            if (errorBlob) {
                std::fprintf(stderr, "Root signature error: %s\n", static_cast<const char*>(errorBlob->GetBufferPointer()));
            }
            throwIfFailed(hr, "D3D12SerializeRootSignature");
        }

        throwIfFailed(
            _device->CreateRootSignature(
                0,
                signatureBlob->GetBufferPointer(),
                signatureBlob->GetBufferSize(),
                IID_PPV_ARGS(&_rootSignature)
            ),
            "CreateRootSignature"
        );

        UINT compileFlags = D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR;
#if defined(_DEBUG)
        compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

        Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
        Microsoft::WRL::ComPtr<ID3DBlob> psBlob;

        errorBlob.Reset();
        hr = D3DCompile(
            kDx12ShaderSource,
            std::strlen(kDx12ShaderSource),
            "Dx12Shaders.hlsl",
            nullptr,
            nullptr,
            "vertexMain",
            "vs_5_0",
            compileFlags,
            0,
            &vsBlob,
            &errorBlob
        );
        if (FAILED(hr)) {
            if (errorBlob) {
                std::fprintf(stderr, "Vertex shader compile error: %s\n", static_cast<const char*>(errorBlob->GetBufferPointer()));
            }
            throwIfFailed(hr, "D3DCompile (vs_5_0)");
        }

        errorBlob.Reset();
        hr = D3DCompile(
            kDx12ShaderSource,
            std::strlen(kDx12ShaderSource),
            "Dx12Shaders.hlsl",
            nullptr,
            nullptr,
            "pixelMain",
            "ps_5_0",
            compileFlags,
            0,
            &psBlob,
            &errorBlob
        );
        if (FAILED(hr)) {
            if (errorBlob) {
                std::fprintf(stderr, "Pixel shader compile error: %s\n", static_cast<const char*>(errorBlob->GetBufferPointer()));
            }
            throwIfFailed(hr, "D3DCompile (ps_5_0)");
        }

        constexpr D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(Vertex, position)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(Vertex, normal)),   D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(Vertex, color)),    D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.pRootSignature = _rootSignature.Get();
        psoDesc.VS = {vsBlob->GetBufferPointer(), vsBlob->GetBufferSize()};
        psoDesc.PS = {psBlob->GetBufferPointer(), psBlob->GetBufferSize()};
        psoDesc.InputLayout = {inputLayout, static_cast<UINT>(std::size(inputLayout))};

        psoDesc.BlendState.AlphaToCoverageEnable = FALSE;
        psoDesc.BlendState.IndependentBlendEnable = FALSE;
        D3D12_RENDER_TARGET_BLEND_DESC rtBlend{};
        rtBlend.BlendEnable = FALSE;
        rtBlend.LogicOpEnable = FALSE;
        rtBlend.SrcBlend = D3D12_BLEND_ONE;
        rtBlend.DestBlend = D3D12_BLEND_ZERO;
        rtBlend.BlendOp = D3D12_BLEND_OP_ADD;
        rtBlend.SrcBlendAlpha = D3D12_BLEND_ONE;
        rtBlend.DestBlendAlpha = D3D12_BLEND_ZERO;
        rtBlend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        rtBlend.LogicOp = D3D12_LOGIC_OP_NOOP;
        rtBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        psoDesc.BlendState.RenderTarget[0] = rtBlend;
        psoDesc.SampleMask = UINT_MAX;

        // [4~5단계 경계: 반시계 방향(CCW) 앞면 기준 후면 제거]
        psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
        psoDesc.RasterizerState.FrontCounterClockwise = TRUE;
        psoDesc.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
        psoDesc.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
        psoDesc.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
        psoDesc.RasterizerState.DepthClipEnable = TRUE;
        psoDesc.RasterizerState.MultisampleEnable = FALSE;
        psoDesc.RasterizerState.AntialiasedLineEnable = FALSE;
        psoDesc.RasterizerState.ForcedSampleCount = 0;
        psoDesc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

        // [6단계: 폐색 해결(Z-버퍼 깊이 비교 LESS)]
        psoDesc.DepthStencilState.DepthEnable = TRUE;
        psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        psoDesc.DepthStencilState.StencilEnable = FALSE;
        psoDesc.DepthStencilState.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
        psoDesc.DepthStencilState.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;

        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = kRenderTargetViewFormat;
        psoDesc.DSVFormat = kDepthStencilFormat;
        psoDesc.SampleDesc.Count = 1;
        psoDesc.SampleDesc.Quality = 0;

        throwIfFailed(_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_pipelineState)), "CreateGraphicsPipelineState");
    }

    void createBuffer(
        UINT64 byteSize,
        D3D12_HEAP_TYPE heapType,
        D3D12_RESOURCE_STATES initialState,
        Microsoft::WRL::ComPtr<ID3D12Resource>& outBuffer,
        const char* context
    ) {
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = heapType;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

        D3D12_RESOURCE_DESC bufferDesc{};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Alignment = 0;
        bufferDesc.Width = byteSize;
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.SampleDesc.Quality = 0;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        throwIfFailed(
            _device->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &bufferDesc,
                initialState,
                nullptr,
                IID_PPV_ARGS(&outBuffer)
            ),
            context
        );
    }

    void createUploadBuffer(UINT64 byteSize, Microsoft::WRL::ComPtr<ID3D12Resource>& outBuffer) {
        createBuffer(
            byteSize,
            D3D12_HEAP_TYPE_UPLOAD,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            outBuffer,
            "CreateCommittedResource (UploadBuffer)"
        );
    }

    void createDefaultBuffer(UINT64 byteSize, Microsoft::WRL::ComPtr<ID3D12Resource>& outBuffer) {
        createBuffer(
            byteSize,
            D3D12_HEAP_TYPE_DEFAULT,
            D3D12_RESOURCE_STATE_COMMON,
            outBuffer,
            "CreateCommittedResource (DefaultBuffer)"
        );
    }

    // [0.6단계: 메모리 토폴로지 정렬 및 정점·인덱스(DEFAULT + Staging)·프레임 상수(UPLOAD) 자원 실체화]
    void createGeometryAndUniformBuffers() {
        const D3D12_RANGE readRange{0, 0};

        createDefaultBuffer(static_cast<UINT64>(sizeof(kCubeVertices)), _vertexBuffer);
        createDefaultBuffer(static_cast<UINT64>(sizeof(kCubeIndices)), _indexBuffer);

        Microsoft::WRL::ComPtr<ID3D12Resource> vertexStagingBuffer;
        createUploadBuffer(static_cast<UINT64>(sizeof(kCubeVertices)), vertexStagingBuffer);
        void* mappedVertices = nullptr;
        throwIfFailed(vertexStagingBuffer->Map(0, &readRange, &mappedVertices), "VertexStagingBuffer::Map");
        std::memcpy(mappedVertices, kCubeVertices.data(), sizeof(kCubeVertices));
        vertexStagingBuffer->Unmap(0, nullptr);

        Microsoft::WRL::ComPtr<ID3D12Resource> indexStagingBuffer;
        createUploadBuffer(static_cast<UINT64>(sizeof(kCubeIndices)), indexStagingBuffer);
        void* mappedIndices = nullptr;
        throwIfFailed(indexStagingBuffer->Map(0, &readRange, &mappedIndices), "IndexStagingBuffer::Map");
        std::memcpy(mappedIndices, kCubeIndices.data(), sizeof(kCubeIndices));
        indexStagingBuffer->Unmap(0, nullptr);

        auto& allocator = _commandAllocators[0];
        throwIfFailed(allocator->Reset(), "ID3D12CommandAllocator::Reset (staging)");
        throwIfFailed(_commandList->Reset(allocator.Get(), nullptr), "ID3D12GraphicsCommandList::Reset (staging)");

        _commandList->CopyBufferRegion(
            _vertexBuffer.Get(),
            0,
            vertexStagingBuffer.Get(),
            0,
            static_cast<UINT64>(sizeof(kCubeVertices))
        );
        _commandList->CopyBufferRegion(
            _indexBuffer.Get(),
            0,
            indexStagingBuffer.Get(),
            0,
            static_cast<UINT64>(sizeof(kCubeIndices))
        );

        std::array<D3D12_RESOURCE_BARRIER, 2> barriers{};
        barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[0].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barriers[0].Transition.pResource = _vertexBuffer.Get();
        barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;

        barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[1].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barriers[1].Transition.pResource = _indexBuffer.Get();
        barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_INDEX_BUFFER;

        _commandList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());

        throwIfFailed(_commandList->Close(), "ID3D12GraphicsCommandList::Close (staging)");

        ID3D12CommandList* commandLists[] = {_commandList.Get()};
        _commandQueue->ExecuteCommandLists(1, commandLists);
        flushGpu();

        vertexStagingBuffer.Reset();
        indexStagingBuffer.Reset();

        _vertexBufferView.BufferLocation = _vertexBuffer->GetGPUVirtualAddress();
        _vertexBufferView.StrideInBytes = static_cast<UINT>(sizeof(Vertex));
        _vertexBufferView.SizeInBytes = static_cast<UINT>(sizeof(kCubeVertices));

        _indexBufferView.BufferLocation = _indexBuffer->GetGPUVirtualAddress();
        _indexBufferView.Format = DXGI_FORMAT_R16_UINT;
        _indexBufferView.SizeInBytes = static_cast<UINT>(sizeof(kCubeIndices));

        // 256바이트 정렬된 프레임 슬롯별 Constant Buffer를 Upload Heap에 Persistent Map
        createUploadBuffer(static_cast<UINT64>(kAlignedUniformSize) * kFrameCount, _uniformBuffer);
        void* mappedUniforms = nullptr;
        throwIfFailed(_uniformBuffer->Map(0, &readRange, &mappedUniforms), "UniformBuffer::Map");
        _uniformMappedData = static_cast<std::uint8_t*>(mappedUniforms);
    }

    // [0.8단계: CPU↔GPU 동기화 원시체(Fence + Win32 Event) 구축]
    void createSyncObjects() {
        throwIfFailed(_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_fence)), "CreateFence");
        _fenceValues.fill(0);
        _nextFenceValue = 1;

        _fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!_fenceEvent) {
            throw std::runtime_error("Direct3D 12 error: CreateEventW failed for fence synchronization.");
        }
    }

    UINT _width = 1;
    UINT _height = 1;
    UINT _frameIndex = 0;
    UINT _rtvDescriptorSize = 0;

    Microsoft::WRL::ComPtr<IDXGIFactory6> _factory;
    Microsoft::WRL::ComPtr<ID3D12Device> _device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> _commandQueue;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> _swapChain;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> _rtvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> _dsvHeap;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kFrameCount> _renderTargets;
    Microsoft::WRL::ComPtr<ID3D12Resource> _depthStencilBuffer;

    std::array<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>, kFrameCount> _commandAllocators;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> _commandList;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> _rootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> _pipelineState;

    Microsoft::WRL::ComPtr<ID3D12Resource> _vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> _indexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> _uniformBuffer;
    D3D12_VERTEX_BUFFER_VIEW _vertexBufferView{};
    D3D12_INDEX_BUFFER_VIEW _indexBufferView{};
    std::uint8_t* _uniformMappedData = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Fence> _fence;
    std::array<UINT64, kFrameCount> _fenceValues{};
    UINT64 _nextFenceValue = 1;
    HANDLE _fenceEvent = nullptr;
};
