#pragma once

#include <cstdint>
#include <vector>

#include "Math/Math.h"

namespace dy::Graphics
{
	enum class MeshID : uint32_t
	{
		Invalid = 0xFFFFFFFFu
	};

	[[nodiscard]] inline constexpr uint32_t ToIndex(MeshID id)
	{
		return static_cast<uint32_t>(id);
	}

	[[nodiscard]] inline constexpr bool IsValid(MeshID id)
	{
		return id != MeshID::Invalid;
	}

	struct Vertex
	{
		Math::float3 position;
		Math::float3 normal;
		Math::float2 uv;
		Math::float4 color = Math::float4(1.0f, 1.0f, 1.0f, 1.0f);
		Math::float4 tangent = Math::float4(1.0f, 0.0f, 0.0f, 1.0f);
	};

	struct MeshData
	{
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;
	};

	[[nodiscard]] MeshData CreateCubeMesh(float size = 1.0f);
}
