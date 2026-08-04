#pragma once
#include <cstdint>
#include <vector>

#include "Core/Types.h"
#include "RHI/IDevice.h"
#include "RHI/ResourceState.h"

namespace dy::RHI
{
	class Texture;
}

namespace dy::Graphics
{
	class Scene;

	class GpuScene
	{
	public:
		[[nodiscard]] bool SyncTextures(const Scene& scene, RHI::IDevice* device);
		void Shutdown(RHI::IDevice* device);

		[[nodiscard]] RHI::TextureHandle ResolveTexture(TextureID textureId) const;
		[[nodiscard]] uint32_t GetTextureCount() const { return static_cast<uint32_t>(m_textures.size()); }

	private:
		struct TextureSlot
		{
			RHI::TextureHandle texture = nullptr;
			RHI::ResourceState state = RHI::ResourceState::Undefined;
			bool ready = false;
		};

		std::vector<TextureSlot> m_textures;
	};
}
