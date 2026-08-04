#include "MetalDevice.h"
#include "MetalBuffer.h"
#include "MetalTexture.h"
#include "MetalPipeline.h"
#include "MetalCommandList.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <dispatch/dispatch.h>

namespace dy::Backends
{
    namespace
    {
        RHI::Format FromLayerPixelFormat(MTLPixelFormat format)
        {
            switch(format)
            {
            case MTLPixelFormatBGRA8Unorm: return RHI::Format::B8G8R8A8_UNORM;
            case MTLPixelFormatBGRA8Unorm_sRGB: return RHI::Format::B8G8R8A8_UNORM_SRGB;
            case MTLPixelFormatRGBA16Float: return RHI::Format::R16G16B16A16_FLOAT;
            default: return RHI::Format::Unknown;
            }
        }

        MTLPixelFormat ToLayerPixelFormat(RHI::Format format)
        {
            switch(format)
            {
            case RHI::Format::B8G8R8A8_UNORM: return MTLPixelFormatBGRA8Unorm;
            case RHI::Format::B8G8R8A8_UNORM_SRGB: return MTLPixelFormatBGRA8Unorm_sRGB;
            case RHI::Format::R16G16B16A16_FLOAT: return MTLPixelFormatRGBA16Float;
            default: return MTLPixelFormatInvalid;
            }
        }

        bool IsFinished(id<MTLCommandBuffer> commandBuffer)
        {
            if(commandBuffer == nil) return false;
            const MTLCommandBufferStatus status = commandBuffer.status;
            return status == MTLCommandBufferStatusCompleted ||
                status == MTLCommandBufferStatusError;
        }
    }

    struct MetalDevice::Impl
    {
        struct FrameSlot
        {
            uint64_t submissionValue = 0;
        };

        struct Submission
        {
            uint64_t value = 0;
            std::vector<std::unique_ptr<MetalCommandList>> commandLists;
            id<MTLCommandBuffer> presentCommandBuffer = nil;
            id<CAMetalDrawable> drawable = nil;
            bool waitingForPresent = false;
        };

        id<MTLDevice> device = nil;
        id<MTLCommandQueue> commandQueue = nil;
        const void* windowHandle = nullptr;

        CAMetalLayer* metalLayer = nil;
        id<CAMetalDrawable> currentDrawable = nil;
        MetalTexture* backBufferTex = nullptr;

        dispatch_queue_t drawableQueue = nullptr;
        std::mutex drawableMutex;
        id<CAMetalDrawable> readyDrawable = nil;
        bool drawableRequestPending = false;
        bool stoppingDrawableAcquisition = false;

        std::vector<FrameSlot> frameSlots;
        std::vector<std::unique_ptr<MetalCommandList>> activeCommandLists;
        uint32_t nextFrameSlot = 0;
        uint32_t activeFrameSlot = 0;
        bool frameActive = false;

        std::vector<Submission> submissions;
        uint64_t nextSubmissionValue = 1;
        uint64_t completedSubmissionValue = 0;
        uint64_t pendingPresentValue = 0;
        bool asyncWorkFailed = false;

        RHI::DescriptorIndex nextDescriptorIndex = 0;
        std::vector<RHI::ITexture*> textures;

        void RequestDrawable()
        {
            CAMetalLayer* layerToRequest = nil;
            dispatch_queue_t queue = nullptr;
            {
                std::unique_lock<std::mutex> lock(
                    drawableMutex, std::try_to_lock);
                if(!lock.owns_lock()) return;
                if(stoppingDrawableAcquisition || drawableRequestPending ||
                    readyDrawable != nil || drawableQueue == nullptr || metalLayer == nil)
                {
                    return;
                }

                drawableRequestPending = true;
                layerToRequest = metalLayer;
                queue = drawableQueue;
#if !__has_feature(objc_arc)
                [layerToRequest retain];
#endif
            }

            dispatch_async(queue, ^{
                @autoreleasepool
                {
                    id<CAMetalDrawable> drawable = nil;
                    {
                        std::lock_guard<std::mutex> lock(drawableMutex);
                        if(!stoppingDrawableAcquisition)
                            drawable = [layerToRequest nextDrawable];
                        if(!stoppingDrawableAcquisition && drawable != nil && readyDrawable == nil)
                        {
#if !__has_feature(objc_arc)
                            [drawable retain];
#endif
                            readyDrawable = drawable;
                        }
                        drawableRequestPending = false;
                    }
#if !__has_feature(objc_arc)
                    [layerToRequest release];
#endif
                }
            });
        }

        void StopDrawableAcquisition()
        {
            {
                std::lock_guard<std::mutex> lock(drawableMutex);
                stoppingDrawableAcquisition = true;
            }

            if(drawableQueue != nullptr)
                dispatch_sync(drawableQueue, ^{});

            std::lock_guard<std::mutex> lock(drawableMutex);
#if !__has_feature(objc_arc)
            [readyDrawable release];
#endif
            readyDrawable = nil;
            drawableRequestPending = false;
        }

        void ClearCurrentDrawable()
        {
#if !__has_feature(objc_arc)
            [currentDrawable release];
#endif
            currentDrawable = nil;
        }

        static void ReleaseSubmission(Submission& submission)
        {
#if !__has_feature(objc_arc)
            [submission.presentCommandBuffer release];
            [submission.drawable release];
#endif
            submission.presentCommandBuffer = nil;
            submission.drawable = nil;
            submission.commandLists.clear();
        }

        void CollectCompletedSubmissions()
        {
            while(!submissions.empty())
            {
                Submission& submission = submissions.front();
                if(submission.waitingForPresent)
                    break;

                id<MTLCommandBuffer> completion = submission.presentCommandBuffer;
                if(completion == nil && !submission.commandLists.empty())
                {
                    completion = (__bridge id<MTLCommandBuffer>)
                        submission.commandLists.back()->GetNativeCommandBuffer();
                }
                if(!IsFinished(completion))
                    break;

                for(const std::unique_ptr<MetalCommandList>& commandList :
                    submission.commandLists)
                {
                    id<MTLCommandBuffer> commandBuffer =
                        (__bridge id<MTLCommandBuffer>)
                            commandList->GetNativeCommandBuffer();
                    if(commandBuffer.status == MTLCommandBufferStatusError)
                    {
                        asyncWorkFailed = true;
                        NSLog(@"Metal submission failed: %@", commandBuffer.error);
                    }
                }
                if(submission.presentCommandBuffer.status ==
                    MTLCommandBufferStatusError)
                {
                    asyncWorkFailed = true;
                    NSLog(@"Metal presentation failed: %@",
                        submission.presentCommandBuffer.error);
                }

                completedSubmissionValue = submission.value;
                ReleaseSubmission(submission);
                submissions.erase(submissions.begin());
            }
        }

        Submission* FindSubmission(uint64_t value)
        {
            const auto it = std::find_if(
                submissions.begin(), submissions.end(),
                [value](const Submission& submission)
                {
                    return submission.value == value;
                });
            return it == submissions.end() ? nullptr : &*it;
        }

        bool DrainGpuForShutdown()
        {
            if(commandQueue == nil) return submissions.empty();

            id<MTLCommandBuffer> marker = [commandQueue commandBuffer];
            if(marker == nil) return false;
            [marker commit];
            [marker waitUntilCompleted];
            return marker.status == MTLCommandBufferStatusCompleted;
        }
    };

    MetalDevice::MetalDevice()
        : m_impl(new Impl())
    {
    }

    MetalDevice::~MetalDevice()
    {
        if(m_impl == nullptr) return;

        m_impl->StopDrawableAcquisition();
        if(!m_impl->submissions.empty() && !m_impl->DrainGpuForShutdown())
        {
            m_impl = nullptr;
            return;
        }

        m_impl->activeCommandLists.clear();

        m_impl->ClearCurrentDrawable();
        for(Impl::Submission& submission : m_impl->submissions)
            Impl::ReleaseSubmission(submission);
        m_impl->submissions.clear();

        if(m_impl->backBufferTex != nullptr)
        {
            RHI::TextureDesc desc = m_impl->backBufferTex->GetDesc();
            m_impl->backBufferTex->SetBackBuffer(nullptr, desc);
        }
        delete m_impl->backBufferTex;
        m_impl->backBufferTex = nullptr;

        if(m_impl->metalLayer != nil)
            m_impl->metalLayer.device = nil;
#if !__has_feature(objc_arc)
        [m_impl->metalLayer release];
        if(m_impl->drawableQueue != nullptr)
            dispatch_release(m_impl->drawableQueue);
        [m_impl->commandQueue release];
        [m_impl->device release];
#endif
        m_impl->metalLayer = nil;
        m_impl->drawableQueue = nullptr;
        m_impl->commandQueue = nil;
        m_impl->device = nil;
        delete m_impl;
    }

    int MetalDevice::Initialize(const void* windowHandle, const RHI::DeviceDesc& desc)
    {
        if(windowHandle == nullptr || desc.maxFramesInFlight == 0)
            return -1;

        m_impl->windowHandle = windowHandle;
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if(device == nil) return -1;
        m_impl->device = device;

        m_impl->commandQueue = [device newCommandQueue];
        if(m_impl->commandQueue == nil) return -1;

        m_impl->frameSlots.resize(desc.maxFramesInFlight);
        m_impl->activeCommandLists.reserve(desc.maxFramesInFlight);
        m_impl->submissions.reserve(desc.maxFramesInFlight);
        return 0;
    }

    bool MetalDevice::CreateSwapchain(const RHI::SwapchainDesc& desc)
    {
        if(m_impl->device == nil || m_impl->commandQueue == nil ||
            m_impl->windowHandle == nullptr || m_impl->metalLayer != nil ||
            m_impl->backBufferTex != nullptr || ![NSThread isMainThread] ||
            desc.minimumImageCount == 0)
        {
            return false;
        }

        NSWindow* window = (__bridge NSWindow*)m_impl->windowHandle;
        NSView* contentView = window.contentView;
        if(contentView == nil) return false;

        CAMetalLayer* layer = [CAMetalLayer layer];
        if(layer == nil) return false;
        layer.device = m_impl->device;

        if(desc.format != RHI::Format::Unknown)
        {
            const MTLPixelFormat requestedFormat = ToLayerPixelFormat(desc.format);
            if(requestedFormat == MTLPixelFormatInvalid) return false;
            layer.pixelFormat = requestedFormat;
            if(layer.pixelFormat != requestedFormat) return false;
        }

        if(@available(macOS 10.13.2, *))
        {
            layer.allowsNextDrawableTimeout = YES;
            switch(desc.presentMode)
            {
            case RHI::PresentMode::Fifo:
                layer.displaySyncEnabled = YES;
                break;
            case RHI::PresentMode::Immediate:
                layer.displaySyncEnabled = NO;
                break;
            case RHI::PresentMode::Mailbox:
            default:
                return false;
            }
            if((desc.presentMode == RHI::PresentMode::Fifo) !=
                (layer.displaySyncEnabled != NO))
            {
                return false;
            }

            if(layer.maximumDrawableCount < desc.minimumImageCount)
                layer.maximumDrawableCount = desc.minimumImageCount;
            if(layer.maximumDrawableCount < desc.minimumImageCount) return false;
        }
        else
        {
            return false;
        }

        const CGFloat scale = window.backingScaleFactor;
        layer.contentsScale = scale;
        layer.frame = contentView.bounds;
        layer.drawableSize = CGSizeMake(
            contentView.bounds.size.width * scale,
            contentView.bounds.size.height * scale);

        const RHI::Format actualFormat = FromLayerPixelFormat(layer.pixelFormat);
        if(actualFormat == RHI::Format::Unknown) return false;

        RHI::TextureDesc backBufferDesc = {};
        backBufferDesc.width = static_cast<uint32_t>(layer.drawableSize.width);
        backBufferDesc.height = static_cast<uint32_t>(layer.drawableSize.height);
        backBufferDesc.depthOrArraySize = 1;
        backBufferDesc.mipLevels = 1;
        backBufferDesc.format = actualFormat;
        backBufferDesc.usage = RHI::TextureUsage::RenderTarget;

        auto backBuffer = std::unique_ptr<MetalTexture>(
            new MetalTexture(backBufferDesc));
        dispatch_queue_t drawableQueue = dispatch_queue_create(
            "dy.engine.metal.drawable", DISPATCH_QUEUE_SERIAL);
        if(drawableQueue == nullptr) return false;

        [contentView setWantsLayer:YES];
        [contentView setLayer:layer];
#if !__has_feature(objc_arc)
        [layer retain];
#endif
        m_impl->metalLayer = layer;
        m_impl->backBufferTex = backBuffer.release();
        m_impl->drawableQueue = drawableQueue;
        m_impl->RequestDrawable();
        return true;
    }

    bool MetalDevice::BeginFrame()
    {
        m_impl->CollectCompletedSubmissions();
        if(m_impl->asyncWorkFailed || m_impl->frameActive ||
            m_impl->pendingPresentValue != 0 || m_impl->metalLayer == nil ||
            m_impl->backBufferTex == nullptr || m_impl->frameSlots.empty() ||
            m_impl->windowHandle == nullptr || ![NSThread isMainThread])
        {
            return false;
        }

        NSWindow* window = (__bridge NSWindow*)m_impl->windowHandle;
        NSView* contentView = window.contentView;
        if(contentView == nil) return false;

        Impl::FrameSlot& slot = m_impl->frameSlots[m_impl->nextFrameSlot];
        if(slot.submissionValue > m_impl->completedSubmissionValue)
            return false;

        id<CAMetalDrawable> drawable = nil;
        uint32_t expectedWidth = 0;
        uint32_t expectedHeight = 0;
        {
            std::unique_lock<std::mutex> lock(
                m_impl->drawableMutex, std::try_to_lock);
            if(!lock.owns_lock()) return false;

            const CGFloat scale = window.backingScaleFactor;
            const CGRect bounds = contentView.bounds;
            const CGSize drawableSize = CGSizeMake(
                bounds.size.width * scale,
                bounds.size.height * scale);
            m_impl->metalLayer.contentsScale = scale;
            m_impl->metalLayer.frame = bounds;
            m_impl->metalLayer.drawableSize = drawableSize;
            if(m_impl->metalLayer.drawableSize.width <= 0.0 ||
                m_impl->metalLayer.drawableSize.height <= 0.0)
            {
                return false;
            }

            expectedWidth = static_cast<uint32_t>(
                m_impl->metalLayer.drawableSize.width);
            expectedHeight = static_cast<uint32_t>(
                m_impl->metalLayer.drawableSize.height);
            drawable = m_impl->readyDrawable;
            m_impl->readyDrawable = nil;
        }
        m_impl->RequestDrawable();
        if(drawable == nil)
        {
            return false;
        }

        m_impl->currentDrawable = drawable;

        id<MTLTexture> texture = drawable.texture;
        const RHI::Format actualFormat = texture == nil
            ? RHI::Format::Unknown
            : FromLayerPixelFormat(texture.pixelFormat);
        if(texture == nil || texture.width == 0 || texture.height == 0 ||
            texture.width != expectedWidth || texture.height != expectedHeight ||
            actualFormat == RHI::Format::Unknown ||
            actualFormat != m_impl->backBufferTex->GetFormat())
        {
            m_impl->ClearCurrentDrawable();
            return false;
        }

        RHI::TextureDesc backBufferDesc = m_impl->backBufferTex->GetDesc();
        backBufferDesc.width = static_cast<uint32_t>(texture.width);
        backBufferDesc.height = static_cast<uint32_t>(texture.height);
        backBufferDesc.format = actualFormat;
        m_impl->backBufferTex->SetBackBuffer(
            (__bridge void*)texture, backBufferDesc);

        m_impl->activeFrameSlot = m_impl->nextFrameSlot;
        m_impl->frameActive = true;
        return true;
    }

    RHI::ICommandList* MetalDevice::AcquireCommandList()
    {
        m_impl->CollectCompletedSubmissions();
        if(m_impl->device == nil || m_impl->commandQueue == nil ||
            m_impl->asyncWorkFailed)
        {
            return nullptr;
        }

        auto commandList = std::make_unique<MetalCommandList>(
            (__bridge void*)m_impl->commandQueue);
        if(!commandList->Begin()) return nullptr;

        for(uint32_t index = 0; index < m_impl->textures.size(); ++index)
        {
            RHI::ITexture* boundTexture = m_impl->textures[index];
            if(boundTexture == nullptr) continue;
            auto* metalTexture = static_cast<MetalTexture*>(boundTexture);
            commandList->SetNativeTexture(
                metalTexture->GetNativeTexture(), index);
        }

        MetalCommandList* result = commandList.get();
        m_impl->activeCommandLists.push_back(std::move(commandList));
        return result;
    }

    bool MetalDevice::Submit(RHI::ICommandList** cmdLists, uint32_t count)
    {
        if(m_impl->asyncWorkFailed || cmdLists == nullptr || count == 0)
        {
            return false;
        }

        std::vector<MetalCommandList*> submittedCommandLists;
        submittedCommandLists.reserve(count);
        bool usesBackBuffer = false;
        for(uint32_t index = 0; index < count; ++index)
        {
            if(cmdLists[index] == nullptr) return false;
            for(uint32_t previous = 0; previous < index; ++previous)
            {
                if(cmdLists[previous] == cmdLists[index]) return false;
            }

            const auto owned = std::find_if(
                m_impl->activeCommandLists.begin(),
                m_impl->activeCommandLists.end(),
                [command = cmdLists[index]](const std::unique_ptr<MetalCommandList>& candidate)
                {
                    return candidate.get() == command;
                });
            if(owned == m_impl->activeCommandLists.end()) return false;

            MetalCommandList* commandList = owned->get();
            if(!commandList->IsClosed() ||
                commandList->GetNativeCommandBuffer() == nullptr)
            {
                return false;
            }
            submittedCommandLists.push_back(commandList);
            usesBackBuffer = usesBackBuffer || commandList->UsesBackBuffer();
        }

        if(usesBackBuffer &&
            (!m_impl->frameActive || m_impl->currentDrawable == nil ||
                m_impl->backBufferTex == nullptr || m_impl->frameSlots.empty()))
        {
            return false;
        }

        m_impl->submissions.emplace_back();
        Impl::Submission& submission = m_impl->submissions.back();
        submission.value = m_impl->nextSubmissionValue++;
        submission.commandLists.reserve(count);
        for(MetalCommandList* commandList : submittedCommandLists)
        {
            const auto owned = std::find_if(
                m_impl->activeCommandLists.begin(),
                m_impl->activeCommandLists.end(),
                [commandList](const std::unique_ptr<MetalCommandList>& candidate)
                {
                    return candidate.get() == commandList;
                });
            submission.commandLists.push_back(std::move(*owned));
            m_impl->activeCommandLists.erase(owned);
        }

        for(const std::unique_ptr<MetalCommandList>& commandList :
            submission.commandLists)
        {
            id<MTLCommandBuffer> commandBuffer =
                (__bridge id<MTLCommandBuffer>)
                    commandList->GetNativeCommandBuffer();
            [commandBuffer commit];
        }

        if(usesBackBuffer)
        {
            submission.drawable = m_impl->currentDrawable;
            submission.waitingForPresent = true;
            m_impl->currentDrawable = nil;

            m_impl->frameSlots[m_impl->activeFrameSlot].submissionValue =
                submission.value;
            m_impl->pendingPresentValue = submission.value;
            m_impl->nextFrameSlot = static_cast<uint32_t>(
                (m_impl->activeFrameSlot + 1) % m_impl->frameSlots.size());
            m_impl->frameActive = false;

            const RHI::TextureDesc backBufferDesc =
                m_impl->backBufferTex->GetDesc();
            m_impl->backBufferTex->SetBackBuffer(nullptr, backBufferDesc);
        }
        return true;
    }

    void MetalDevice::Present()
    {
        if(m_impl->pendingPresentValue == 0) return;

        Impl::Submission* submission = m_impl->FindSubmission(
            m_impl->pendingPresentValue);
        m_impl->pendingPresentValue = 0;
        if(submission == nullptr)
        {
            m_impl->asyncWorkFailed = true;
            return;
        }
        if(submission->drawable == nil)
        {
            submission->waitingForPresent = false;
            m_impl->asyncWorkFailed = true;
            return;
        }

        id<MTLCommandBuffer> commandBuffer =
            [m_impl->commandQueue commandBuffer];
        if(commandBuffer == nil)
        {
            submission->waitingForPresent = false;
            m_impl->asyncWorkFailed = true;
            return;
        }
#if !__has_feature(objc_arc)
        [commandBuffer retain];
#endif
        submission->presentCommandBuffer = commandBuffer;
        [commandBuffer presentDrawable:submission->drawable];
        [commandBuffer commit];
        submission->waitingForPresent = false;
    }

    RHI::IBuffer* MetalDevice::CreateBuffer(const RHI::BufferDesc& desc)
    {
        return new MetalBuffer(desc, (__bridge void*)m_impl->device);
    }

    RHI::ITexture* MetalDevice::CreateTexture(const RHI::TextureDesc& desc)
    {
        return new MetalTexture(desc, (__bridge void*)m_impl->device);
    }

    RHI::IPipelineState* MetalDevice::CreateGraphicsPipeline(
        const RHI::GraphicsPipelineDesc& desc)
    {
        auto pipeline = std::make_unique<MetalPipeline>(
            desc, (__bridge void*)m_impl->device);
        if(pipeline->GetNativePipeline() == nullptr) return nullptr;
        return pipeline.release();
    }

    void MetalDevice::DestroyBuffer(RHI::IBuffer* buffer)
    {
        delete buffer;
    }

    void MetalDevice::DestroyTexture(RHI::ITexture* texture)
    {
        delete texture;
    }

    void MetalDevice::DestroyPipelineState(RHI::IPipelineState* pipeline)
    {
        delete pipeline;
    }

    bool MetalDevice::UpdateTexture(
        RHI::ITexture* texture, const void* data, uint32_t rowPitch)
    {
        if(texture == nullptr || data == nullptr || rowPitch == 0)
            return false;

        auto* metalTexture = static_cast<MetalTexture*>(texture);
        id<MTLTexture> nativeTexture =
            (__bridge id<MTLTexture>)metalTexture->GetNativeTexture();
        if(nativeTexture == nil) return false;

        const MTLRegion region = MTLRegionMake2D(
            0, 0, texture->GetWidth(), texture->GetHeight());
        [nativeTexture replaceRegion:region
                          mipmapLevel:0
                            withBytes:data
                          bytesPerRow:rowPitch];
        return true;
    }

    RHI::DescriptorIndex MetalDevice::AllocateDescriptorSlot()
    {
        return m_impl->nextDescriptorIndex++;
    }

    void MetalDevice::UpdateDescriptorSlot(
        RHI::DescriptorIndex index, RHI::ITexture* texture)
    {
        if(index >= m_impl->textures.size())
            m_impl->textures.resize(index + 1, nullptr);
        m_impl->textures[index] = texture;
    }

    RHI::ITexture* MetalDevice::GetBackBuffer()
    {
        return m_impl->metalLayer == nil ? nullptr : m_impl->backBufferTex;
    }
}
