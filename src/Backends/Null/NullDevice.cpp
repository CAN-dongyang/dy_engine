#include "Backends/Null/NullDevice.h"

#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IPipelineState.h"
#include "RHI/ITexture.h"

#include <stdexcept>
#include <string>
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
			explicit NullTexture(const RHI::TextureDesc& desc)
				: RHI::ITexture(desc)
			{
			}
		};

		class NullPipelineState final : public RHI::IPipelineState
		{
		public:
			explicit NullPipelineState(const RHI::GraphicsPipelineDesc&) {}
		};

		class NullCommandList final : public RHI::ICommandList
		{
		public:
			void Reset()
			{
				m_debugEventStack.clear();
				m_debugMarkers.clear();
			}

			void BindGraphicsPipeline(RHI::IPipelineState*) override {}
			void BindGlobalDescriptors() override {}
			void BindGeometry(const RHI::GeometryBinding&) override {}
			void BindConstantBuffer(uint32_t, RHI::IBuffer*, uint32_t, uint32_t) override {}
			void BindTexture(uint32_t, RHI::ITexture*) override {}
			void SetInlineConstants(uint32_t, const void*) override {}
			void SetRenderTargets(uint32_t, RHI::ITexture**, RHI::ITexture*) override {}
			void SetViewport(const RHI::Viewport&) override {}
			void SetScissor(const RHI::Rect&) override {}
			void ClearColor(RHI::ITexture*, float, float, float, float) override {}
			void ClearDepth(RHI::ITexture*, float) override {}
			void BindVertexBuffer(RHI::IBuffer*, uint32_t, uint32_t) override {}
			void BindIndexBuffer(RHI::IBuffer*, RHI::Format, uint32_t) override {}
			void DrawInstanced(uint32_t, uint32_t, uint32_t, uint32_t) override {}
			void DrawIndexedInstanced(uint32_t, uint32_t, uint32_t, int32_t, uint32_t) override {}
			void BeginDebugEvent(const char* name, const RHI::DebugLabelColor&) override
			{
				if(name == nullptr || name[0] == '\0') throw std::invalid_argument("Null debug event name must not be empty.");
				m_debugEventStack.emplace_back(name);
			}
			void EndDebugEvent() override
			{
				if(m_debugEventStack.empty()) throw std::logic_error("Null debug event end has no matching begin.");
				m_debugEventStack.pop_back();
			}
			void InsertDebugMarker(const char* name, const RHI::DebugLabelColor&) override
			{
				if(name == nullptr || name[0] == '\0') throw std::invalid_argument("Null debug marker name must not be empty.");
				m_debugMarkers.emplace_back(name);
			}
			void Close() override
			{
				if(!m_debugEventStack.empty()) throw std::logic_error("Null command list closed with an unbalanced debug event.");
			}

		private:
			std::vector<std::string> m_debugEventStack;
			std::vector<std::string> m_debugMarkers;
		};
	}

	struct NullDevice::Impl
	{
		NullCommandList commandList;
		NullTexture backBuffer = NullTexture({
			1,
			1,
			1,
			1,
			RHI::Format::R8G8B8A8_UNORM,
			RHI::TextureUsage::RenderTarget
		});
		RHI::DescriptorIndex nextDescriptorIndex = 0;
	};

	NullDevice::NullDevice()
		: m_impl(new Impl())
	{
	}

	NullDevice::~NullDevice()
	{
		delete m_impl;
	}

	void NullDevice::BeginFrame()
	{
		m_impl->commandList.Reset();
	}

	uint32_t NullDevice::GetCurrentFrameIndex() const
	{
		return 0;
	}

	RHI::ICommandList* NullDevice::AcquireCommandList()
	{
		return &m_impl->commandList;
	}

	void NullDevice::Submit(RHI::ICommandList**, uint32_t) {}

	void NullDevice::Present() {}

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
		return &m_impl->backBuffer;
	}

	int NullDevice::Initialize(const void*, const RHI::DeviceDesc&)
	{
		return 0;
	}
}
