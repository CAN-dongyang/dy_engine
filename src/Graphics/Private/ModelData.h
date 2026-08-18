#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "Graphics/Material.h"
#include "Graphics/Mesh.h"
#include "Graphics/Private/MaterialTexture.h"

namespace dy::Graphics
{
	struct ModelMaterialInfo
	{
		MaterialDesc material;
		std::string name;
		std::array<std::string, kMaterialTextureCount> texturePaths = {};
		std::array<bool, kMaterialTextureCount> hasTexture = {};
	};

	struct ModelMesh
	{
		MeshData mesh;
		uint32_t materialIndex = 0;
		std::string name;
	};

	struct ModelData
	{
		std::vector<ModelMesh> meshes;
		std::vector<ModelMaterialInfo> materials;
	};
}
