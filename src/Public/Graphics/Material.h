#pragma once

#include <cstdint>

#include "Graphics/Texture.h"
#include "Math/Math.h"

namespace dy::Graphics
{
	enum class MaterialID : uint32_t
	{
		Invalid = 0xFFFFFFFFu
	};

	[[nodiscard]] inline constexpr uint32_t ToIndex(MaterialID id)
	{
		return static_cast<uint32_t>(id);
	}

	[[nodiscard]] inline constexpr bool IsValid(MaterialID id)
	{
		return id != MaterialID::Invalid;
	}

	struct MaterialDesc
	{
		Math::float4 baseColor = Math::float4(1.0f, 1.0f, 1.0f, 1.0f);
		TextureID baseColorTexture = TextureID::Invalid;
		Math::float3 emissiveColor = Math::float3(0.0f, 0.0f, 0.0f);
		float metallicFactor = 0.0f;
		float roughnessFactor = 0.5f;
		float normalScale = 1.0f;
		float occlusionStrength = 1.0f;
		TextureID metallicRoughnessTexture = TextureID::Invalid;
		TextureID normalTexture = TextureID::Invalid;
		TextureID occlusionTexture = TextureID::Invalid;
		TextureID emissiveTexture = TextureID::Invalid;
	};
}
