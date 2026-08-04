#pragma once
#include <cstdint>

#include "Format.h"
#include "ShaderLayout.h"

namespace dy::RHI
{
	class ICommandList;

	class IBuffer;
	class ITexture;
	class IPipelineState;

	struct BufferDesc;
	struct TextureDesc;
	struct GraphicsPipelineDesc;

	enum class PresentMode : uint32_t
	{
		Immediate,
		Mailbox,
		Fifo
	};

	struct SwapchainDesc
	{
		Format format = Format::Unknown;
		uint32_t minimumImageCount = 2;
		PresentMode presentMode = PresentMode::Fifo;
	};

	struct DeviceDesc
	{
		uint32_t maxFramesInFlight = 2;
		uint32_t maxDrawsPerFrame = 128;
		uint32_t maxBindlessTextures = 128;
		uint32_t defaultShadowMapResolution = 2048;
		ShaderLayoutDesc shaderLayout = {};
	};

	class IDevice
	{
	public:
		virtual ~IDevice() = default;
		[[nodiscard]] static IDevice* Create(const void* windowHandle, const DeviceDesc& desc = {});
		[[nodiscard]] const DeviceDesc& GetDesc() const { return m_desc; }
		[[nodiscard]] virtual bool CreateSwapchain(const SwapchainDesc& desc) = 0;

		[[nodiscard]] virtual bool BeginFrame() = 0;
		[[nodiscard]] virtual RHI::ICommandList* AcquireCommandList()	= 0;

		[[nodiscard]] virtual bool Submit(ICommandList** cmdLists, uint32_t count) = 0;
		virtual void Present() = 0;

		[[nodiscard]] virtual ITexture* GetBackBuffer() = 0;

		[[nodiscard]] virtual IBuffer* CreateBuffer(const BufferDesc& desc) = 0;
		[[nodiscard]] virtual ITexture* CreateTexture(const TextureDesc& desc) = 0;
		[[nodiscard]] virtual IPipelineState* CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) = 0;
		
		virtual void DestroyBuffer(IBuffer* buffer) = 0;
		virtual void DestroyTexture(ITexture* texture) = 0;
		virtual void DestroyPipelineState(IPipelineState* pipeline) = 0;

		virtual bool UpdateTexture(ITexture* texture, const void* data, uint32_t rowPitch) = 0;

		// (Vulkan: 내부 처리 → false. D3D12: 명시 필요 → true. Phase 3에서 통일 예정.)
		[[nodiscard]] virtual bool RequiresExplicitShadowPass() const { return false; }
		// (D3D12/Metal: false. Vulkan: true)
		[[nodiscard]] virtual bool RequiresClipSpaceYFlip() const { return false; }

		[[nodiscard]] virtual DescriptorIndex AllocateDescriptorSlot() { return INVALID_DESCRIPTOR_INDEX; }
		virtual void UpdateDescriptorSlot(DescriptorIndex index, ITexture* texture) { (void)index; (void)texture; }
		virtual void UpdateDescriptorSlot(DescriptorIndex index, IBuffer* buffer) { (void)index; (void)buffer; }

	protected:
		virtual int Initialize(const void* windowHandle, const DeviceDesc& desc) = 0;

	private:
		void SetDesc(const DeviceDesc& desc) { m_desc = desc; }

		DeviceDesc m_desc = {};
	};
}
