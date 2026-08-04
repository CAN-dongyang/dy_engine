#include "Backends/Null/NullDevice.h"

#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IPipelineState.h"
#include "RHI/ITexture.h"

#include <algorithm>
#include <memory>
#include <vector>

namespace dy::Backends
{
	namespace
	{
		class NullBuffer final : public RHI::IBuffer
		{
		public:
			explicit NullBuffer(const RHI::BufferDesc& desc)
				: RHI::IBuffer(desc)
			{
			}

			void* Map(uint32_t) override { return nullptr; }
			void Unmap() override {}
		};

		class NullTexture final : public RHI::ITexture
		{
		public:
			explicit NullTexture(const RHI::TextureDesc& desc, bool swapchainImage = false)
				: RHI::ITexture(desc)
				, m_swapchainImage(swapchainImage)
			{
			}

			[[nodiscard]] bool IsSwapchainImage() const { return m_swapchainImage; }

		private:
			bool m_swapchainImage = false;
		};

		class NullPipelineState final : public RHI::IPipelineState
		{
		public:
			explicit NullPipelineState(const RHI::GraphicsPipelineDesc&) {}
		};

		class NullCommandList final : public RHI::ICommandList
		{
		public:
			void BindGraphicsPipeline(RHI::IPipelineState*) override {}
			void BindGlobalDescriptors() override {}
			void BindGeometry(const RHI::GeometryBinding&) override {}
			void BindConstantBuffer(uint32_t, RHI::IBuffer*, uint32_t, uint32_t) override {}
			void BindTexture(uint32_t, RHI::ITexture* texture) override { TrackTexture(texture); }
			void SetInlineConstants(uint32_t, const void*) override {}
			void SetRenderTargets(
				uint32_t count,
				RHI::ITexture** renderTargets,
				RHI::ITexture* depthStencil) override
			{
				for (uint32_t index = 0; index < count && renderTargets != nullptr; ++index)
				{
					TrackTexture(renderTargets[index]);
				}
				TrackTexture(depthStencil);
			}
			void SetViewport(const RHI::Viewport&) override {}
			void SetScissor(const RHI::Rect&) override {}
			void ClearColor(RHI::ITexture* texture, float, float, float, float) override { TrackTexture(texture); }
			void ClearDepth(RHI::ITexture* texture, float) override { TrackTexture(texture); }
			void BindVertexBuffer(RHI::IBuffer*, uint32_t, uint32_t) override {}
			void BindIndexBuffer(RHI::IBuffer*, RHI::Format, uint32_t) override {}
			void DrawInstanced(uint32_t, uint32_t, uint32_t, uint32_t) override {}
			void DrawIndexedInstanced(uint32_t, uint32_t, uint32_t, int32_t, uint32_t) override {}
			void Close() override { m_closed = true; }

			[[nodiscard]] bool IsClosed() const { return m_closed; }
			[[nodiscard]] const std::vector<NullTexture*>& GetReferencedSwapchainImages() const
			{
				return m_referencedSwapchainImages;
			}

		private:
			void TrackTexture(RHI::ITexture* texture)
			{
				if (texture == nullptr) return;
				auto* nullTexture = static_cast<NullTexture*>(texture);
				if (!nullTexture->IsSwapchainImage()) return;
				if (std::find(
						m_referencedSwapchainImages.begin(),
						m_referencedSwapchainImages.end(),
						nullTexture) == m_referencedSwapchainImages.end())
				{
					m_referencedSwapchainImages.push_back(nullTexture);
				}
			}

			std::vector<NullTexture*> m_referencedSwapchainImages;
			bool m_closed = false;
		};

		struct NullFrameSlot
		{
			uint64_t completionValue = 0;
		};
	}

	struct NullDevice::Impl
	{
		const void* windowHandle = nullptr;
		std::vector<std::unique_ptr<NullTexture>> backBuffers;
		std::vector<uint64_t> imageCompletionValues;
		std::vector<NullFrameSlot> frames;
		std::vector<std::unique_ptr<NullCommandList>> activeCommandLists;
		uint64_t nextCompletionValue = 1;
		uint64_t completedValue = 0;
		uint32_t nextFrameIndex = 0;
		uint32_t activeFrameIndex = 0;
		uint32_t activeImageIndex = 0;
		uint32_t nextImageIndex = 0;
		RHI::DescriptorIndex nextDescriptorIndex = 0;
		bool initialized = false;
		bool swapchainReady = false;
		bool frameReady = false;
		bool frameSubmitted = false;
	};

	NullDevice::NullDevice()
		: m_impl(new Impl())
	{
	}

	NullDevice::~NullDevice()
	{
		delete m_impl;
	}

	bool NullDevice::CreateSwapchain(const RHI::SwapchainDesc& desc)
	{
		if (m_impl == nullptr || !m_impl->initialized || m_impl->swapchainReady ||
			desc.minimumImageCount == 0)
		{
			return false;
		}

		RHI::Format format = desc.format;
		if (format == RHI::Format::Unknown) format = RHI::Format::R8G8B8A8_UNORM;
		switch (desc.presentMode)
		{
		case RHI::PresentMode::Immediate:
		case RHI::PresentMode::Mailbox:
		case RHI::PresentMode::Fifo:
			break;
		default:
			return false;
		}
		switch (format)
		{
		case RHI::Format::R8G8B8A8_UNORM:
		case RHI::Format::B8G8R8A8_UNORM:
		case RHI::Format::R8G8B8A8_UNORM_SRGB:
		case RHI::Format::B8G8R8A8_UNORM_SRGB:
		case RHI::Format::R16G16B16A16_FLOAT:
		case RHI::Format::R32G32B32A32_FLOAT:
			break;
		default:
			return false;
		}

		RHI::TextureDesc backBufferDesc = {};
		backBufferDesc.format = format;
		backBufferDesc.usage = RHI::TextureUsage::RenderTarget;

		std::vector<std::unique_ptr<NullTexture>> backBuffers;
		backBuffers.reserve(desc.minimumImageCount);
		for (uint32_t index = 0; index < desc.minimumImageCount; ++index)
		{
			backBuffers.push_back(std::make_unique<NullTexture>(backBufferDesc, true));
		}

		m_impl->backBuffers = std::move(backBuffers);
		m_impl->imageCompletionValues.assign(desc.minimumImageCount, 0);
		m_impl->nextImageIndex = 0;
		m_impl->swapchainReady = true;
		return true;
	}

	bool NullDevice::BeginFrame()
	{
		if (m_impl == nullptr || !m_impl->swapchainReady || m_impl->frameReady ||
			m_impl->frameSubmitted || m_impl->frames.empty() ||
			m_impl->backBuffers.empty())
		{
			return false;
		}

		const uint32_t frameIndex = m_impl->nextFrameIndex;
		const uint32_t imageIndex = m_impl->nextImageIndex;
		if (m_impl->frames[frameIndex].completionValue > m_impl->completedValue ||
			m_impl->imageCompletionValues[imageIndex] > m_impl->completedValue)
		{
			return false;
		}

		m_impl->activeFrameIndex = frameIndex;
		m_impl->activeImageIndex = imageIndex;
		m_impl->frameReady = true;
		return true;
	}

	RHI::ICommandList* NullDevice::AcquireCommandList()
	{
		if (m_impl == nullptr || !m_impl->initialized) return nullptr;

		auto commandList = std::make_unique<NullCommandList>();
		NullCommandList* result = commandList.get();
		m_impl->activeCommandLists.push_back(std::move(commandList));
		return result;
	}

	bool NullDevice::Submit(RHI::ICommandList** cmdLists, uint32_t count)
	{
		if (m_impl == nullptr || !m_impl->initialized || cmdLists == nullptr ||
			count == 0)
		{
			return false;
		}

		NullTexture* activeBackBuffer = nullptr;
		if (m_impl->frameReady && m_impl->activeImageIndex < m_impl->backBuffers.size())
		{
			activeBackBuffer = m_impl->backBuffers[m_impl->activeImageIndex].get();
		}

		std::vector<NullCommandList*> submittedCommandLists;
		submittedCommandLists.reserve(count);
		bool frameSubmission = false;
		for (uint32_t index = 0; index < count; ++index)
		{
			if (cmdLists[index] == nullptr) return false;
			for (uint32_t previous = 0; previous < index; ++previous)
			{
				if (cmdLists[previous] == cmdLists[index]) return false;
			}

			const auto owned = std::find_if(
				m_impl->activeCommandLists.begin(),
				m_impl->activeCommandLists.end(),
				[command = cmdLists[index]](const std::unique_ptr<NullCommandList>& candidate)
				{
					return candidate.get() == command;
				});
			if (owned == m_impl->activeCommandLists.end() || !(*owned)->IsClosed())
			{
				return false;
			}

			for (NullTexture* image : (*owned)->GetReferencedSwapchainImages())
			{
				if (activeBackBuffer == nullptr || image != activeBackBuffer) return false;
				frameSubmission = true;
			}
			submittedCommandLists.push_back(owned->get());
		}

		const uint64_t completionValue = m_impl->nextCompletionValue++;
		m_impl->completedValue = completionValue;
		for (NullCommandList* commandList : submittedCommandLists)
		{
			const auto owned = std::find_if(
				m_impl->activeCommandLists.begin(),
				m_impl->activeCommandLists.end(),
				[commandList](const std::unique_ptr<NullCommandList>& candidate)
				{
					return candidate.get() == commandList;
				});
			m_impl->activeCommandLists.erase(owned);
		}
		if (frameSubmission)
		{
			m_impl->frames[m_impl->activeFrameIndex].completionValue = completionValue;
			m_impl->imageCompletionValues[m_impl->activeImageIndex] = completionValue;
			m_impl->nextFrameIndex =
				(m_impl->activeFrameIndex + 1) % static_cast<uint32_t>(m_impl->frames.size());
			m_impl->frameReady = false;
			m_impl->frameSubmitted = true;
		}
		return true;
	}

	void NullDevice::Present()
	{
		if (m_impl == nullptr || !m_impl->frameSubmitted) return;

		m_impl->nextImageIndex =
			(m_impl->activeImageIndex + 1) % static_cast<uint32_t>(m_impl->backBuffers.size());
		m_impl->frameSubmitted = false;
	}

	RHI::IBuffer* NullDevice::CreateBuffer(const RHI::BufferDesc& desc)
	{
		return new NullBuffer(desc);
	}

	RHI::ITexture* NullDevice::CreateTexture(const RHI::TextureDesc& desc)
	{
		return new NullTexture(desc);
	}

	bool NullDevice::UpdateTexture(RHI::ITexture*, const void*, uint32_t)
	{
		return true;
	}

	RHI::IPipelineState* NullDevice::CreateGraphicsPipeline(const RHI::GraphicsPipelineDesc& desc)
	{
		return new NullPipelineState(desc);
	}

	RHI::DescriptorIndex NullDevice::AllocateDescriptorSlot()
	{
		return m_impl->nextDescriptorIndex++;
	}

	void NullDevice::UpdateDescriptorSlot(RHI::DescriptorIndex, RHI::ITexture*) {}
	void NullDevice::UpdateDescriptorSlot(RHI::DescriptorIndex, RHI::IBuffer*) {}

	void NullDevice::DestroyBuffer(RHI::IBuffer* buffer)
	{
		delete buffer;
	}

	void NullDevice::DestroyTexture(RHI::ITexture* texture)
	{
		delete texture;
	}

	void NullDevice::DestroyPipelineState(RHI::IPipelineState* pipeline)
	{
		delete pipeline;
	}

	RHI::ITexture* NullDevice::GetBackBuffer()
	{
		if (m_impl == nullptr || !m_impl->swapchainReady || m_impl->backBuffers.empty())
		{
			return nullptr;
		}
		const uint32_t imageIndex = (m_impl->frameReady || m_impl->frameSubmitted)
			? m_impl->activeImageIndex
			: m_impl->nextImageIndex;
		return imageIndex < m_impl->backBuffers.size()
			? m_impl->backBuffers[imageIndex].get()
			: nullptr;
	}

	int NullDevice::Initialize(const void* windowHandle, const RHI::DeviceDesc& desc)
	{
		if (m_impl == nullptr || windowHandle == nullptr || desc.maxFramesInFlight == 0)
		{
			return -1;
		}
		m_impl->windowHandle = windowHandle;
		m_impl->frames.resize(desc.maxFramesInFlight);
		m_impl->initialized = true;
		return 0;
	}
}
