#include "Graphics/Mesh.h"

#include <algorithm>

namespace dy::Graphics
{
	MeshData CreateCubeMesh(float size)
	{
		const float halfSize = std::max(size, 0.0f) * 0.5f;
		MeshData mesh;
		mesh.vertices.reserve(24u);
		mesh.indices.reserve(36u);

		auto addFace = [&](const Math::float3& a, const Math::float3& b, const Math::float3& c,
			const Math::float3& d, const Math::float3& normal, const Math::float4& tangent)
		{
			const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
			const Math::float4 white(1.0f, 1.0f, 1.0f, 1.0f);
			mesh.vertices.push_back(Vertex{ a, normal, Math::float2(0.0f, 0.0f), white, tangent });
			mesh.vertices.push_back(Vertex{ b, normal, Math::float2(1.0f, 0.0f), white, tangent });
			mesh.vertices.push_back(Vertex{ c, normal, Math::float2(1.0f, 1.0f), white, tangent });
			mesh.vertices.push_back(Vertex{ d, normal, Math::float2(0.0f, 1.0f), white, tangent });
			mesh.indices.insert(mesh.indices.end(), { base, base + 1u, base + 2u, base, base + 2u, base + 3u });
		};

		const float h = halfSize;
		addFace({ -h, -h, h }, { h, -h, h }, { h, h, h }, { -h, h, h }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f });
		addFace({ h, -h, -h }, { -h, -h, -h }, { -h, h, -h }, { h, h, -h }, { 0.0f, 0.0f, -1.0f }, { -1.0f, 0.0f, 0.0f, 1.0f });
		addFace({ h, -h, h }, { h, -h, -h }, { h, h, -h }, { h, h, h }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f, 1.0f });
		addFace({ -h, -h, -h }, { -h, -h, h }, { -h, h, h }, { -h, h, -h }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f });
		addFace({ -h, h, h }, { h, h, h }, { h, h, -h }, { -h, h, -h }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f });
		addFace({ -h, -h, -h }, { h, -h, -h }, { h, -h, h }, { -h, -h, h }, { 0.0f, -1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f });
		return mesh;
	}
}
