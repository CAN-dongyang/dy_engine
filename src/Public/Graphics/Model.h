#pragma once

#include <string>

#include "Graphics/Entity.h"

namespace dy::Graphics
{
	class Scene;

	struct ModelLoadOptions
	{
		bool flipV = false;
	};

	struct ModelSceneDesc
	{
		std::string path;
		Math::float3 position = Math::float3(0.0f, 0.0f, 0.0f);
		Math::float4x4 transform = Math::float4x4::Identity();
		float normalizedSize = 1.6f;
		bool normalize = true;
		bool yUpToZUp = true;
		RenderFlags renderFlags = {};
		ModelLoadOptions loadOptions = {};
	};

	[[nodiscard]] bool AddModelToScene(Scene& scene, const ModelSceneDesc& desc);
	[[nodiscard]] bool AddModelToScene(
		Scene& scene,
		const std::string& path,
		const Math::float3& position = Math::float3(0.0f, 0.0f, 0.0f));
}
