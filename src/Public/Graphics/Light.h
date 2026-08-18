#pragma once

#include <cstdint>

#include "Math/Math.h"

namespace dy::Graphics
{
	enum class DirectionalLightID : uint32_t
	{
		Invalid = 0xFFFFFFFFu
	};

	enum class PointLightID : uint32_t
	{
		Invalid = 0xFFFFFFFFu
	};

	[[nodiscard]] inline constexpr uint32_t ToIndex(DirectionalLightID id)
	{
		return static_cast<uint32_t>(id);
	}

	[[nodiscard]] inline constexpr bool IsValid(DirectionalLightID id)
	{
		return id != DirectionalLightID::Invalid;
	}

	[[nodiscard]] inline constexpr uint32_t ToIndex(PointLightID id)
	{
		return static_cast<uint32_t>(id);
	}

	[[nodiscard]] inline constexpr bool IsValid(PointLightID id)
	{
		return id != PointLightID::Invalid;
	}

	struct DirectionalLight
	{
		Math::float3 direction = Math::float3(0.35f, 0.65f, 0.68f);
		Math::float3 color = Math::float3(1.0f, 0.94f, 0.82f);
		float intensity = 4.0f;
		bool castShadow = true;
		float shadowStrength = 0.45f;
	};

	struct PointLight
	{
		Math::float3 position = Math::float3(0.0f, 0.0f, 2.0f);
		float range = 6.0f;
		Math::float3 color = Math::float3(1.0f, 0.94f, 0.82f);
		float intensity = 6.0f;
	};
}
