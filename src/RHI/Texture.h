#pragma once
#include <cstdint>
#include "Format.h"
#include "ResourceHandles.h"
#include "ResourceState.h"

namespace dy::RHI
{
	// Texture binding usages (Bitmask)
	enum class TextureUsage : uint32_t {
		None				= 0,
		ShaderResource		= 1 << 0,
		RenderTarget		= 1 << 1,
		DepthStencil		= 1 << 2,
		Storage				= 1 << 3,
	};
	DY_RHI_ENABLE_ENUM_FLAGS(TextureUsage)

	// Descriptor for creating a texture
	struct TextureDesc {
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t depthOrArraySize = 1;
		uint32_t mipLevels = 1;
		Format format = Format::Unknown;
		TextureUsage usage = {};
	};

	class Texture
	{
	public:
		[[nodiscard]] const TextureDesc& GetDesc() const { return m_desc; }

	protected:
		virtual ~Texture() = default;
		explicit Texture(const TextureDesc& desc) : m_desc(desc) {}
		void SetDesc(const TextureDesc& desc) { m_desc = desc; }

	private:
		TextureDesc m_desc = {};
	};
}
