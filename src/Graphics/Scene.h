#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "Core/Types.h"
#include "Graphics/Mesh.h"

namespace dy::Graphics
{
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

	class Scene
	{
	public:
		[[nodiscard]] TextureID CreateTexture(const TextureAsset& texture)
		{
			m_textures.push_back(texture);
			return static_cast<TextureID>(m_textures.size() - 1u);
		}
		[[nodiscard]] TextureID CreateTexture(std::string sourcePath)
		{
			return CreateTexture(TextureAsset{ std::move(sourcePath) });
		}
		[[nodiscard]] MaterialID CreateMaterial(const MaterialDesc& material)
		{
			m_materials.push_back(material);
			return static_cast<MaterialID>(m_materials.size() - 1u);
		}
		[[nodiscard]] MeshID CreateMesh(const MeshData& mesh)
		{
			m_meshes.push_back(mesh);
			return static_cast<MeshID>(m_meshes.size() - 1u);
		}
		[[nodiscard]] EntityID CreateEntity(
			MeshID mesh,
			MaterialID material,
			const Math::float4x4& worldMatrix = Math::float4x4::Identity(),
			const RenderFlags& renderFlags = {})
		{
			const EntityID entity = static_cast<EntityID>(m_entityMeshes.size());
			m_entityMeshes.push_back(mesh);
			m_entityMaterials.push_back(material);
			m_entityTransforms.push_back(Transform{ worldMatrix });
			m_entityRenderFlags.push_back(renderFlags);
			return entity;
		}
		[[nodiscard]] DirectionalLightID CreateDirectionalLight(const DirectionalLight& light)
		{
			m_directionalLights.push_back(light);
			return static_cast<DirectionalLightID>(m_directionalLights.size() - 1u);
		}
		[[nodiscard]] PointLightID CreatePointLight(const PointLight& light)
		{
			m_pointLights.push_back(light);
			return static_cast<PointLightID>(m_pointLights.size() - 1u);
		}

		[[nodiscard]] uint32_t GetTextureCount() const { return static_cast<uint32_t>(m_textures.size()); }
		[[nodiscard]] uint32_t GetMaterialCount() const { return static_cast<uint32_t>(m_materials.size()); }
		[[nodiscard]] uint32_t GetMeshCount() const { return static_cast<uint32_t>(m_meshes.size()); }
		[[nodiscard]] uint32_t GetEntityCount() const { return static_cast<uint32_t>(m_entityMeshes.size()); }
		[[nodiscard]] uint32_t GetDirectionalLightCount() const { return static_cast<uint32_t>(m_directionalLights.size()); }
		[[nodiscard]] uint32_t GetPointLightCount() const { return static_cast<uint32_t>(m_pointLights.size()); }

		[[nodiscard]] const TextureAsset& GetTexture(TextureID textureId) const
		{
			return m_textures[ToIndex(textureId)];
		}
		[[nodiscard]] const MaterialDesc& GetMaterial(MaterialID materialId) const
		{
			return m_materials[ToIndex(materialId)];
		}
		void SetMaterial(MaterialID materialId, const MaterialDesc& material)
		{
			m_materials[ToIndex(materialId)] = material;
		}
		[[nodiscard]] const MeshData& GetMesh(MeshID meshId) const
		{
			return m_meshes[ToIndex(meshId)];
		}
		[[nodiscard]] MeshID GetEntityMesh(EntityID entityId) const
		{
			return m_entityMeshes[ToIndex(entityId)];
		}
		[[nodiscard]] MaterialID GetEntityMaterial(EntityID entityId) const
		{
			return m_entityMaterials[ToIndex(entityId)];
		}
		[[nodiscard]] const Transform& GetTransform(EntityID entityId) const
		{
			return m_entityTransforms[ToIndex(entityId)];
		}
		[[nodiscard]] Transform& GetTransform(EntityID entityId)
		{
			return m_entityTransforms[ToIndex(entityId)];
		}
		[[nodiscard]] const RenderFlags& GetRenderFlags(EntityID entityId) const
		{
			return m_entityRenderFlags[ToIndex(entityId)];
		}
		void SetRenderFlags(EntityID entityId, const RenderFlags& renderFlags)
		{
			m_entityRenderFlags[ToIndex(entityId)] = renderFlags;
		}
		[[nodiscard]] const DirectionalLight& GetDirectionalLight(DirectionalLightID lightId) const
		{
			return m_directionalLights[ToIndex(lightId)];
		}
		void SetDirectionalLight(DirectionalLightID lightId, const DirectionalLight& light)
		{
			m_directionalLights[ToIndex(lightId)] = light;
		}
		[[nodiscard]] const PointLight& GetPointLight(PointLightID lightId) const
		{
			return m_pointLights[ToIndex(lightId)];
		}
		void SetPointLight(PointLightID lightId, const PointLight& light)
		{
			m_pointLights[ToIndex(lightId)] = light;
		}

	private:
		std::vector<TextureAsset> m_textures;
		std::vector<MaterialDesc> m_materials;
		std::vector<MeshData> m_meshes;
		std::vector<MeshID> m_entityMeshes;
		std::vector<MaterialID> m_entityMaterials;
		std::vector<Transform> m_entityTransforms;
		std::vector<RenderFlags> m_entityRenderFlags;
		std::vector<DirectionalLight> m_directionalLights;
		std::vector<PointLight> m_pointLights;
	};
}
