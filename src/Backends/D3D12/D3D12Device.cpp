#include "D3D12Device.h"
#include "D3D12CommandList.h"
#include "D3D12Buffer.h"
#include "RHI/IPipelineState.h"
#include "D3D12PipelineState.h"
#include "D3D12Texture.h"
#include "d3dx12.h"
#include <d3dcompiler.h>

#pragma comment(lib, "d3dcompiler.lib")

#include <d3d12.h>
#include <dxgi1_6.h>
#include <algorithm>
#include <iostream>
#include <limits>
#include <memory>
#include <utility>
#include <vector>
#include <wrl.h>

using Microsoft::WRL::ComPtr;

namespace dy::Backends
{
    namespace
    {
        constexpr uint32_t kGlobalDescriptorHeapSize = 1024;
        constexpr uint32_t kTransientDescriptorSlotCount = 1;
    }

    struct D3D12FrameSlot
    {
        uint64_t completionValue = 0;
    };

    struct D3D12SubmissionRecord
    {
        uint64_t completionValue = 0;
        std::vector<std::unique_ptr<D3D12CommandList>> commandLists;
        std::vector<ComPtr<ID3D12Object>> retainedObjects;
    };

    // 헤더에서 선언만 했던 구조체의 실제 정의
    struct D3D12InternalState
    {
        ComPtr<ID3D12Device> device;
        ComPtr<ID3D12InfoQueue> infoQueue; // 디버그 빌드: D3D12 검증 메시지 수집
        ComPtr<ID3D12CommandQueue> commandQueue;
        HWND windowHandle = nullptr;
        ComPtr<IDXGISwapChain3> swapChain;
        ComPtr<ID3D12DescriptorHeap> rtvHeap;
        std::vector<std::unique_ptr<D3D12Texture>> backBufferTextures;
        std::vector<uint64_t> imageCompletionValues;

        ComPtr<ID3D12Fence> fence;
        uint64_t nextCompletionValue = 1;
        HANDLE fenceEvent = nullptr;
        std::vector<D3D12FrameSlot> frames;
        std::vector<D3D12SubmissionRecord> submissions;
        std::vector<std::unique_ptr<D3D12CommandList>> activeCommandLists;

        uint32_t nextFrameIndex = 0;
        uint32_t activeFrameIndex = 0;
        uint32_t activeImageIndex = 0;
        UINT presentSyncInterval = 1;
        UINT presentFlags = 0;
        UINT swapchainFlags = 0;
        DXGI_FORMAT swapchainResourceFormat = DXGI_FORMAT_UNKNOWN;
        DXGI_FORMAT swapchainRtvFormat = DXGI_FORMAT_UNKNOWN;
        RHI::Format swapchainFormat = RHI::Format::Unknown;
        RHI::SwapchainDesc swapchainDesc = {};
        bool swapchainReady = false;
        bool frameReady = false;
        bool frameSubmitted = false;
        bool submissionFaulted = false;

        ComPtr<ID3D12DescriptorHeap> globalDescriptorHeap;
        uint32_t descriptorSlotOffset = 0;
        uint32_t srvDescriptorSize = 0;

        ComPtr<ID3D12RootSignature> deviceRootSignature;
        ComPtr<ID3D12PipelineState> texturedTrianglePipeline;
    };

    static bool CreateBackBufferViews(
        D3D12InternalState* internal,
        IDXGISwapChain3* swapchain,
        RHI::Format format,
        DXGI_FORMAT resourceFormat,
        DXGI_FORMAT rtvFormat,
        ComPtr<ID3D12DescriptorHeap>& rtvHeap,
        std::vector<std::unique_ptr<D3D12Texture>>& textures)
    {
        if (internal == nullptr || internal->device == nullptr || swapchain == nullptr)
        {
            return false;
        }

        DXGI_SWAP_CHAIN_DESC1 actualDesc = {};
        if (FAILED(swapchain->GetDesc1(&actualDesc)) || actualDesc.BufferCount == 0 ||
            actualDesc.Format != resourceFormat)
        {
            return false;
        }

        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.NumDescriptors = actualDesc.BufferCount;
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        ComPtr<ID3D12DescriptorHeap> newRtvHeap;
        if (FAILED(internal->device->CreateDescriptorHeap(
                &heapDesc, IID_PPV_ARGS(&newRtvHeap))))
        {
            return false;
        }

        const uint32_t descriptorSize =
            internal->device->GetDescriptorHandleIncrementSize(
                D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
            newRtvHeap->GetCPUDescriptorHandleForHeapStart();
        D3D12_RENDER_TARGET_VIEW_DESC viewDesc = {};
        viewDesc.Format = rtvFormat;
        viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

        std::vector<std::unique_ptr<D3D12Texture>> newTextures;
        newTextures.reserve(actualDesc.BufferCount);
        for (UINT imageIndex = 0; imageIndex < actualDesc.BufferCount; ++imageIndex)
        {
            ComPtr<ID3D12Resource> resource;
            if (FAILED(swapchain->GetBuffer(imageIndex, IID_PPV_ARGS(&resource))))
            {
                return false;
            }

            internal->device->CreateRenderTargetView(
                resource.Get(), &viewDesc, rtvHandle);
            const D3D12_RESOURCE_DESC nativeDesc = resource->GetDesc();
            RHI::TextureDesc textureDesc = {};
            textureDesc.width = static_cast<uint32_t>(nativeDesc.Width);
            textureDesc.height = nativeDesc.Height;
            textureDesc.format = format;
            textureDesc.usage = RHI::TextureUsage::RenderTarget;
            newTextures.push_back(std::make_unique<D3D12Texture>(
                resource.Get(), textureDesc, rtvHandle.ptr, true));
            rtvHandle.ptr += descriptorSize;
        }

        rtvHeap = std::move(newRtvHeap);
        textures = std::move(newTextures);
        return true;
    }

    // 누적된 D3D12 검증 메시지를 stdout 으로 덤프하고 디바이스 제거 사유를 확인한다.
    // (디버그 레이어가 켜진 디버그 빌드에서만 메시지가 쌓인다.)
    static void DumpInfoQueue(D3D12InternalState* internal, const char* where)
    {
        if (internal == nullptr || internal->infoQueue == nullptr) return;
        const UINT64 count = internal->infoQueue->GetNumStoredMessages();
        for (UINT64 i = 0; i < count; ++i) {
            SIZE_T len = 0;
            internal->infoQueue->GetMessage(i, nullptr, &len);
            std::vector<char> bytes(len);
            D3D12_MESSAGE* msg = reinterpret_cast<D3D12_MESSAGE*>(bytes.data());
            if (SUCCEEDED(internal->infoQueue->GetMessage(i, msg, &len)) && msg->pDescription) {
                std::cout << "[D3D12 " << where << " sev=" << static_cast<int>(msg->Severity)
                          << " id=" << static_cast<int>(msg->ID) << "] " << msg->pDescription << std::endl;
            }
        }
        internal->infoQueue->ClearStoredMessages();
        const HRESULT removedReason = internal->device->GetDeviceRemovedReason();
        if (FAILED(removedReason)) {
            std::cout << "[D3D12] DEVICE REMOVED reason=0x" << std::hex << static_cast<unsigned>(removedReason) << std::dec << std::endl;
        }
    }

    D3D12Device::D3D12Device()
    {
        m_internal = new D3D12InternalState();
    }

    D3D12Device::~D3D12Device()
    {
        if (m_internal == nullptr) return;

        if (m_internal->commandQueue != nullptr &&
            m_internal->fence != nullptr &&
            m_internal->fenceEvent != nullptr)
        {
            const uint64_t completionValue = m_internal->nextCompletionValue++;
            if (SUCCEEDED(m_internal->commandQueue->Signal(
                    m_internal->fence.Get(), completionValue)) &&
                m_internal->fence->GetCompletedValue() < completionValue &&
                SUCCEEDED(m_internal->fence->SetEventOnCompletion(
                    completionValue, m_internal->fenceEvent)))
            {
                WaitForSingleObject(m_internal->fenceEvent, INFINITE);
            }
        }

        if (m_internal->fenceEvent != nullptr) CloseHandle(m_internal->fenceEvent);
        delete m_internal;
    }

    int D3D12Device::Initialize(const void* windowHandle, const RHI::DeviceDesc& desc)
    {
        if (windowHandle == nullptr || desc.maxFramesInFlight == 0) return -1;
        m_internal->windowHandle = static_cast<HWND>(const_cast<void*>(windowHandle));

#if defined(_DEBUG)
        // 디버그 레이어 활성화(디바이스 생성 전 필수). 이래야 InfoQueue 에 검증 메시지가 쌓인다.
        // ("그래픽 도구" 선택 기능 미설치 시 D3D12GetDebugInterface 가 실패하므로 조건부 처리.)
        {
            ComPtr<ID3D12Debug> debugController;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
                debugController->EnableDebugLayer();
            }
        }
#endif

        // 1. 디바이스 생성
        if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_internal->device)))) {
            return -1;
        }
        
#if defined(_DEBUG)
        if (SUCCEEDED(m_internal->device.As(&m_internal->infoQueue))) {
            // break 하지 않고 메시지를 모아 DumpInfoQueue 가 stdout 으로 덤프한다.
            m_internal->infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, FALSE);
            m_internal->infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, FALSE);
            m_internal->infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, FALSE);
        }
#endif

        // 2. 커맨드 큐 생성
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(m_internal->device->CreateCommandQueue(
                &queueDesc, IID_PPV_ARGS(&m_internal->commandQueue))))
        {
            return -1;
        }

        // 6. 동기화용 펜스(Fence) 생성
        if (FAILED(m_internal->device->CreateFence(
                0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_internal->fence))))
        {
            return -1;
        }
        m_internal->fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (m_internal->fenceEvent == nullptr) return -1;

        // 7. 글로벌 디스크립터 힙 생성
        D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
        srvHeapDesc.NumDescriptors = kGlobalDescriptorHeapSize;
        srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        
        if(FAILED(m_internal->device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_internal->globalDescriptorHeap))))
        {
            std::cout << "Failed to create descriptor heap!" << std::endl;
            return -1;
        }
        m_internal->srvDescriptorSize = m_internal->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        m_internal->frames.resize(desc.maxFramesInFlight);
        return 0;
    }

    bool D3D12Device::CreateSwapchain(const RHI::SwapchainDesc& desc)
    {
        if (m_internal == nullptr || m_internal->device == nullptr ||
            m_internal->commandQueue == nullptr || m_internal->windowHandle == nullptr ||
            m_internal->swapchainReady || desc.minimumImageCount == 0 ||
            desc.minimumImageCount > DXGI_MAX_SWAP_CHAIN_BUFFERS)
        {
            return false;
        }

        RHI::Format actualFormat = desc.format;
        DXGI_FORMAT resourceFormat = DXGI_FORMAT_UNKNOWN;
        DXGI_FORMAT rtvFormat = DXGI_FORMAT_UNKNOWN;
        switch (desc.format)
        {
        case RHI::Format::Unknown:
            actualFormat = RHI::Format::R8G8B8A8_UNORM;
            resourceFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
            rtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
            break;
        case RHI::Format::R8G8B8A8_UNORM:
            resourceFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
            rtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
            break;
        case RHI::Format::B8G8R8A8_UNORM:
            resourceFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
            rtvFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
            break;
        case RHI::Format::R8G8B8A8_UNORM_SRGB:
            resourceFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
            rtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            break;
        case RHI::Format::B8G8R8A8_UNORM_SRGB:
            resourceFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
            rtvFormat = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
            break;
        case RHI::Format::R16G16B16A16_FLOAT:
            resourceFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
            rtvFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
            break;
        default:
            return false;
        }

        ComPtr<IDXGIFactory4> factory;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return false;

        UINT presentSyncInterval = 1;
        UINT presentFlags = 0;
        UINT swapchainFlags = 0;
        switch (desc.presentMode)
        {
        case RHI::PresentMode::Fifo:
            break;
        case RHI::PresentMode::Immediate:
        {
            ComPtr<IDXGIFactory5> factory5;
            BOOL tearingSupported = FALSE;
            if (FAILED(factory.As(&factory5)) || factory5 == nullptr ||
                FAILED(factory5->CheckFeatureSupport(
                    DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                    &tearingSupported,
                    sizeof(tearingSupported))) ||
                tearingSupported == FALSE)
            {
                return false;
            }
            presentSyncInterval = 0;
            presentFlags = DXGI_PRESENT_ALLOW_TEARING;
            swapchainFlags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
            break;
        }
        case RHI::PresentMode::Mailbox:
        default:
            return false;
        }

        RECT clientRect = {};
        if (!GetClientRect(m_internal->windowHandle, &clientRect) ||
            clientRect.right <= clientRect.left ||
            clientRect.bottom <= clientRect.top)
        {
            return false;
        }

        DXGI_SWAP_CHAIN_DESC1 requestedDesc = {};
        requestedDesc.BufferCount = std::max(desc.minimumImageCount, 2u);
        requestedDesc.Width = static_cast<UINT>(clientRect.right - clientRect.left);
        requestedDesc.Height = static_cast<UINT>(clientRect.bottom - clientRect.top);
        requestedDesc.Format = resourceFormat;
        requestedDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        requestedDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        requestedDesc.SampleDesc.Count = 1;
        requestedDesc.Flags = swapchainFlags;

        ComPtr<IDXGISwapChain1> swapchain1;
        ComPtr<IDXGISwapChain3> swapchain3;
        if (FAILED(factory->CreateSwapChainForHwnd(
                m_internal->commandQueue.Get(),
                m_internal->windowHandle,
                &requestedDesc,
                nullptr,
                nullptr,
                &swapchain1)) ||
            FAILED(swapchain1.As(&swapchain3)))
        {
            return false;
        }

        DXGI_SWAP_CHAIN_DESC1 actualDesc = {};
        if (FAILED(swapchain3->GetDesc1(&actualDesc)) ||
            actualDesc.BufferCount < desc.minimumImageCount ||
            actualDesc.Format != resourceFormat)
        {
            return false;
        }

        ComPtr<ID3D12DescriptorHeap> rtvHeap;
        std::vector<std::unique_ptr<D3D12Texture>> backBufferTextures;
        if (!CreateBackBufferViews(
                m_internal,
                swapchain3.Get(),
                actualFormat,
                resourceFormat,
                rtvFormat,
                rtvHeap,
                backBufferTextures))
        {
            return false;
        }

        m_internal->swapChain = std::move(swapchain3);
        m_internal->rtvHeap = std::move(rtvHeap);
        m_internal->backBufferTextures = std::move(backBufferTextures);
        m_internal->imageCompletionValues.assign(actualDesc.BufferCount, 0);
        m_internal->presentSyncInterval = presentSyncInterval;
        m_internal->presentFlags = presentFlags;
        m_internal->swapchainFlags = swapchainFlags;
        m_internal->swapchainResourceFormat = resourceFormat;
        m_internal->swapchainRtvFormat = rtvFormat;
        m_internal->swapchainFormat = actualFormat;
        m_internal->swapchainDesc = desc;
        m_internal->activeImageIndex = m_internal->swapChain->GetCurrentBackBufferIndex();
        m_internal->swapchainReady =
            m_internal->activeImageIndex < m_internal->backBufferTextures.size();
        return m_internal->swapchainReady;
    }

    bool D3D12Device::BeginFrame()
    {
        if (m_internal == nullptr || !m_internal->swapchainReady ||
            m_internal->submissionFaulted || m_internal->frameReady ||
            m_internal->frameSubmitted || m_internal->frames.empty())
        {
            return false;
        }

        const uint64_t completedValue = m_internal->fence->GetCompletedValue();
        if (completedValue == std::numeric_limits<uint64_t>::max())
        {
            m_internal->submissionFaulted = true;
            return false;
        }
        m_internal->submissions.erase(
            std::remove_if(
                m_internal->submissions.begin(),
                m_internal->submissions.end(),
                [completedValue](const D3D12SubmissionRecord& submission)
                {
                    return submission.completionValue <= completedValue;
                }),
            m_internal->submissions.end());

        RECT clientRect = {};
        if (!GetClientRect(m_internal->windowHandle, &clientRect) ||
            clientRect.right <= clientRect.left ||
            clientRect.bottom <= clientRect.top)
        {
            return false;
        }
        const uint32_t width = static_cast<uint32_t>(
            clientRect.right - clientRect.left);
        const uint32_t height = static_cast<uint32_t>(
            clientRect.bottom - clientRect.top);

        if (m_internal->backBufferTextures.empty())
        {
            ComPtr<ID3D12DescriptorHeap> rtvHeap;
            std::vector<std::unique_ptr<D3D12Texture>> backBufferTextures;
            if (!CreateBackBufferViews(
                    m_internal,
                    m_internal->swapChain.Get(),
                    m_internal->swapchainFormat,
                    m_internal->swapchainResourceFormat,
                    m_internal->swapchainRtvFormat,
                    rtvHeap,
                    backBufferTextures))
            {
                return false;
            }
            m_internal->rtvHeap = std::move(rtvHeap);
            m_internal->backBufferTextures = std::move(backBufferTextures);
            m_internal->imageCompletionValues.assign(
                m_internal->backBufferTextures.size(), 0);
        }

        const RHI::TextureDesc& backBufferDesc =
            m_internal->backBufferTextures.front()->GetDesc();
        if (backBufferDesc.width != width || backBufferDesc.height != height)
        {
            for (const D3D12FrameSlot& frame : m_internal->frames)
            {
                if (frame.completionValue > completedValue) return false;
            }
            for (uint64_t completionValue : m_internal->imageCompletionValues)
            {
                if (completionValue > completedValue) return false;
            }
            for (const std::unique_ptr<D3D12CommandList>& commandList :
                m_internal->activeCommandLists)
            {
                if (!commandList->GetReferencedSwapchainImages().empty()) return false;
            }

            const UINT imageCount = static_cast<UINT>(
                m_internal->imageCompletionValues.size());
            if (imageCount < m_internal->swapchainDesc.minimumImageCount)
            {
                return false;
            }

            m_internal->backBufferTextures.clear();
            m_internal->rtvHeap.Reset();
            if (FAILED(m_internal->swapChain->ResizeBuffers(
                    imageCount,
                    width,
                    height,
                    m_internal->swapchainResourceFormat,
                    m_internal->swapchainFlags)))
            {
                ComPtr<ID3D12DescriptorHeap> rtvHeap;
                std::vector<std::unique_ptr<D3D12Texture>> backBufferTextures;
                if (CreateBackBufferViews(
                        m_internal,
                        m_internal->swapChain.Get(),
                        m_internal->swapchainFormat,
                        m_internal->swapchainResourceFormat,
                        m_internal->swapchainRtvFormat,
                        rtvHeap,
                        backBufferTextures))
                {
                    m_internal->rtvHeap = std::move(rtvHeap);
                    m_internal->backBufferTextures = std::move(backBufferTextures);
                    m_internal->imageCompletionValues.assign(
                        m_internal->backBufferTextures.size(), 0);
                }
                return false;
            }

            ComPtr<ID3D12DescriptorHeap> rtvHeap;
            std::vector<std::unique_ptr<D3D12Texture>> backBufferTextures;
            const RHI::Format format =
                m_internal->swapchainDesc.format == RHI::Format::Unknown
                    ? m_internal->swapchainFormat
                    : m_internal->swapchainDesc.format;
            if (!CreateBackBufferViews(
                    m_internal,
                    m_internal->swapChain.Get(),
                    format,
                    m_internal->swapchainResourceFormat,
                    m_internal->swapchainRtvFormat,
                    rtvHeap,
                    backBufferTextures))
            {
                return false;
            }
            m_internal->rtvHeap = std::move(rtvHeap);
            m_internal->backBufferTextures = std::move(backBufferTextures);
            m_internal->imageCompletionValues.assign(
                m_internal->backBufferTextures.size(), 0);
        }

        const uint32_t imageIndex = m_internal->swapChain->GetCurrentBackBufferIndex();
        if (m_internal->nextFrameIndex >= m_internal->frames.size() ||
            imageIndex >= m_internal->imageCompletionValues.size() ||
            m_internal->frames[m_internal->nextFrameIndex].completionValue > completedValue ||
            m_internal->imageCompletionValues[imageIndex] > completedValue)
        {
            return false;
        }

        m_internal->activeFrameIndex = m_internal->nextFrameIndex;
        m_internal->activeImageIndex = imageIndex;
        m_internal->frameReady = true;
        return true;
    }

    RHI::ICommandList* D3D12Device::AcquireCommandList()
    {
        if (m_internal == nullptr || m_internal->device == nullptr ||
            m_internal->submissionFaulted)
        {
            return nullptr;
        }

        const uint64_t completedValue = m_internal->fence->GetCompletedValue();
        if (completedValue == std::numeric_limits<uint64_t>::max())
        {
            m_internal->submissionFaulted = true;
            return nullptr;
        }
        m_internal->submissions.erase(
            std::remove_if(
                m_internal->submissions.begin(),
                m_internal->submissions.end(),
                [completedValue](const D3D12SubmissionRecord& submission)
                {
                    return submission.completionValue <= completedValue;
                }),
            m_internal->submissions.end());

        auto commandList = std::make_unique<D3D12CommandList>(
            m_internal->device.Get(),
            m_internal->globalDescriptorHeap.Get(),
            m_internal->srvDescriptorSize);
        if (commandList->GetNativeList() == nullptr) return nullptr;
        D3D12CommandList* result = commandList.get();
        m_internal->activeCommandLists.push_back(std::move(commandList));
        return result;
    }

    bool D3D12Device::Submit(RHI::ICommandList** cmdLists, uint32_t count)
    {
        if (m_internal == nullptr || m_internal->submissionFaulted ||
            cmdLists == nullptr || count == 0)
        {
            return false;
        }

        D3D12Texture* activeBackBuffer = nullptr;
        if (m_internal->frameReady &&
            m_internal->activeImageIndex < m_internal->backBufferTextures.size())
        {
            activeBackBuffer =
                m_internal->backBufferTextures[m_internal->activeImageIndex].get();
        }

        std::vector<D3D12CommandList*> submittedCommandLists;
        submittedCommandLists.reserve(count);
        std::vector<ID3D12CommandList*> nativeCommandLists;
        nativeCommandLists.reserve(count);
        bool frameSubmission = false;
        for (uint32_t index = 0; index < count; ++index)
        {
            if (cmdLists[index] == nullptr) return false;
            for (uint32_t previous = 0; previous < index; ++previous)
            {
                if (cmdLists[previous] == cmdLists[index]) return false;
            }

            const auto owned = std::find_if(
                m_internal->activeCommandLists.begin(),
                m_internal->activeCommandLists.end(),
                [command = cmdLists[index]](const std::unique_ptr<D3D12CommandList>& candidate)
                {
                    return candidate.get() == command;
                });
            if (owned == m_internal->activeCommandLists.end()) return false;

            D3D12CommandList* commandList = owned->get();
            if (!commandList->IsClosed() ||
                commandList->GetNativeList() == nullptr)
            {
                return false;
            }

            for (D3D12Texture* image : commandList->GetReferencedSwapchainImages())
            {
                if (activeBackBuffer == nullptr || image != activeBackBuffer)
                {
                    return false;
                }
                frameSubmission = true;
            }

            submittedCommandLists.push_back(commandList);
            nativeCommandLists.push_back(
                static_cast<ID3D12CommandList*>(commandList->GetNativeList()));
        }

        m_internal->submissions.emplace_back();
        D3D12SubmissionRecord& submission = m_internal->submissions.back();
        submission.completionValue = m_internal->nextCompletionValue++;
        submission.commandLists.reserve(count);
        for (D3D12CommandList* commandList : submittedCommandLists)
        {
            const auto owned = std::find_if(
                m_internal->activeCommandLists.begin(),
                m_internal->activeCommandLists.end(),
                [commandList](const std::unique_ptr<D3D12CommandList>& candidate)
                {
                    return candidate.get() == commandList;
                });
            submission.commandLists.push_back(std::move(*owned));
            m_internal->activeCommandLists.erase(owned);
        }

        m_internal->commandQueue->ExecuteCommandLists(
            count, nativeCommandLists.data());
        if (FAILED(m_internal->commandQueue->Signal(
                m_internal->fence.Get(), submission.completionValue)))
        {
            submission.completionValue = std::numeric_limits<uint64_t>::max();
            m_internal->submissionFaulted = true;
            if (frameSubmission) m_internal->frameReady = false;
            return false;
        }

        if (frameSubmission)
        {
            m_internal->frames[m_internal->activeFrameIndex].completionValue =
                submission.completionValue;
            m_internal->imageCompletionValues[m_internal->activeImageIndex] =
                submission.completionValue;
            m_internal->nextFrameIndex =
                (m_internal->activeFrameIndex + 1) %
                static_cast<uint32_t>(m_internal->frames.size());
            m_internal->frameReady = false;
            m_internal->frameSubmitted = true;
        }
        DumpInfoQueue(m_internal, "Submit");
        return true;
    }

    void D3D12Device::Present()
    {
        if (m_internal == nullptr || m_internal->submissionFaulted ||
            !m_internal->frameSubmitted)
        {
            return;
        }

        const HRESULT result = m_internal->swapChain->Present(
            m_internal->presentSyncInterval,
            m_internal->presentFlags);
        m_internal->frameSubmitted = false;
        if (FAILED(result)) m_internal->submissionFaulted = true;
        DumpInfoQueue(m_internal, "Present");
    }

    RHI::IBuffer* D3D12Device::CreateBuffer(const RHI::BufferDesc& desc) { 
        return new D3D12Buffer(m_internal->device.Get(), desc);
    }

    RHI::IPipelineState* D3D12Device::CreateGraphicsPipeline(const RHI::GraphicsPipelineDesc& desc) {
        // 1. Root Signature 1.1 지원 여부 확인
        D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
        if (FAILED(m_internal->device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData)))) {
            featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
        }

        // 2. Root Parameter 정의
        // 머티리얼 텍스처 테이블: 글로벌 디스크립터 힙을 덮는 unbounded SRV 배열(register t0, space0).
        //  - non-bindless: 셰이더가 인덱스 0(=트랜지언트 슬롯)만 읽음.
        //  - bindless    : 셰이더가 per-draw 디스크립터 인덱스로 BindlessTextures[idx] 를 읽음.
        // DESCRIPTORS_VOLATILE 라 접근하지 않는 슬롯은 미초기화여도 무방.
        CD3DX12_DESCRIPTOR_RANGE1 textureSrvRange;
        textureSrvRange.Init(
            D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
            UINT_MAX, // unbounded
            0,
            0,
            D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE,
            0);

        // 그림자 맵 SRV: bindless 텍스처 힙과 겹치지 않도록 register(t0, space4) 에 단독 배치.
        CD3DX12_DESCRIPTOR_RANGE1 shadowSrvRange;
        shadowSrvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 4, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE, 0);

        CD3DX12_DESCRIPTOR_RANGE1 metallicRoughnessSrvRange;
        metallicRoughnessSrvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 1, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE, 0);
        CD3DX12_DESCRIPTOR_RANGE1 normalSrvRange;
        normalSrvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 1, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE, 0);
        CD3DX12_DESCRIPTOR_RANGE1 occlusionSrvRange;
        occlusionSrvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3, 1, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE, 0);
        CD3DX12_DESCRIPTOR_RANGE1 emissiveSrvRange;
        emissiveSrvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4, 1, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE, 0);

        CD3DX12_ROOT_PARAMETER1 rootParameters[10] = {};
        rootParameters[0].InitAsConstants(52, 0); // register(b0): DrawConstants 208 bytes = 52 DWORDs
        rootParameters[1].InitAsDescriptorTable(1, &textureSrvRange, D3D12_SHADER_VISIBILITY_PIXEL); // register(t0, space0)
        rootParameters[2].InitAsConstantBufferView(1); // register(b1): RendererLighting
        rootParameters[3].InitAsConstantBufferView(3); // register(b3): ShadowMatrix
        // bindless storage SRV 는 space0~2 의 무한(unbounded) 범위(param 1)와 겹치면 안 되므로 space3 에 둔다.
        rootParameters[4].InitAsShaderResourceView(11, 3); // register(t11, space3): instance transforms
        rootParameters[5].InitAsDescriptorTable(1, &shadowSrvRange, D3D12_SHADER_VISIBILITY_PIXEL); // register(t0, space4): shadow map
        rootParameters[6].InitAsDescriptorTable(1, &metallicRoughnessSrvRange, D3D12_SHADER_VISIBILITY_PIXEL); // register(t1, space1)
        rootParameters[7].InitAsDescriptorTable(1, &normalSrvRange, D3D12_SHADER_VISIBILITY_PIXEL); // register(t2, space1)
        rootParameters[8].InitAsDescriptorTable(1, &occlusionSrvRange, D3D12_SHADER_VISIBILITY_PIXEL); // register(t3, space1)
        rootParameters[9].InitAsDescriptorTable(1, &emissiveSrvRange, D3D12_SHADER_VISIBILITY_PIXEL); // register(t4, space1)

        CD3DX12_STATIC_SAMPLER_DESC samplerDesc(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);

        // 3. Versioned Root Signature 생성
        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
        rootSigDesc.Init_1_1(10, rootParameters, 1, &samplerDesc, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        Microsoft::WRL::ComPtr<ID3DBlob> signature;
        Microsoft::WRL::ComPtr<ID3DBlob> error;

        // SerializeVersionedRootSignature를 사용하면 기기 지원 버전에 맞게 자동으로 1.1 또는 1.0으로 다운그레이드 직렬화 해줌
        if (FAILED(D3DX12SerializeVersionedRootSignature(&rootSigDesc, featureData.HighestVersion, &signature, &error))) {
            std::cout << "[D3D12] RootSignature serialize FAILED";
            if (error) std::cout << ": " << static_cast<const char*>(error->GetBufferPointer());
            std::cout << std::endl;
            return nullptr;
        }

        Microsoft::WRL::ComPtr<ID3D12RootSignature> pRootSignature;
        if (FAILED(m_internal->device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pRootSignature)))) {
            std::cout << "[D3D12] CreateRootSignature FAILED" << std::endl;
            DumpInfoQueue(m_internal, "CreateRootSignature");
            return nullptr;
        }

        // 4. PSO 설정 (CD3DX12 헬퍼로 대폭 축소!)
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        static const D3D12_INPUT_ELEMENT_DESC kInputLayout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };
        psoDesc.InputLayout = { kInputLayout, static_cast<UINT>(sizeof(kInputLayout) / sizeof(kInputLayout[0])) };
        psoDesc.pRootSignature = pRootSignature.Get();

        // 셰이더 컴파일
        ComPtr<ID3DBlob> vsBlob;
        ComPtr<ID3DBlob> psBlob;
        ComPtr<ID3DBlob> errorBlob;

        UINT compileFlags = D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES;
#if defined(_DEBUG) || defined(DEBUG)
        compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

        const bool hasPixelShader = desc.pixelShader != nullptr && desc.pixelShaderSize > 0u;

        HRESULT hr = D3DCompile(desc.vertexShader, desc.vertexShaderSize, nullptr, nullptr, nullptr, "main", "vs_5_1", compileFlags, 0, &vsBlob, &errorBlob);
        if (FAILED(hr)) {
            if (errorBlob) std::cout << "VS Compile Error: " << (char*)errorBlob->GetBufferPointer() << std::endl;
            return nullptr;
        }

        if (hasPixelShader) {
            hr = D3DCompile(desc.pixelShader, desc.pixelShaderSize, nullptr, nullptr, nullptr, "main", "ps_5_1", compileFlags, 0, &psBlob, &errorBlob);
            if (FAILED(hr)) {
                if (errorBlob) std::cout << "PS Compile Error: " << (char*)errorBlob->GetBufferPointer() << std::endl;
                return nullptr;
            }
        }

        psoDesc.VS = CD3DX12_SHADER_BYTECODE(vsBlob.Get());
        if (hasPixelShader) {
            psoDesc.PS = CD3DX12_SHADER_BYTECODE(psBlob.Get());
        }

        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK; // 뒷면 컬링 활성화
        psoDesc.RasterizerState.FrontCounterClockwise = TRUE; // 엔진 메시/Vulkan과 동일하게 CCW를 앞면으로
        if (!hasPixelShader)
        {
            // 깊이 전용(그림자) PSO: 양면 모두 캐스트하도록 컬링 해제 + 깊이 바이어스로 아크네 완화.
            psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
            psoDesc.RasterizerState.DepthBias = desc.depthBias;
            psoDesc.RasterizerState.SlopeScaledDepthBias = desc.depthBiasSlope;
            psoDesc.RasterizerState.DepthBiasClamp = desc.depthBiasClamp;
        }
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
        psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState.DepthEnable = desc.depthEnable ? TRUE : FALSE;
        psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        const RHI::Format dsvFormat = desc.depthStencilFormat == RHI::Format::Unknown
            ? RHI::Format::D24_UNORM_S8_UINT
            : desc.depthStencilFormat;
        psoDesc.DSVFormat = static_cast<DXGI_FORMAT>(D3D12Texture::ToDxgiDepthStencilFormat(dsvFormat));

        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = hasPixelShader ? 1u : 0u;
        if (hasPixelShader) {
            if (desc.renderTargetFormat == RHI::Format::Unknown) return nullptr;
            psoDesc.RTVFormats[0] = static_cast<DXGI_FORMAT>(
                D3D12Texture::ToDxgiFormat(desc.renderTargetFormat));
            if (psoDesc.RTVFormats[0] == DXGI_FORMAT_UNKNOWN) return nullptr;
        }
        psoDesc.SampleDesc.Count = 1;

        // 5. PSO 생성
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pPSO;
        if (FAILED(m_internal->device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pPSO)))) {
            std::cout << "[D3D12] CreateGraphicsPipelineState FAILED (hasPixelShader=" << hasPixelShader << ")" << std::endl;
            DumpInfoQueue(m_internal, "CreateGraphicsPipelineState");
            return nullptr;
        }

        // 6. 래퍼 객체로 반환
        DumpInfoQueue(m_internal, "CreateGraphicsPipeline");
        return new D3D12PipelineState(pPSO.Get(), pRootSignature.Get());
    }

    RHI::DescriptorIndex D3D12Device::AllocateDescriptorSlot() {
        if(m_internal->descriptorSlotOffset >= kGlobalDescriptorHeapSize - kTransientDescriptorSlotCount) return RHI::INVALID_DESCRIPTOR_INDEX;
        return m_internal->descriptorSlotOffset++;
    }

    void D3D12Device::UpdateDescriptorSlot(RHI::DescriptorIndex index, RHI::IBuffer* buffer) {
        if(index == RHI::INVALID_DESCRIPTOR_INDEX || buffer == nullptr) return;
        D3D12Buffer* dxBuffer = static_cast<D3D12Buffer*>(buffer);

        CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(m_internal->globalDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
        srvHandle.Offset(index, m_internal->srvDescriptorSize);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = dxBuffer->GetSize() / dxBuffer->GetStride();
        srvDesc.Buffer.StructureByteStride = dxBuffer->GetStride();
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

        m_internal->device->CreateShaderResourceView(
            static_cast<ID3D12Resource*>(dxBuffer->GetNativeResource()),
            &srvDesc,
            srvHandle
        );
    }

    void D3D12Device::UpdateDescriptorSlot(RHI::DescriptorIndex index, RHI::ITexture* texture) {
        if(index == RHI::INVALID_DESCRIPTOR_INDEX || texture == nullptr) return;
        // ITexture를 D3D12Texture로 다운캐스팅
        D3D12Texture* dxTexture = static_cast<D3D12Texture*>(texture);

        // 1. 글로벌 힙의 시작 주소 가져오기
        CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(m_internal->globalDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
        
        // 2. 인덱스(슬롯 번호)만큼 주소 이동
        srvHandle.Offset(index, m_internal->srvDescriptorSize);

        // 3. SRV(Shader Resource View) 생성
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        
        srvDesc.Format = static_cast<DXGI_FORMAT>(D3D12Texture::ToDxgiShaderResourceFormat(dxTexture->GetFormat()));
        
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;

        // 실제 리소스(ID3D12Resource)를 가져와서 뷰 생성
        m_internal->device->CreateShaderResourceView(
            static_cast<ID3D12Resource*>(dxTexture->GetNativeResource()),
            &srvDesc,
            srvHandle
        );

        // 커맨드 리스트가 SRV 디스크립터 테이블을 바인딩할 때 GPU 핸들을 계산할 수 있도록 슬롯 기억.
        dxTexture->SetGlobalSrvIndex(index);
    }

    RHI::ITexture* D3D12Device::CreateTexture(const RHI::TextureDesc& desc) {
        return new D3D12Texture(m_internal->device.Get(), desc);
    }

    bool D3D12Device::UpdateTexture(RHI::ITexture* texture, const void* data, uint32_t rowPitch) {
        if (m_internal == nullptr || texture == nullptr || data == nullptr ||
            rowPitch == 0 || m_internal->submissionFaulted)
        {
            return false;
        }
        auto d3dTexture = static_cast<D3D12Texture*>(texture);
        ID3D12Resource* destResource = static_cast<ID3D12Resource*>(d3dTexture->GetNativeResource());
        if (destResource == nullptr) return false;

        D3D12_RESOURCE_DESC desc = destResource->GetDesc();
        UINT64 requiredSize = 0;
        m_internal->device->GetCopyableFootprints(&desc, 0, 1, 0, nullptr, nullptr, nullptr, &requiredSize);

        std::cout << "[D3D12Device] UpdateTexture started. requiredSize=" << requiredSize << std::endl;

        // Upload Heap 버퍼 생성
        ComPtr<ID3D12Resource> uploadBuffer;
        CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
        CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(requiredSize);
        HRESULT hr = m_internal->device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc, 
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuffer));
        
        if (FAILED(hr)) return false;

        // 임시 커맨드 리스트 생성
        ComPtr<ID3D12CommandAllocator> alloc;
        if (FAILED(m_internal->device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc))))
        {
            return false;
        }
        ComPtr<ID3D12GraphicsCommandList> cmdList;
        if (FAILED(m_internal->device->CreateCommandList(
                0,
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                alloc.Get(),
                nullptr,
                IID_PPV_ARGS(&cmdList))))
        {
            return false;
        }

        // d3dx12.h 헬퍼를 사용해 완벽한 복사 수행
        D3D12_SUBRESOURCE_DATA subresourceData = {};
        subresourceData.pData = data;
        subresourceData.RowPitch = rowPitch;
        subresourceData.SlicePitch = rowPitch * desc.Height;

        UINT64 bytesCopied = UpdateSubresources(cmdList.Get(), destResource, uploadBuffer.Get(), 0, 0, 1, &subresourceData);
        if (bytesCopied == 0) return false;

        // 텍스처 상태 변경: COPY_DEST -> PIXEL_SHADER_RESOURCE
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = destResource;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &barrier);

        if (FAILED(cmdList->Close())) return false;

        m_internal->submissions.emplace_back();
        D3D12SubmissionRecord& submission = m_internal->submissions.back();
        submission.completionValue = m_internal->nextCompletionValue++;
        submission.retainedObjects.push_back(uploadBuffer);
        submission.retainedObjects.push_back(alloc);
        submission.retainedObjects.push_back(cmdList);

        // 커맨드 실행
        ID3D12CommandList* lists[] = { cmdList.Get() };
        m_internal->commandQueue->ExecuteCommandLists(1, lists);
        if (FAILED(m_internal->commandQueue->Signal(
                m_internal->fence.Get(), submission.completionValue)))
        {
            submission.completionValue = std::numeric_limits<uint64_t>::max();
            m_internal->submissionFaulted = true;
            return false;
        }
        
        d3dTexture->SetResourceState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        return true;
    }

    void D3D12Device::DestroyBuffer(RHI::IBuffer* buffer) { delete buffer; }

    void D3D12Device::DestroyTexture(RHI::ITexture* texture) {
        delete texture;
    }

    void D3D12Device::DestroyPipelineState(RHI::IPipelineState* pipeline) { delete pipeline; }

    RHI::ITexture* D3D12Device::GetBackBuffer() {
        if (m_internal == nullptr || !m_internal->swapchainReady ||
            m_internal->backBufferTextures.empty())
        {
            return nullptr;
        }
        const uint32_t imageIndex = (m_internal->frameReady || m_internal->frameSubmitted)
            ? m_internal->activeImageIndex
            : m_internal->swapChain->GetCurrentBackBufferIndex();
        return imageIndex < m_internal->backBufferTextures.size()
            ? m_internal->backBufferTextures[imageIndex].get()
            : nullptr;
    }
}
