#pragma once

#include <cstdint>

#include "Math/Math.h"

namespace dy::Graphics
{
	enum class EntityID : uint32_t
	{
		Invalid = 0xFFFFFFFFu
	};

	[[nodiscard]] inline constexpr uint32_t ToIndex(EntityID id)
	{
		return static_cast<uint32_t>(id);
	}

	[[nodiscard]] inline constexpr bool IsValid(EntityID id)
	{
		return id != EntityID::Invalid;
	}

	struct alignas(16) Transform
	{
		Math::float4x4 worldMatrix = Math::float4x4::Identity();
	};

	struct RenderFlags
	{
		bool castShadow = true;
		bool receiveShadow = true;
	};
}
