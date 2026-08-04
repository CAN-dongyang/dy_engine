#pragma once

#include <cstdint>

namespace dy::RHI
{
	enum class ResourceState : uint8_t
	{
		Undefined,
		Common,
		CopyDestination,
		VertexBuffer,
		IndexBuffer,
		ConstantBuffer,
		ShaderResource,
		UnorderedAccess,
		RenderTarget,
		DepthRead,
		DepthWrite,
		Present
	};

	struct TextureSubresourceRange
	{
		uint32_t firstMipLevel = 0;
		uint32_t mipLevelCount = 0;
		uint32_t firstArrayLayer = 0;
		uint32_t arrayLayerCount = 0;
	};
}
