#include "Graphics/RenderPath.h"

#include <array>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include "Graphics/Private/RendererShaderLayout.h"
#include "Graphics/Scene.h"
#include "RHI/Buffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/Pipeline.h"
#include "RHI/ResourceSet.h"
#include "RHI/Texture.h"

using namespace dy;
using namespace dy::Graphics;

namespace Layout = dy::Graphics::Private::RendererShaderLayout;

namespace
{
	using RendererVertex = Layout::RendererVertex;

	struct MeshRange
	{
		uint32_t firstVertex = 0;
		uint32_t firstIndex = 0;
		uint32_t indexCount = 0;
	};

	struct InstanceTransform
	{
		Math::float4x4 modelMatrix = Math::float4x4::Identity();
	};

	struct PendingDrawBatch
	{
		uint32_t meshIndex = 0;
		uint32_t materialIndex = 0;
		uint32_t textureFlags = 0;
		std::vector<InstanceTransform> instances;
	};

	struct DrawBatch
	{
		uint32_t meshIndex = 0;
		uint32_t materialIndex = 0;
		uint32_t textureFlags = 0;
		uint32_t firstInstance = 0;
		uint32_t instanceCount = 0;
	};

	[[nodiscard]] std::vector<RendererVertex> BuildRendererVertices(const MeshData& mesh)
	{
		std::vector<RendererVertex> vertices;
		vertices.reserve(mesh.vertices.size());
		for(const Vertex& vertex : mesh.vertices)
		{
			RendererVertex out = {};
			out.px = vertex.position.x;
			out.py = vertex.position.y;
			out.pz = vertex.position.z;
			out.nx = vertex.normal.x;
			out.ny = vertex.normal.y;
			out.nz = vertex.normal.z;
			out.u = vertex.uv.x;
			out.v = vertex.uv.y;
			out.tx = vertex.tangent.x;
			out.ty = vertex.tangent.y;
			out.tz = vertex.tangent.z;
			out.tw = vertex.tangent.w;
			vertices.push_back(out);
		}
		return vertices;
	}

	[[nodiscard]] std::vector<uint32_t> BuildRendererIndices(const MeshData& mesh)
	{
		if(!mesh.indices.empty()) return mesh.indices;

		std::vector<uint32_t> indices;
		indices.reserve(mesh.vertices.size());
		for(uint32_t index = 0; index < static_cast<uint32_t>(mesh.vertices.size()); ++index)
		{
			indices.push_back(index);
		}
		return indices;
	}

	void DestroyBuffer(RHI::IDevice* device, RHI::BufferHandle& buffer, uint32_t& sizeBytes, bool& ready)
	{
		if(device != nullptr && buffer != nullptr) device->DestroyBuffer(buffer);
		buffer = nullptr;
		sizeBytes = 0;
		ready = false;
	}

	[[nodiscard]] bool EnsureBuffer(
		RHI::IDevice* device,
		RHI::BufferHandle& buffer,
		uint32_t& currentSizeBytes,
		bool& ready,
		uint32_t sizeBytes,
		uint32_t stride,
		RHI::BufferUsage usage)
	{
		if(device == nullptr) return false;
		if(sizeBytes == 0)
		{
			DestroyBuffer(device, buffer, currentSizeBytes, ready);
			return true;
		}
		if(buffer != nullptr && currentSizeBytes == sizeBytes) return true;

		DestroyBuffer(device, buffer, currentSizeBytes, ready);
		buffer = device->CreateBuffer(RHI::BufferDesc{
			sizeBytes,
			stride,
			usage,
			RHI::ResourceState::CopyDestination
		});
		if(buffer == nullptr) return false;
		currentSizeBytes = sizeBytes;
		return true;
	}

	[[nodiscard]] bool RecordBufferUpload(
		RHI::IDevice* device,
		RHI::ICommandList& commandList,
		RHI::BufferHandle buffer,
		const void* data,
		uint32_t sizeBytes,
		bool ready,
		RHI::ResourceState useState)
	{
		if(device == nullptr || buffer == nullptr || data == nullptr || sizeBytes == 0) return false;
		if(ready)
		{
			const RHI::ResourceBarrierDesc beforeCopy = {
				buffer,
				nullptr,
				useState,
				RHI::ResourceState::CopyDestination,
				{}
			};
			commandList.ResourceBarrier(&beforeCopy, 1);
		}
		if(!device->UpdateBuffer(commandList, buffer, 0, data, sizeBytes))
		{
			if(ready)
			{
				const RHI::ResourceBarrierDesc restore = {
					buffer,
					nullptr,
					RHI::ResourceState::CopyDestination,
					useState,
					{}
				};
				commandList.ResourceBarrier(&restore, 1);
			}
			return false;
		}

		const RHI::ResourceBarrierDesc afterCopy = {
			buffer,
			nullptr,
			RHI::ResourceState::CopyDestination,
			useState,
			{}
		};
		commandList.ResourceBarrier(&afterCopy, 1);
		return true;
	}

	[[nodiscard]] bool ResourceSetMatches(
		const RHI::ResourceSetHandle resourceSet,
		RHI::PipelineHandle pipeline,
		const RHI::ResourceBinding* bindings,
		uint32_t bindingCount)
	{
		if(resourceSet == nullptr || resourceSet->GetPipeline() != pipeline ||
			resourceSet->GetBindingCount() != bindingCount) return false;

		const RHI::ResourceBinding* current = resourceSet->GetBindings();
		for(uint32_t index = 0; index < bindingCount; ++index)
		{
			const RHI::ResourceBinding& left = current[index];
			const RHI::ResourceBinding& right = bindings[index];
			if(left.binding != right.binding ||
				left.arrayElement != right.arrayElement || left.buffer != right.buffer ||
				left.texture != right.texture || left.offset != right.offset || left.size != right.size ||
				left.subresources.firstMipLevel != right.subresources.firstMipLevel ||
				left.subresources.mipLevelCount != right.subresources.mipLevelCount ||
				left.subresources.firstArrayLayer != right.subresources.firstArrayLayer ||
				left.subresources.arrayLayerCount != right.subresources.arrayLayerCount)
			{
				return false;
			}
		}
		return true;
	}

	[[nodiscard]] bool ReplaceResourceSet(
		RHI::IDevice* device,
		RHI::ResourceSetHandle& resourceSet,
		RHI::PipelineHandle pipeline,
		const RHI::ResourceBinding* bindings,
		uint32_t bindingCount)
	{
		if(ResourceSetMatches(resourceSet, pipeline, bindings, bindingCount)) return true;

		RHI::ResourceSetHandle replacement = device->CreateResourceSet(RHI::ResourceSetDesc{
			pipeline,
			bindings,
			bindingCount
		});
		if(replacement == nullptr) return false;
		if(resourceSet != nullptr) device->DestroyResourceSet(resourceSet);
		resourceSet = replacement;
		return true;
	}

	void DestroyResourceSets(RHI::IDevice* device, std::vector<RHI::ResourceSetHandle>& resourceSets)
	{
		if(device != nullptr)
		{
			for(RHI::ResourceSetHandle resourceSet : resourceSets)
			{
				if(resourceSet != nullptr) device->DestroyResourceSet(resourceSet);
			}
		}
		resourceSets.clear();
	}

	[[nodiscard]] bool EnsureMaterialResourceSets(
		RHI::IDevice* device,
		const RenderPathContext& context,
		RHI::BufferHandle instanceBuffer,
		std::vector<RHI::ResourceSetHandle>& resourceSets)
	{
		if(device == nullptr || context.pipeline == nullptr || context.materialStates == nullptr ||
			context.lightingBuffer == nullptr || context.shadowMatrixBuffer == nullptr ||
			context.shadowDepth == nullptr || instanceBuffer == nullptr) return false;

		const std::vector<SceneMaterialState>& materials = *context.materialStates;
		while(resourceSets.size() > materials.size())
		{
			RHI::ResourceSetHandle resourceSet = resourceSets.back();
			if(resourceSet != nullptr) device->DestroyResourceSet(resourceSet);
			resourceSets.pop_back();
		}
		resourceSets.resize(materials.size(), nullptr);

		for(uint32_t materialIndex = 0; materialIndex < static_cast<uint32_t>(materials.size()); ++materialIndex)
		{
			const SceneMaterialState& material = materials[materialIndex];
			const std::array<RHI::ResourceBinding, 9> bindings = {{
				{ RENDERER_BINDING_BASE_COLOR_TEXTURE, 0, nullptr, material.textures[kMaterialBaseColorTextureSlot], 0, 0, {} },
				{ RENDERER_BINDING_LIGHTING_CONSTANTS, 0, context.lightingBuffer, nullptr, 0, static_cast<uint32_t>(sizeof(Layout::RendererLightingConstants)), {} },
				{ RENDERER_BINDING_SHADOW_TEXTURE, 0, nullptr, context.shadowDepth, 0, 0, {} },
				{ RENDERER_BINDING_SHADOW_MATRIX, 0, context.shadowMatrixBuffer, nullptr, 0, static_cast<uint32_t>(sizeof(Layout::RendererShadowConstants)), {} },
				{ RENDERER_BINDING_TRANSFORM_STORAGE, 0, instanceBuffer, nullptr, 0, instanceBuffer->GetDesc().size, {} },
				{ RENDERER_BINDING_METALLIC_ROUGHNESS_TEXTURE, 0, nullptr, material.textures[kMaterialMetallicRoughnessTextureSlot], 0, 0, {} },
				{ RENDERER_BINDING_NORMAL_TEXTURE, 0, nullptr, material.textures[kMaterialNormalTextureSlot], 0, 0, {} },
				{ RENDERER_BINDING_OCCLUSION_TEXTURE, 0, nullptr, material.textures[kMaterialOcclusionTextureSlot], 0, 0, {} },
				{ RENDERER_BINDING_EMISSIVE_TEXTURE, 0, nullptr, material.textures[kMaterialEmissiveTextureSlot], 0, 0, {} }
			}};
			if(!ReplaceResourceSet(
					device,
					resourceSets[materialIndex],
					context.pipeline,
					bindings.data(),
					static_cast<uint32_t>(bindings.size()))) return false;
		}
		return true;
	}

	[[nodiscard]] bool EnsureShadowResourceSet(
		RHI::IDevice* device,
		const RenderPathContext& context,
		RHI::BufferHandle instanceBuffer,
		RHI::ResourceSetHandle& resourceSet)
	{
		if(device == nullptr || context.shadowPipeline == nullptr ||
			context.shadowMatrixBuffer == nullptr || instanceBuffer == nullptr) return false;

		const std::array<RHI::ResourceBinding, 2> bindings = {{
			{ RENDERER_BINDING_SHADOW_MATRIX, 0, context.shadowMatrixBuffer, nullptr, 0, static_cast<uint32_t>(sizeof(Layout::RendererShadowConstants)), {} },
			{ RENDERER_BINDING_TRANSFORM_STORAGE, 0, instanceBuffer, nullptr, 0, instanceBuffer->GetDesc().size, {} }
		}};
		return ReplaceResourceSet(
			device,
			resourceSet,
			context.shadowPipeline,
			bindings.data(),
			static_cast<uint32_t>(bindings.size()));
	}

	[[nodiscard]] uint32_t ComputeTextureFlags(const SceneMaterialState& material, const RenderFlags& renderFlags)
	{
		uint32_t flags = material.textureFlags;
		if(renderFlags.receiveShadow) flags |= RENDERER_TEXTURE_FLAG_RECEIVE_SHADOW;
		return flags;
	}

	[[nodiscard]] Layout::DrawConstants MakeDrawConstants(
		const RendererDesc& config,
		const MaterialDesc& material,
		const Transform& transform,
		uint32_t textureFlags,
		uint32_t instanceBase)
	{
		Layout::DrawConstants constants = {};
		constants.viewProjectionMatrix = config.viewProjectionMatrix;
		constants.modelMatrix = transform.worldMatrix;
		constants.textureFlags = textureFlags;
		constants.instanceBase = instanceBase;
		constants.emissiveColor = Math::float4(
			material.emissiveColor.x,
			material.emissiveColor.y,
			material.emissiveColor.z,
			0.0f);
		constants.baseColor = material.baseColor;
		constants.materialParams = Math::float4(
			material.metallicFactor,
			material.roughnessFactor,
			material.normalScale,
			material.occlusionStrength);
		return constants;
	}

	[[nodiscard]] bool ShouldRecordShadow(const RenderPathContext& context)
	{
		return context.shadowPipeline != nullptr && context.shadowDepth != nullptr &&
			context.shadowMatrixBuffer != nullptr && context.shadowMapResolution != 0;
	}

	[[nodiscard]] bool BeginShadowRendering(
		RHI::ICommandList& commandList,
		const RenderPathContext& context,
		RHI::ResourceSetHandle resourceSet)
	{
		if(context.shadowPipeline == nullptr || context.shadowDepth == nullptr || resourceSet == nullptr) return false;
		if(context.shadowDepthState != RHI::ResourceState::DepthWrite)
		{
			const RHI::ResourceBarrierDesc barrier = {
				nullptr,
				context.shadowDepth,
				context.shadowDepthState,
				RHI::ResourceState::DepthWrite,
				{}
			};
			commandList.ResourceBarrier(&barrier, 1);
		}

		RHI::DepthStencilAttachment depth = {};
		depth.texture = context.shadowDepth;
		depth.state = RHI::ResourceState::DepthWrite;
		depth.depthLoadOp = RHI::LoadOp::Clear;
		depth.depthStoreOp = RHI::StoreOp::Store;
		depth.clearDepth = 1.0f;
		depth.stencilLoadOp = RHI::LoadOp::Discard;
		depth.stencilStoreOp = RHI::StoreOp::Discard;
		commandList.BeginRendering(RHI::RenderingDesc{ nullptr, 0, &depth });
		commandList.BindGraphicsPipeline(context.shadowPipeline);
		commandList.BindResourceSet(resourceSet);
		commandList.SetViewport(RHI::Viewport{
			0.0f,
			0.0f,
			static_cast<float>(context.shadowMapResolution),
			static_cast<float>(context.shadowMapResolution),
			0.0f,
			1.0f
		});
		commandList.SetScissor(RHI::Rect{ 0, 0, context.shadowMapResolution, context.shadowMapResolution });
		return true;
	}

	void EndShadowRendering(RHI::ICommandList& commandList, RHI::TextureHandle shadowDepth)
	{
		commandList.EndRendering();
		const RHI::ResourceBarrierDesc barrier = {
			nullptr,
			shadowDepth,
			RHI::ResourceState::DepthWrite,
			RHI::ResourceState::ShaderResource,
			{}
		};
		commandList.ResourceBarrier(&barrier, 1);
	}

	[[nodiscard]] bool BeginMainRendering(
		RHI::ICommandList& commandList,
		RHI::TextureHandle backBuffer,
		const RenderPathContext& context,
		bool shadowRecorded)
	{
		if(backBuffer == nullptr || context.pipeline == nullptr || context.config == nullptr) return false;

		std::array<RHI::ResourceBarrierDesc, 3> barriers = {};
		uint32_t barrierCount = 0;
		barriers[barrierCount++] = {
			nullptr,
			backBuffer,
			RHI::ResourceState::Present,
			RHI::ResourceState::RenderTarget,
			{}
		};
		if(context.depthStencil != nullptr && context.depthStencilState != RHI::ResourceState::DepthWrite)
		{
			barriers[barrierCount++] = {
				nullptr,
				context.depthStencil,
				context.depthStencilState,
				RHI::ResourceState::DepthWrite,
				{}
			};
		}
		if(context.shadowDepth != nullptr && !shadowRecorded &&
			context.shadowDepthState != RHI::ResourceState::ShaderResource)
		{
			barriers[barrierCount++] = {
				nullptr,
				context.shadowDepth,
				context.shadowDepthState,
				RHI::ResourceState::ShaderResource,
				{}
			};
		}
		commandList.ResourceBarrier(barriers.data(), barrierCount);

		RHI::ColorAttachment color = {};
		color.texture = backBuffer;
		color.loadOp = RHI::LoadOp::Clear;
		color.storeOp = RHI::StoreOp::Store;
		color.clearColor[0] = context.config->clearColor.x;
		color.clearColor[1] = context.config->clearColor.y;
		color.clearColor[2] = context.config->clearColor.z;
		color.clearColor[3] = context.config->clearColor.w;

		RHI::DepthStencilAttachment depth = {};
		depth.texture = context.depthStencil;
		depth.state = RHI::ResourceState::DepthWrite;
		depth.depthLoadOp = RHI::LoadOp::Clear;
		depth.depthStoreOp = RHI::StoreOp::Discard;
		depth.clearDepth = 1.0f;
		depth.stencilLoadOp = RHI::LoadOp::Discard;
		depth.stencilStoreOp = RHI::StoreOp::Discard;

		const RHI::RenderingDesc rendering = {
			&color,
			1,
			context.depthStencil != nullptr ? &depth : nullptr
		};
		commandList.BeginRendering(rendering);
		commandList.BindGraphicsPipeline(context.pipeline);
		commandList.SetViewport(RHI::Viewport{
			0.0f,
			0.0f,
			static_cast<float>(backBuffer->GetDesc().width),
			static_cast<float>(backBuffer->GetDesc().height),
			0.0f,
			1.0f
		});
		commandList.SetScissor(RHI::Rect{ 0, 0, backBuffer->GetDesc().width, backBuffer->GetDesc().height });
		return true;
	}

	void EndMainRendering(RHI::ICommandList& commandList, RHI::TextureHandle backBuffer)
	{
		commandList.EndRendering();
		const RHI::ResourceBarrierDesc barrier = {
			nullptr,
			backBuffer,
			RHI::ResourceState::RenderTarget,
			RHI::ResourceState::Present,
			{}
		};
		commandList.ResourceBarrier(&barrier, 1);
	}

	void AddPendingBatchInstance(
		std::vector<PendingDrawBatch>& batches,
		uint32_t meshIndex,
		uint32_t materialIndex,
		uint32_t textureFlags,
		const Transform& transform)
	{
		for(PendingDrawBatch& batch : batches)
		{
			if(batch.meshIndex == meshIndex && batch.materialIndex == materialIndex &&
				batch.textureFlags == textureFlags)
			{
				batch.instances.push_back(InstanceTransform{ transform.worldMatrix });
				return;
			}
		}

		PendingDrawBatch batch = {};
		batch.meshIndex = meshIndex;
		batch.materialIndex = materialIndex;
		batch.textureFlags = textureFlags;
		batch.instances.push_back(InstanceTransform{ transform.worldMatrix });
		batches.push_back(std::move(batch));
	}

	void BuildMainDrawBatches(
		const Scene& scene,
		const RenderPathContext& context,
		const std::vector<MeshRange>& meshRanges,
		std::vector<DrawBatch>& drawBatches,
		std::vector<InstanceTransform>& instances)
	{
		drawBatches.clear();
		instances.clear();
		if(context.materialStates == nullptr) return;

		const std::vector<SceneMaterialState>& materials = *context.materialStates;
		std::vector<PendingDrawBatch> pending;
		for(uint32_t entityIndex = 0; entityIndex < scene.GetEntityCount(); ++entityIndex)
		{
			const EntityID entity = static_cast<EntityID>(entityIndex);
			const MeshID meshId = scene.GetEntityMesh(entity);
			const MaterialID materialId = scene.GetEntityMaterial(entity);
			if(!IsValid(meshId) || !IsValid(materialId)) continue;

			const uint32_t meshIndex = ToIndex(meshId);
			const uint32_t materialIndex = ToIndex(materialId);
			if(meshIndex >= meshRanges.size() || meshRanges[meshIndex].indexCount == 0 ||
				materialIndex >= materials.size()) continue;

			AddPendingBatchInstance(
				pending,
				meshIndex,
				materialIndex,
				ComputeTextureFlags(materials[materialIndex], scene.GetRenderFlags(entity)),
				scene.GetTransform(entity));
		}

		for(const PendingDrawBatch& source : pending)
		{
			if(source.instances.empty()) continue;
			DrawBatch batch = {};
			batch.meshIndex = source.meshIndex;
			batch.materialIndex = source.materialIndex;
			batch.textureFlags = source.textureFlags;
			batch.firstInstance = static_cast<uint32_t>(instances.size());
			batch.instanceCount = static_cast<uint32_t>(source.instances.size());
			drawBatches.push_back(batch);
			instances.insert(instances.end(), source.instances.begin(), source.instances.end());
		}
	}

	class PerDrawBindPath final : public IRenderPath
	{
	public:
		[[nodiscard]] bool PrepareResources(const Scene& scene, RHI::IDevice* device, const RenderPathContext&) override;
		[[nodiscard]] bool RecordMainPass(const Scene& scene, RHI::IDevice* device, const RenderPathContext& context) override;
		void Shutdown(RHI::IDevice* device) override;

	private:
		struct SceneMeshState
		{
			RHI::BufferHandle vertexBuffer = nullptr;
			RHI::BufferHandle indexBuffer = nullptr;
			uint32_t vertexBytes = 0;
			uint32_t indexBytes = 0;
			uint32_t indexCount = 0;
			bool vertexReady = false;
			bool indexReady = false;
		};

		void DestroyMeshState(RHI::IDevice* device, SceneMeshState& mesh);
		[[nodiscard]] bool RecordShadowDraws(
			RHI::ICommandList& commandList,
			const Scene& scene,
			const RenderPathContext& context);

		std::vector<SceneMeshState> m_meshes;
		RHI::BufferHandle m_instanceBuffer = nullptr;
		uint32_t m_instanceBytes = 0;
		bool m_instanceReady = false;
		std::vector<RHI::ResourceSetHandle> m_materialResourceSets;
		RHI::ResourceSetHandle m_shadowResourceSet = nullptr;
	};

	void PerDrawBindPath::DestroyMeshState(RHI::IDevice* device, SceneMeshState& mesh)
	{
		DestroyBuffer(device, mesh.vertexBuffer, mesh.vertexBytes, mesh.vertexReady);
		DestroyBuffer(device, mesh.indexBuffer, mesh.indexBytes, mesh.indexReady);
		mesh.indexCount = 0;
	}

	bool PerDrawBindPath::PrepareResources(const Scene& scene, RHI::IDevice* device, const RenderPathContext&)
	{
		if(device == nullptr) return false;

		while(m_meshes.size() > scene.GetMeshCount())
		{
			DestroyMeshState(device, m_meshes.back());
			m_meshes.pop_back();
		}
		m_meshes.resize(scene.GetMeshCount());

		RHI::ICommandList* commandList = device->AcquireCommandList();
		if(commandList == nullptr) return false;
		bool uploadFailed = false;
		std::vector<bool*> uploaded;
		for(uint32_t meshIndex = 0; meshIndex < scene.GetMeshCount(); ++meshIndex)
		{
			const MeshData& source = scene.GetMesh(static_cast<MeshID>(meshIndex));
			if(source.vertices.size() > std::numeric_limits<uint32_t>::max() / sizeof(RendererVertex) ||
				(!source.indices.empty() &&
					source.indices.size() > std::numeric_limits<uint32_t>::max() / sizeof(uint32_t)))
			{
				uploadFailed = true;
				break;
			}
			std::vector<RendererVertex> vertices = BuildRendererVertices(source);
			std::vector<uint32_t> indices = BuildRendererIndices(source);
			SceneMeshState& mesh = m_meshes[meshIndex];
			if(vertices.empty() || indices.empty())
			{
				DestroyMeshState(device, mesh);
				continue;
			}

			const uint32_t vertexBytes = static_cast<uint32_t>(vertices.size() * sizeof(RendererVertex));
			const uint32_t indexBytes = static_cast<uint32_t>(indices.size() * sizeof(uint32_t));
			if(!EnsureBuffer(
					device, mesh.vertexBuffer, mesh.vertexBytes, mesh.vertexReady,
					vertexBytes, static_cast<uint32_t>(sizeof(RendererVertex)), RHI::BufferUsage::Vertex) ||
				!EnsureBuffer(
					device, mesh.indexBuffer, mesh.indexBytes, mesh.indexReady,
					indexBytes, static_cast<uint32_t>(sizeof(uint32_t)), RHI::BufferUsage::Index))
			{
				uploadFailed = true;
				break;
			}
			if(!RecordBufferUpload(
					device, *commandList, mesh.vertexBuffer, vertices.data(), vertexBytes,
					mesh.vertexReady, RHI::ResourceState::VertexBuffer))
			{
				uploadFailed = true;
				break;
			}
			uploaded.push_back(&mesh.vertexReady);
			if(!RecordBufferUpload(
					device, *commandList, mesh.indexBuffer, indices.data(), indexBytes,
					mesh.indexReady, RHI::ResourceState::IndexBuffer))
			{
				uploadFailed = true;
				break;
			}
			uploaded.push_back(&mesh.indexReady);
			mesh.indexCount = static_cast<uint32_t>(indices.size());
		}

		const InstanceTransform identity = {};
		if(!uploadFailed)
		{
			if(!EnsureBuffer(
					device, m_instanceBuffer, m_instanceBytes, m_instanceReady,
					static_cast<uint32_t>(sizeof(identity)), static_cast<uint32_t>(sizeof(identity)),
					RHI::BufferUsage::Storage) ||
				!RecordBufferUpload(
					device, *commandList, m_instanceBuffer, &identity, static_cast<uint32_t>(sizeof(identity)),
					m_instanceReady, RHI::ResourceState::ShaderResource)) uploadFailed = true;
			else uploaded.push_back(&m_instanceReady);
		}

		commandList->Close();
		std::array<RHI::ICommandList*, 1> commands = { commandList };
		const bool submitted = device->Submit(commands.data(), 1);
		if(submitted)
		{
			for(bool* ready : uploaded) *ready = true;
		}
		return submitted && !uploadFailed;
	}

	bool PerDrawBindPath::RecordShadowDraws(
		RHI::ICommandList& commandList,
		const Scene& scene,
		const RenderPathContext& context)
	{
		if(context.config == nullptr || context.materialStates == nullptr ||
			!BeginShadowRendering(commandList, context, m_shadowResourceSet)) return false;

		const std::vector<SceneMaterialState>& materials = *context.materialStates;
		for(uint32_t entityIndex = 0; entityIndex < scene.GetEntityCount(); ++entityIndex)
		{
			const EntityID entity = static_cast<EntityID>(entityIndex);
			const RenderFlags& renderFlags = scene.GetRenderFlags(entity);
			if(!renderFlags.castShadow) continue;

			const MeshID meshId = scene.GetEntityMesh(entity);
			const MaterialID materialId = scene.GetEntityMaterial(entity);
			if(!IsValid(meshId) || !IsValid(materialId)) continue;
			const uint32_t meshIndex = ToIndex(meshId);
			const uint32_t materialIndex = ToIndex(materialId);
			if(meshIndex >= m_meshes.size() || materialIndex >= materials.size()) continue;

			const SceneMeshState& mesh = m_meshes[meshIndex];
			if(mesh.vertexBuffer == nullptr || mesh.indexBuffer == nullptr || mesh.indexCount == 0) continue;
			commandList.BindVertexBuffer(0, mesh.vertexBuffer, 0);
			commandList.BindIndexBuffer(mesh.indexBuffer, RHI::Format::R32_UINT, 0);
			const Layout::DrawConstants constants = MakeDrawConstants(
				*context.config,
				scene.GetMaterial(materialId),
				scene.GetTransform(entity),
				ComputeTextureFlags(materials[materialIndex], renderFlags),
				0);
			commandList.SetInlineConstants(0, static_cast<uint32_t>(sizeof(constants)), &constants);
			commandList.DrawIndexedInstanced(mesh.indexCount, 1, 0, 0, 0);
		}
		EndShadowRendering(commandList, context.shadowDepth);
		return true;
	}

	bool PerDrawBindPath::RecordMainPass(
		const Scene& scene,
		RHI::IDevice* device,
		const RenderPathContext& context)
	{
		if(device == nullptr || context.config == nullptr || context.materialStates == nullptr ||
			context.pipeline == nullptr || m_instanceBuffer == nullptr) return false;
		if(!EnsureMaterialResourceSets(device, context, m_instanceBuffer, m_materialResourceSets)) return false;

		const bool recordShadow = ShouldRecordShadow(context);
		if(recordShadow && !EnsureShadowResourceSet(
				device, context, m_instanceBuffer, m_shadowResourceSet)) return false;

		RHI::TextureHandle backBuffer = device->GetBackBuffer();
		if(backBuffer == nullptr) return false;
		RHI::ICommandList* commandList = device->AcquireCommandList();
		if(commandList == nullptr) return false;
		const bool shadowRecorded = !recordShadow || RecordShadowDraws(*commandList, scene, context);
		const bool mainBegan = shadowRecorded &&
			BeginMainRendering(*commandList, backBuffer, context, recordShadow);

		if(mainBegan)
		{
			const std::vector<SceneMaterialState>& materials = *context.materialStates;
			for(uint32_t entityIndex = 0; entityIndex < scene.GetEntityCount(); ++entityIndex)
			{
				const EntityID entity = static_cast<EntityID>(entityIndex);
				const MeshID meshId = scene.GetEntityMesh(entity);
				const MaterialID materialId = scene.GetEntityMaterial(entity);
				if(!IsValid(meshId) || !IsValid(materialId)) continue;

				const uint32_t meshIndex = ToIndex(meshId);
				const uint32_t materialIndex = ToIndex(materialId);
				if(meshIndex >= m_meshes.size() || materialIndex >= materials.size() ||
					materialIndex >= m_materialResourceSets.size()) continue;
				const SceneMeshState& mesh = m_meshes[meshIndex];
				if(mesh.vertexBuffer == nullptr || mesh.indexBuffer == nullptr || mesh.indexCount == 0) continue;

				commandList->BindResourceSet(m_materialResourceSets[materialIndex]);
				commandList->BindVertexBuffer(0, mesh.vertexBuffer, 0);
				commandList->BindIndexBuffer(mesh.indexBuffer, RHI::Format::R32_UINT, 0);
				const Layout::DrawConstants constants = MakeDrawConstants(
					*context.config,
					scene.GetMaterial(materialId),
					scene.GetTransform(entity),
					ComputeTextureFlags(materials[materialIndex], scene.GetRenderFlags(entity)),
					0);
				commandList->SetInlineConstants(0, static_cast<uint32_t>(sizeof(constants)), &constants);
				commandList->DrawIndexedInstanced(mesh.indexCount, 1, 0, 0, 0);
			}

			EndMainRendering(*commandList, backBuffer);
		}
		commandList->Close();
		std::array<RHI::ICommandList*, 1> commands = { commandList };
		return device->Submit(commands.data(), 1) && mainBegan;
	}

	void PerDrawBindPath::Shutdown(RHI::IDevice* device)
	{
		if(device == nullptr) return;
		DestroyResourceSets(device, m_materialResourceSets);
		if(m_shadowResourceSet != nullptr) device->DestroyResourceSet(m_shadowResourceSet);
		m_shadowResourceSet = nullptr;
		for(SceneMeshState& mesh : m_meshes) DestroyMeshState(device, mesh);
		m_meshes.clear();
		DestroyBuffer(device, m_instanceBuffer, m_instanceBytes, m_instanceReady);
	}

	class BatchedBindPath : public IRenderPath
	{
	public:
		[[nodiscard]] bool PrepareResources(const Scene& scene, RHI::IDevice* device, const RenderPathContext&) override;
		[[nodiscard]] bool RecordMainPass(const Scene& scene, RHI::IDevice* device, const RenderPathContext& context) override;
		void Shutdown(RHI::IDevice* device) override;

	private:
		[[nodiscard]] bool RecordShadowDraws(
			RHI::ICommandList& commandList,
			const Scene& scene,
			const RenderPathContext& context);

		RHI::BufferHandle m_vertexBuffer = nullptr;
		RHI::BufferHandle m_indexBuffer = nullptr;
		RHI::BufferHandle m_instanceBuffer = nullptr;
		uint32_t m_vertexBytes = 0;
		uint32_t m_indexBytes = 0;
		uint32_t m_instanceBytes = 0;
		bool m_vertexReady = false;
		bool m_indexReady = false;
		bool m_instanceReady = false;
		std::vector<MeshRange> m_meshRanges;
		std::vector<DrawBatch> m_drawBatches;
		std::vector<InstanceTransform> m_instances;
		std::vector<RHI::ResourceSetHandle> m_materialResourceSets;
		RHI::ResourceSetHandle m_shadowResourceSet = nullptr;
	};

	bool BatchedBindPath::PrepareResources(const Scene& scene, RHI::IDevice* device, const RenderPathContext&)
	{
		if(device == nullptr) return false;

		std::vector<RendererVertex> vertices;
		std::vector<uint32_t> indices;
		m_meshRanges.assign(scene.GetMeshCount(), {});
		for(uint32_t meshIndex = 0; meshIndex < scene.GetMeshCount(); ++meshIndex)
		{
			const MeshData& source = scene.GetMesh(static_cast<MeshID>(meshIndex));
			if(source.vertices.size() > std::numeric_limits<uint32_t>::max() / sizeof(RendererVertex) ||
				(!source.indices.empty() &&
					source.indices.size() > std::numeric_limits<uint32_t>::max() / sizeof(uint32_t))) return false;
			std::vector<RendererVertex> meshVertices = BuildRendererVertices(source);
			std::vector<uint32_t> meshIndices = BuildRendererIndices(source);
			if(meshVertices.empty() || meshIndices.empty()) continue;
			if(meshVertices.size() > std::numeric_limits<uint32_t>::max() / sizeof(RendererVertex) ||
				meshIndices.size() > std::numeric_limits<uint32_t>::max() / sizeof(uint32_t) ||
				vertices.size() > std::numeric_limits<uint32_t>::max() / sizeof(RendererVertex) - meshVertices.size() ||
				indices.size() > std::numeric_limits<uint32_t>::max() / sizeof(uint32_t) - meshIndices.size()) return false;

			MeshRange& range = m_meshRanges[meshIndex];
			range.firstVertex = static_cast<uint32_t>(vertices.size());
			range.firstIndex = static_cast<uint32_t>(indices.size());
			range.indexCount = static_cast<uint32_t>(meshIndices.size());
			vertices.insert(vertices.end(), meshVertices.begin(), meshVertices.end());
			indices.insert(indices.end(), meshIndices.begin(), meshIndices.end());
		}

		const uint32_t vertexBytes = static_cast<uint32_t>(vertices.size() * sizeof(RendererVertex));
		const uint32_t indexBytes = static_cast<uint32_t>(indices.size() * sizeof(uint32_t));
		if(!EnsureBuffer(
				device, m_vertexBuffer, m_vertexBytes, m_vertexReady,
				vertexBytes, static_cast<uint32_t>(sizeof(RendererVertex)), RHI::BufferUsage::Vertex) ||
			!EnsureBuffer(
				device, m_indexBuffer, m_indexBytes, m_indexReady,
				indexBytes, static_cast<uint32_t>(sizeof(uint32_t)), RHI::BufferUsage::Index)) return false;
		if(vertexBytes == 0 || indexBytes == 0) return true;

		RHI::ICommandList* commandList = device->AcquireCommandList();
		if(commandList == nullptr) return false;
		const bool vertexUploaded = RecordBufferUpload(
			device, *commandList, m_vertexBuffer, vertices.data(), vertexBytes,
			m_vertexReady, RHI::ResourceState::VertexBuffer);
		const bool indexUploaded = vertexUploaded && RecordBufferUpload(
			device, *commandList, m_indexBuffer, indices.data(), indexBytes,
			m_indexReady, RHI::ResourceState::IndexBuffer);

		commandList->Close();
		std::array<RHI::ICommandList*, 1> commands = { commandList };
		const bool submitted = device->Submit(commands.data(), 1);
		if(submitted)
		{
			if(vertexUploaded) m_vertexReady = true;
			if(indexUploaded) m_indexReady = true;
		}
		return submitted && vertexUploaded && indexUploaded;
	}

	bool BatchedBindPath::RecordShadowDraws(
		RHI::ICommandList& commandList,
		const Scene& scene,
		const RenderPathContext& context)
	{
		if(context.config == nullptr || context.materialStates == nullptr ||
			!BeginShadowRendering(commandList, context, m_shadowResourceSet)) return false;
		commandList.BindVertexBuffer(0, m_vertexBuffer, 0);
		commandList.BindIndexBuffer(m_indexBuffer, RHI::Format::R32_UINT, 0);

		const std::vector<SceneMaterialState>& materials = *context.materialStates;
		for(uint32_t entityIndex = 0; entityIndex < scene.GetEntityCount(); ++entityIndex)
		{
			const EntityID entity = static_cast<EntityID>(entityIndex);
			const RenderFlags& renderFlags = scene.GetRenderFlags(entity);
			if(!renderFlags.castShadow) continue;

			const MeshID meshId = scene.GetEntityMesh(entity);
			const MaterialID materialId = scene.GetEntityMaterial(entity);
			if(!IsValid(meshId) || !IsValid(materialId)) continue;
			const uint32_t meshIndex = ToIndex(meshId);
			const uint32_t materialIndex = ToIndex(materialId);
			if(meshIndex >= m_meshRanges.size() || materialIndex >= materials.size()) continue;
			const MeshRange& range = m_meshRanges[meshIndex];
			if(range.indexCount == 0) continue;

			const Layout::DrawConstants constants = MakeDrawConstants(
				*context.config,
				scene.GetMaterial(materialId),
				scene.GetTransform(entity),
				ComputeTextureFlags(materials[materialIndex], renderFlags),
				0);
			commandList.SetInlineConstants(0, static_cast<uint32_t>(sizeof(constants)), &constants);
			commandList.DrawIndexedInstanced(
				range.indexCount,
				1,
				range.firstIndex,
				static_cast<int32_t>(range.firstVertex),
				0);
		}
		EndShadowRendering(commandList, context.shadowDepth);
		return true;
	}

	bool BatchedBindPath::RecordMainPass(
		const Scene& scene,
		RHI::IDevice* device,
		const RenderPathContext& context)
	{
		if(device == nullptr || context.config == nullptr || context.materialStates == nullptr ||
			context.pipeline == nullptr) return false;

		BuildMainDrawBatches(scene, context, m_meshRanges, m_drawBatches, m_instances);
		const bool hasDraws = !m_drawBatches.empty();
		uint32_t instanceBytes = 0;
		if(hasDraws)
		{
			if(m_vertexBuffer == nullptr || m_indexBuffer == nullptr) return false;
			if(m_instances.size() > std::numeric_limits<uint32_t>::max() / sizeof(InstanceTransform)) return false;
			instanceBytes = static_cast<uint32_t>(m_instances.size() * sizeof(InstanceTransform));
			if(!EnsureBuffer(
					device, m_instanceBuffer, m_instanceBytes, m_instanceReady,
					instanceBytes, static_cast<uint32_t>(sizeof(InstanceTransform)), RHI::BufferUsage::Storage) ||
				!EnsureMaterialResourceSets(device, context, m_instanceBuffer, m_materialResourceSets)) return false;
		}
		const bool recordShadow = hasDraws && ShouldRecordShadow(context);
		if(recordShadow && !EnsureShadowResourceSet(
				device, context, m_instanceBuffer, m_shadowResourceSet)) return false;

		RHI::TextureHandle backBuffer = device->GetBackBuffer();
		if(backBuffer == nullptr) return false;
		RHI::ICommandList* commandList = device->AcquireCommandList();
		if(commandList == nullptr) return false;
		const bool instanceUploaded = !hasDraws || RecordBufferUpload(
			device, *commandList, m_instanceBuffer, m_instances.data(), instanceBytes,
			m_instanceReady, RHI::ResourceState::ShaderResource);
		const bool shadowRecorded = instanceUploaded &&
			(!recordShadow || RecordShadowDraws(*commandList, scene, context));
		const bool mainBegan = shadowRecorded &&
			BeginMainRendering(*commandList, backBuffer, context, recordShadow);
		if(mainBegan)
		{
			if(hasDraws)
			{
				commandList->BindVertexBuffer(0, m_vertexBuffer, 0);
				commandList->BindIndexBuffer(m_indexBuffer, RHI::Format::R32_UINT, 0);
			}

			const std::vector<SceneMaterialState>& materials = *context.materialStates;
			for(const DrawBatch& batch : m_drawBatches)
			{
				if(batch.meshIndex >= m_meshRanges.size() || batch.materialIndex >= materials.size() ||
					batch.materialIndex >= m_materialResourceSets.size()) continue;
				const MeshRange& range = m_meshRanges[batch.meshIndex];
				if(range.indexCount == 0 || batch.instanceCount == 0) continue;

				commandList->BindResourceSet(m_materialResourceSets[batch.materialIndex]);
				const Layout::DrawConstants constants = MakeDrawConstants(
					*context.config,
					scene.GetMaterial(static_cast<MaterialID>(batch.materialIndex)),
					Transform{},
					batch.textureFlags,
					batch.firstInstance + 1);
				commandList->SetInlineConstants(0, static_cast<uint32_t>(sizeof(constants)), &constants);
				commandList->DrawIndexedInstanced(
					range.indexCount,
					batch.instanceCount,
					range.firstIndex,
					static_cast<int32_t>(range.firstVertex),
					0);
			}

			EndMainRendering(*commandList, backBuffer);
		}
		commandList->Close();
		std::array<RHI::ICommandList*, 1> commands = { commandList };
		const bool submitted = device->Submit(commands.data(), 1);
		if(submitted && hasDraws && instanceUploaded) m_instanceReady = true;
		return submitted && mainBegan;
	}

	void BatchedBindPath::Shutdown(RHI::IDevice* device)
	{
		if(device == nullptr) return;
		DestroyResourceSets(device, m_materialResourceSets);
		if(m_shadowResourceSet != nullptr) device->DestroyResourceSet(m_shadowResourceSet);
		m_shadowResourceSet = nullptr;
		DestroyBuffer(device, m_vertexBuffer, m_vertexBytes, m_vertexReady);
		DestroyBuffer(device, m_indexBuffer, m_indexBytes, m_indexReady);
		DestroyBuffer(device, m_instanceBuffer, m_instanceBytes, m_instanceReady);
		m_meshRanges.clear();
		m_drawBatches.clear();
		m_instances.clear();
	}

}

namespace dy::Graphics
{
	std::unique_ptr<IRenderPath> CreateRenderPath(RendererBindingMode bindingMode)
	{
		switch(bindingMode)
		{
		case RendererBindingMode::Bindless:
			return nullptr;
		case RendererBindingMode::BatchedBind:
			return std::make_unique<BatchedBindPath>();
		case RendererBindingMode::PerDrawBind:
		default:
			return std::make_unique<PerDrawBindPath>();
		}
	}
}
