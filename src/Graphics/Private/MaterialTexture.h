#pragma once

#include <cstdint>

namespace dy::Graphics
{
	enum class MaterialTextureKind : uint32_t
	{
		BaseColor,
		MetallicRoughness,
		Normal,
		Occlusion,
		Emissive,
		Count
	};

	inline constexpr uint32_t kMaterialTextureCount = static_cast<uint32_t>(MaterialTextureKind::Count);

	[[nodiscard]] inline constexpr uint32_t ToIndex(MaterialTextureKind kind)
	{
		return static_cast<uint32_t>(kind);
	}
}
