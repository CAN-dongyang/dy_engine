#include "Graphics/Renderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "Graphics/Private/RendererShaderLayout.h"
#include "Graphics/RenderPath.h"
#include "Graphics/Scene.h"
#include "Math/Math.h"
#include "RHI/Buffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/Pipeline.h"
#include "RHI/Shader.h"
#include "RHI/Texture.h"

#if defined(ENABLE_METAL)
#include "StockMetalLibrary.h"
#else
#include "StockFragmentShader.h"
#include "StockShadowVertexShader.h"
#include "StockVertexShader.h"
#endif

using namespace dy;
using namespace dy::Graphics;

namespace Layout = dy::Graphics::Private::RendererShaderLayout;

namespace
{
	[[nodiscard]] Math::float3 SelectShadowUpVector(const Math::float3& lightForward)
	{
		return std::abs(lightForward.z) > 0.95f
			? Math::float3(0.0f, 1.0f, 0.0f)
			: Math::float3(0.0f, 0.0f, 1.0f);
	}

	[[nodiscard]] Math::float4x4 ComputeDirectionalLightViewProj(
		const Math::float3& lightDirection,
		const ShadowMapDesc& desc)
	{
		const Math::float3 lightForward = Normalize(lightDirection);
		const Math::float3 lightOrigin(
			desc.sceneCenter.x + lightForward.x * desc.lightDistance,
			desc.sceneCenter.y + lightForward.y * desc.lightDistance,
			desc.sceneCenter.z + lightForward.z * desc.lightDistance);
		const Math::float4x4 view = Math::LookAtRH(
			lightOrigin,
			desc.sceneCenter,
			SelectShadowUpVector(lightForward));
		return Math::OrthographicRH_ZO(
			desc.orthoWidth,
			desc.orthoHeight,
			desc.nearPlane,
			desc.farPlane) * view;
	}

	[[nodiscard]] ShadowMapDesc FitDirectionalShadowMapToBounds(
		const Math::float3& lightDirection,
		const ShadowMapDesc& baseDesc,
		const Math::float3& boundsMin,
		const Math::float3& boundsMax,
		float padding)
	{
		ShadowMapDesc desc = baseDesc;
		if(boundsMin.x > boundsMax.x || boundsMin.y > boundsMax.y || boundsMin.z > boundsMax.z)
		{
			return desc;
		}

		padding = std::max(padding, 0.0f);
		const Math::float3 center(
			(boundsMin.x + boundsMax.x) * 0.5f,
			(boundsMin.y + boundsMax.y) * 0.5f,
			(boundsMin.z + boundsMax.z) * 0.5f);
		const Math::float3 halfExtent(
			(boundsMax.x - boundsMin.x) * 0.5f,
			(boundsMax.y - boundsMin.y) * 0.5f,
			(boundsMax.z - boundsMin.z) * 0.5f);
		const float radius = std::max(Length(halfExtent), 0.1f);

		const Math::float3 lightForward = Normalize(lightDirection);
		desc.sceneCenter = center;
		desc.lightDistance = std::max(radius + padding + desc.nearPlane, 0.5f);

		const Math::float3 lightOrigin(
			desc.sceneCenter.x + lightForward.x * desc.lightDistance,
			desc.sceneCenter.y + lightForward.y * desc.lightDistance,
			desc.sceneCenter.z + lightForward.z * desc.lightDistance);
		const Math::float4x4 view = Math::LookAtRH(
			lightOrigin,
			desc.sceneCenter,
			SelectShadowUpVector(lightForward));

		const Math::float3 corners[] = {
			Math::float3(boundsMin.x, boundsMin.y, boundsMin.z),
			Math::float3(boundsMax.x, boundsMin.y, boundsMin.z),
			Math::float3(boundsMin.x, boundsMax.y, boundsMin.z),
			Math::float3(boundsMax.x, boundsMax.y, boundsMin.z),
			Math::float3(boundsMin.x, boundsMin.y, boundsMax.z),
			Math::float3(boundsMax.x, boundsMin.y, boundsMax.z),
			Math::float3(boundsMin.x, boundsMax.y, boundsMax.z),
			Math::float3(boundsMax.x, boundsMax.y, boundsMax.z)
		};

		float minX = std::numeric_limits<float>::max();
		float minY = std::numeric_limits<float>::max();
		float minZ = std::numeric_limits<float>::max();
		float maxX = -std::numeric_limits<float>::max();
		float maxY = -std::numeric_limits<float>::max();
		float maxZ = -std::numeric_limits<float>::max();
		for(const Math::float3& corner : corners)
		{
			const Math::float3 lightSpace = Math::TransformPoint(view, corner);
			minX = std::min(minX, lightSpace.x);
			minY = std::min(minY, lightSpace.y);
			minZ = std::min(minZ, lightSpace.z);
			maxX = std::max(maxX, lightSpace.x);
			maxY = std::max(maxY, lightSpace.y);
			maxZ = std::max(maxZ, lightSpace.z);
		}

		desc.orthoWidth = std::max(maxX - minX + padding * 2.0f, 0.1f);
		desc.orthoHeight = std::max(maxY - minY + padding * 2.0f, 0.1f);
		const float depthRange = std::max(maxZ - minZ + padding * 2.0f, 0.1f);
		desc.farPlane = std::max(
			desc.nearPlane + depthRange + desc.lightDistance,
			desc.nearPlane + 0.1f);
		return desc;
	}

	[[nodiscard]] const DirectionalLight* GetPrimaryDirectionalLight(const Scene& scene)
	{
		return scene.GetDirectionalLightCount() > 0 ? &scene.GetDirectionalLight(0) : nullptr;
	}

	[[nodiscard]] const PointLight* GetPrimaryPointLight(const Scene& scene)
	{
		if(scene.GetPointLightCount() == 0) return nullptr;
		const PointLight& light = scene.GetPointLight(0);
		return light.intensity > 0.0f && light.range > 0.0f ? &light : nullptr;
	}

	[[nodiscard]] Math::Bounds3 ComputeShadowBounds(const Scene& scene)
	{
		Math::Bounds3 bounds = {};
		const uint32_t entityCount = scene.GetEntityCount();
		for(uint32_t entityIndex = 0; entityIndex < entityCount; ++entityIndex)
		{
			const EntityID entity = static_cast<EntityID>(entityIndex);
			const RenderFlags& flags = scene.GetRenderFlags(entity);
			if(!flags.castShadow && !flags.receiveShadow) continue;

			const MeshID meshId = scene.GetEntityMesh(entity);
			if(!IsValid(meshId)) continue;

			const MeshData& mesh = scene.GetMesh(meshId);
			const Transform& transform = scene.GetTransform(entity);
			for(const Vertex& vertex : mesh.vertices)
			{
				bounds.Include(Math::TransformPoint(transform.worldMatrix, vertex.position));
			}
		}
		return bounds;
	}
}

Renderer::Renderer(RendererBindingMode bindingMode)
	: m_initialBindingMode(bindingMode)
{
	m_config.bindingMode = bindingMode;
}

Renderer::Renderer(const RendererDesc& desc)
	: m_config(desc)
	, m_initialBindingMode(desc.bindingMode)
	, m_hasInitialConfig(true)
{
}

bool Renderer::Initialize(RHI::IDevice* device, const RendererDesc& config)
{
	if(device == nullptr) return false;
	m_config = m_hasInitialConfig ? m_config : config;
	if(!m_hasInitialConfig && config.bindingMode == RendererBindingMode::PerDrawBind &&
		m_initialBindingMode != RendererBindingMode::PerDrawBind)
	{
		m_config.bindingMode = m_initialBindingMode;
	}

	RHI::SwapchainDesc swapchainDesc = {};
	swapchainDesc.format = m_config.outputFormat;
	swapchainDesc.minimumImageCount = m_config.backBufferCount;
	swapchainDesc.presentMode = m_config.vsync ? RHI::PresentMode::Fifo : RHI::PresentMode::Immediate;
	if(!device->CreateSwapchain(swapchainDesc)) return false;

	RHI::TextureHandle backBuffer = device->GetBackBuffer();
	if(backBuffer == nullptr || backBuffer->GetDesc().format == RHI::Format::Unknown) return false;
	if(m_config.outputFormat != RHI::Format::Unknown && backBuffer->GetDesc().format != m_config.outputFormat) return false;

#if defined(ENABLE_METAL)
	const void* vertexBinary = dy::Graphics::Private::kStockMetalLibrary;
	const std::size_t vertexBinarySize = dy::Graphics::Private::kStockMetalLibrarySize;
	const void* fragmentBinary = vertexBinary;
	const std::size_t fragmentBinarySize = vertexBinarySize;
	const void* shadowBinary = vertexBinary;
	const std::size_t shadowBinarySize = vertexBinarySize;
	const char* vertexEntry = "vertexShader";
	const char* fragmentEntry = "fragmentShader";
	const char* shadowEntry = "shadowVertexShader";
#else
	const void* vertexBinary = dy::Graphics::Private::kStockVertexShader;
	const std::size_t vertexBinarySize = dy::Graphics::Private::kStockVertexShaderSize;
	const void* fragmentBinary = dy::Graphics::Private::kStockFragmentShader;
	const std::size_t fragmentBinarySize = dy::Graphics::Private::kStockFragmentShaderSize;
	const void* shadowBinary = dy::Graphics::Private::kStockShadowVertexShader;
	const std::size_t shadowBinarySize = dy::Graphics::Private::kStockShadowVertexShaderSize;
	const char* vertexEntry = "main";
	const char* fragmentEntry = "main";
	const char* shadowEntry = "main";
#endif

	m_vertexShader = device->CreateShader(RHI::ShaderDesc{
		RHI::ShaderStage::Vertex, vertexEntry, vertexBinary, vertexBinarySize });
	m_fragmentShader = device->CreateShader(RHI::ShaderDesc{
		RHI::ShaderStage::Fragment, fragmentEntry, fragmentBinary, fragmentBinarySize });
	if(m_config.enableShadows)
	{
		m_shadowVertexShader = device->CreateShader(RHI::ShaderDesc{
			RHI::ShaderStage::Vertex, shadowEntry, shadowBinary, shadowBinarySize });
	}
	if(m_vertexShader == nullptr || m_fragmentShader == nullptr ||
		(m_config.enableShadows && m_shadowVertexShader == nullptr))
	{
		Shutdown(device);
		return false;
	}

	BuildRenderPassPlan();
	BuildPipelineStates(device);
	m_path = CreateRenderPath(m_config.bindingMode);
	if(m_pipeline == nullptr || (m_config.enableShadows && m_shadowPipeline == nullptr) ||
		m_path == nullptr || !CreateDefaultMaterialTextures(device))
	{
		Shutdown(device);
		return false;
	}
	return true;
}

void Renderer::SetCamera(const CameraDesc& camera)
{
	const Math::float4x4 view = Math::LookAtRH(camera.eye, camera.target, camera.up);
	const Math::float4x4 proj = camera.orthographic
		? Math::OrthographicRH_ZO(camera.orthoWidth, camera.orthoHeight, camera.nearPlane, camera.farPlane)
		: Math::PerspectiveRH_ZO(camera.fovYRadians, camera.aspect, camera.nearPlane, camera.farPlane);

	m_config.viewProjectionMatrix = proj * view;
	m_config.cameraPosition = camera.eye;
}

void Renderer::SetViewProjection(const Math::float4x4& viewProjection)
{
	m_config.viewProjectionMatrix = viewProjection;
}

void Renderer::SetCameraPosition(const Math::float3& cameraPosition)
{
	m_config.cameraPosition = cameraPosition;
}

void Renderer::SetAmbientLight(const Math::float3& color, float intensity)
{
	m_config.ambientColor = color;
	m_config.ambientIntensity = intensity;
}

void Renderer::SetPBR(const PBRDesc& pbr)
{
	m_config.pbr = pbr;
}

void Renderer::SetEnvironmentLight(const EnvironmentDesc& environment)
{
	m_config.environment = environment;
}

void Renderer::Shutdown(RHI::IDevice* device)
{
	if(device == nullptr) return;

	if(m_path != nullptr) m_path->Shutdown(device);
	m_path.reset();

	m_gpuScene.Shutdown(device);
	m_materialStates.clear();
	m_renderPasses.clear();

	if(m_lightingBuffer != nullptr)
	{
		device->DestroyBuffer(m_lightingBuffer);
		m_lightingBuffer = nullptr;
		m_lightingBufferReady = false;
	}
	if(m_depthStencilTarget != nullptr)
	{
		device->DestroyTexture(m_depthStencilTarget);
		m_depthStencilTarget = nullptr;
		m_depthStencilState = RHI::ResourceState::Undefined;
	}
	if(m_shadowDepthTarget != nullptr)
	{
		device->DestroyTexture(m_shadowDepthTarget);
		m_shadowDepthTarget = nullptr;
		m_shadowDepthState = RHI::ResourceState::Undefined;
	}
	if(m_shadowMatrixBuffer != nullptr)
	{
		device->DestroyBuffer(m_shadowMatrixBuffer);
		m_shadowMatrixBuffer = nullptr;
		m_shadowMatrixBufferReady = false;
	}
	if(m_shadowPipeline != nullptr)
	{
		device->DestroyPipeline(m_shadowPipeline);
		m_shadowPipeline = nullptr;
	}
	if(m_pipeline != nullptr)
	{
		device->DestroyPipeline(m_pipeline);
		m_pipeline = nullptr;
	}
	for(RHI::TextureHandle& texture : m_defaultMaterialTextures)
	{
		if(texture != nullptr) device->DestroyTexture(texture);
		texture = nullptr;
	}
	if(m_shadowVertexShader != nullptr)
	{
		device->DestroyShader(m_shadowVertexShader);
		m_shadowVertexShader = nullptr;
	}
	if(m_fragmentShader != nullptr)
	{
		device->DestroyShader(m_fragmentShader);
		m_fragmentShader = nullptr;
	}
	if(m_vertexShader != nullptr)
	{
		device->DestroyShader(m_vertexShader);
		m_vertexShader = nullptr;
	}
	m_shadowPassEnabled = false;
}

void Renderer::Render(const Scene& scene, RHI::IDevice* device)
{
	if(device == nullptr || m_path == nullptr || m_pipeline == nullptr) return;

	if(!m_gpuScene.SyncTextures(scene, device)) return;
	EnsureMaterialStateCapacity(scene.GetMaterialCount());
	UpdateMaterialStates(scene);

	RenderPathContext context = {};
	context.config = &m_config;
	context.pipeline = m_pipeline;
	context.materialStates = &m_materialStates;
	EnsureDepthStencilTarget(device);
	EnsureShadowDepthTarget(device);
	context.depthStencil = m_depthStencilTarget;
	context.depthStencilState = m_depthStencilState;
	context.shadowDepth = m_shadowDepthTarget;
	context.shadowDepthState = m_shadowDepthState;
	context.shadowMapResolution = m_shadowDepthTarget != nullptr ? m_shadowDepthTarget->GetDesc().width : 0;
	if(!m_path->PrepareResources(scene, device, context)) return;

	RHI::ICommandList* frameDataCommand = device->AcquireCommandList();
	if(frameDataCommand == nullptr) return;
	const bool shadowUpdated = UpdateShadowBuffer(scene, device, *frameDataCommand);
	const bool lightingUpdated = shadowUpdated &&
		UpdateLightingBuffer(scene, device, *frameDataCommand);
	frameDataCommand->Close();
	std::array<RHI::ICommandList*, 1> frameData = { frameDataCommand };
	const bool frameDataSubmitted = device->Submit(frameData.data(), 1);
	if(frameDataSubmitted)
	{
		if(shadowUpdated) m_shadowMatrixBufferReady = true;
		if(lightingUpdated) m_lightingBufferReady = true;
	}
	if(!frameDataSubmitted || !shadowUpdated || !lightingUpdated) return;

	context.lightingBuffer = m_lightingBuffer;
	context.shadowMatrixBuffer = m_shadowMatrixBuffer;
	const DirectionalLight* shadowLight = GetPrimaryDirectionalLight(scene);
	if(m_shadowPassEnabled && GetPrimaryPointLight(scene) == nullptr &&
		shadowLight != nullptr && shadowLight->castShadow)
	{
		if(m_shadowDepthTarget != nullptr && m_shadowPipeline != nullptr)
		{
			context.shadowPipeline = m_shadowPipeline;
		}
	}

	for(const RenderPassDesc& pass : m_renderPasses)
	{
		if(!pass.enabled) continue;
		if(pass.kind == RenderPassKind::MainForward && pass.work == RenderPassWork::Graphics)
		{
			if(m_path->RecordMainPass(scene, device, context))
			{
				m_depthStencilState = m_depthStencilTarget != nullptr
					? RHI::ResourceState::DepthWrite
					: RHI::ResourceState::Undefined;
				m_shadowDepthState = m_shadowDepthTarget != nullptr
					? RHI::ResourceState::ShaderResource
					: RHI::ResourceState::Undefined;
			}
		}
	}
}

void Renderer::BuildPipelineStates(RHI::IDevice* device)
{
	RHI::TextureHandle backBuffer = device->GetBackBuffer();
	if(backBuffer == nullptr || backBuffer->GetDesc().format == RHI::Format::Unknown ||
		m_vertexShader == nullptr || m_fragmentShader == nullptr) return;

	const RHI::VertexBufferLayout vertexBuffer = {
		0,
		static_cast<uint32_t>(sizeof(Layout::RendererVertex)),
		RHI::VertexStepMode::Vertex
	};
	const std::array<RHI::VertexAttribute, 4> vertexAttributes = {{
		{ 0, 0, RHI::Format::R32G32B32_FLOAT, static_cast<uint32_t>(offsetof(Layout::RendererVertex, px)) },
		{ 1, 0, RHI::Format::R32G32B32_FLOAT, static_cast<uint32_t>(offsetof(Layout::RendererVertex, nx)) },
		{ 2, 0, RHI::Format::R32G32_FLOAT, static_cast<uint32_t>(offsetof(Layout::RendererVertex, u)) },
		{ 3, 0, RHI::Format::R32G32B32A32_FLOAT, static_cast<uint32_t>(offsetof(Layout::RendererVertex, tx)) }
	}};

	RHI::SamplerDesc materialSampler = {};
	materialSampler.minFilter = RHI::SamplerFilter::Linear;
	materialSampler.magFilter = RHI::SamplerFilter::Linear;
	materialSampler.mipFilter = RHI::SamplerFilter::Linear;
	materialSampler.addressU = RHI::SamplerAddressMode::Repeat;
	materialSampler.addressV = RHI::SamplerAddressMode::Repeat;
	materialSampler.addressW = RHI::SamplerAddressMode::Repeat;
	materialSampler.mipLodBias = 0.0f;
	materialSampler.minLod = 0.0f;
	materialSampler.maxLod = 0.0f;

	RHI::SamplerDesc shadowSampler = materialSampler;
	shadowSampler.addressU = RHI::SamplerAddressMode::ClampToEdge;
	shadowSampler.addressV = RHI::SamplerAddressMode::ClampToEdge;
	shadowSampler.addressW = RHI::SamplerAddressMode::ClampToEdge;

	const RHI::ShaderStageFlags vertexAndFragment =
		RHI::ShaderStageFlags::Vertex | RHI::ShaderStageFlags::Fragment;
	const std::array<RHI::ResourceBindingLayout, 11> bindings = {{
		{ RENDERER_BINDING_BASE_COLOR_TEXTURE, RHI::ResourceBindingType::SampledTexture, 1, RHI::ShaderStageFlags::Fragment, {} },
		{ RENDERER_BINDING_LIGHTING_CONSTANTS, RHI::ResourceBindingType::ConstantBuffer, 1, RHI::ShaderStageFlags::Fragment, {} },
		{ RENDERER_BINDING_SHADOW_TEXTURE, RHI::ResourceBindingType::SampledTexture, 1, RHI::ShaderStageFlags::Fragment, {} },
		{ RENDERER_BINDING_SHADOW_MATRIX, RHI::ResourceBindingType::ConstantBuffer, 1, RHI::ShaderStageFlags::Vertex, {} },
		{ RENDERER_BINDING_TRANSFORM_STORAGE, RHI::ResourceBindingType::ReadOnlyStorageBuffer, 1, RHI::ShaderStageFlags::Vertex, {} },
		{ RENDERER_BINDING_METALLIC_ROUGHNESS_TEXTURE, RHI::ResourceBindingType::SampledTexture, 1, RHI::ShaderStageFlags::Fragment, {} },
		{ RENDERER_BINDING_NORMAL_TEXTURE, RHI::ResourceBindingType::SampledTexture, 1, RHI::ShaderStageFlags::Fragment, {} },
		{ RENDERER_BINDING_OCCLUSION_TEXTURE, RHI::ResourceBindingType::SampledTexture, 1, RHI::ShaderStageFlags::Fragment, {} },
		{ RENDERER_BINDING_EMISSIVE_TEXTURE, RHI::ResourceBindingType::SampledTexture, 1, RHI::ShaderStageFlags::Fragment, {} },
		{ RENDERER_BINDING_MATERIAL_SAMPLER, RHI::ResourceBindingType::StaticSampler, 1, RHI::ShaderStageFlags::Fragment, materialSampler },
		{ RENDERER_BINDING_SHADOW_SAMPLER, RHI::ResourceBindingType::StaticSampler, 1, RHI::ShaderStageFlags::Fragment, shadowSampler }
	}};

	const RHI::ColorAttachmentDesc colorAttachment = {
		backBuffer->GetDesc().format,
		{ true, RHI::BlendFactor::SourceAlpha, RHI::BlendFactor::OneMinusSourceAlpha, RHI::BlendOp::Add,
			RHI::BlendFactor::One, RHI::BlendFactor::Zero, RHI::BlendOp::Add },
		RHI::ColorWriteMask::All
	};

	RHI::GraphicsPipelineDesc desc = {};
	desc.vertexShader = m_vertexShader;
	desc.fragmentShader = m_fragmentShader;
	desc.topology = RHI::PrimitiveTopology::TriangleList;
	desc.vertexBuffers = &vertexBuffer;
	desc.vertexBufferCount = 1;
	desc.vertexAttributes = vertexAttributes.data();
	desc.vertexAttributeCount = static_cast<uint32_t>(vertexAttributes.size());
	desc.raster = { RHI::FillMode::Solid, RHI::CullMode::Back, RHI::FrontFace::CounterClockwise, 0.0f, 0.0f, 0.0f };
	desc.depthStencil.format = m_config.depthStencilFormat;
	desc.depthStencil.depthTestEnabled = m_config.depthStencilFormat != RHI::Format::Unknown;
	desc.depthStencil.depthWriteEnabled = desc.depthStencil.depthTestEnabled;
	desc.depthStencil.depthCompareOp = desc.depthStencil.depthTestEnabled ? RHI::CompareOp::Less : RHI::CompareOp::Always;
	desc.colorAttachments = &colorAttachment;
	desc.colorAttachmentCount = 1;
	desc.layout = {
		bindings.data(),
		static_cast<uint32_t>(bindings.size()),
		static_cast<uint32_t>(sizeof(Layout::DrawConstants)),
		vertexAndFragment,
		RENDERER_BINDING_INLINE_CONSTANTS
	};
	m_pipeline = device->CreateGraphicsPipeline(desc);

	m_shadowPassEnabled = IsRenderPassEnabled(RenderPassKind::Shadow) && m_shadowVertexShader != nullptr;
	if(!m_shadowPassEnabled) return;

	const std::array<RHI::ResourceBindingLayout, 2> shadowBindings = {{
		{ RENDERER_BINDING_SHADOW_MATRIX, RHI::ResourceBindingType::ConstantBuffer, 1, RHI::ShaderStageFlags::Vertex, {} },
		{ RENDERER_BINDING_TRANSFORM_STORAGE, RHI::ResourceBindingType::ReadOnlyStorageBuffer, 1, RHI::ShaderStageFlags::Vertex, {} }
	}};
	RHI::GraphicsPipelineDesc shadowDesc = {};
	shadowDesc.vertexShader = m_shadowVertexShader;
	shadowDesc.topology = RHI::PrimitiveTopology::TriangleList;
	shadowDesc.vertexBuffers = &vertexBuffer;
	shadowDesc.vertexBufferCount = 1;
	shadowDesc.vertexAttributes = vertexAttributes.data();
	shadowDesc.vertexAttributeCount = static_cast<uint32_t>(vertexAttributes.size());
	shadowDesc.raster = {
		RHI::FillMode::Solid,
		RHI::CullMode::None,
		RHI::FrontFace::CounterClockwise,
		0.0f,
		m_config.shadowRasterSlopeBias,
		0.0f
	};
	shadowDesc.depthStencil.format = m_config.shadowFormat;
	shadowDesc.depthStencil.depthTestEnabled = true;
	shadowDesc.depthStencil.depthWriteEnabled = true;
	shadowDesc.depthStencil.depthCompareOp = RHI::CompareOp::Less;
	shadowDesc.layout = {
		shadowBindings.data(),
		static_cast<uint32_t>(shadowBindings.size()),
		static_cast<uint32_t>(sizeof(Layout::DrawConstants)),
		RHI::ShaderStageFlags::Vertex,
		RENDERER_BINDING_INLINE_CONSTANTS
	};
	m_shadowPipeline = device->CreateGraphicsPipeline(shadowDesc);
	m_shadowPassEnabled = m_shadowPipeline != nullptr;
}

void Renderer::BuildRenderPassPlan()
{
	m_renderPasses.clear();
	m_renderPasses.push_back(RenderPassDesc{
		RenderPassKind::Shadow,
		RenderPassWork::PrepareOnly,
		"Shadow",
		m_config.enableShadows && m_shadowVertexShader != nullptr
	});
	m_renderPasses.push_back(RenderPassDesc{
		RenderPassKind::MainForward,
		RenderPassWork::Graphics,
		"MainForward",
		m_config.enableMainPass
	});
}

bool Renderer::CreateDefaultMaterialTextures(RHI::IDevice* device)
{
	if(device == nullptr) return false;
	const std::array<std::array<uint8_t, 4>, 3> pixels = {{
		{{ 255, 255, 255, 255 }},
		{{ 128, 128, 255, 255 }},
		{{ 0, 0, 0, 255 }}
	}};

	RHI::TextureDesc desc = {};
	desc.width = 1;
	desc.height = 1;
	desc.depthOrArraySize = 1;
	desc.mipLevels = 1;
	desc.format = RHI::Format::R8G8B8A8_UNORM;
	desc.usage = RHI::TextureUsage::ShaderResource;
	for(RHI::TextureHandle& texture : m_defaultMaterialTextures)
	{
		texture = device->CreateTexture(desc);
		if(texture == nullptr) return false;
	}

	RHI::ICommandList* commandList = device->AcquireCommandList();
	if(commandList == nullptr) return false;
	std::array<RHI::ResourceBarrierDesc, 3> beforeCopy = {};
	std::array<RHI::ResourceBarrierDesc, 3> barriers = {};
	uint32_t barrierCount = 0;
	bool uploadFailed = false;
	for(uint32_t index = 0; index < m_defaultMaterialTextures.size(); ++index)
	{
		beforeCopy[index].texture = m_defaultMaterialTextures[index];
		beforeCopy[index].before = RHI::ResourceState::Undefined;
		beforeCopy[index].after = RHI::ResourceState::CopyDestination;
	}
	commandList->ResourceBarrier(beforeCopy.data(), static_cast<uint32_t>(beforeCopy.size()));
	for(uint32_t index = 0; index < m_defaultMaterialTextures.size(); ++index)
	{
		if(!device->UpdateTexture(
				*commandList,
				m_defaultMaterialTextures[index],
				0,
				0,
				pixels[index].data(),
				static_cast<uint32_t>(pixels[index].size()),
				4,
				4))
		{
			uploadFailed = true;
			continue;
		}
		barriers[barrierCount].texture = m_defaultMaterialTextures[index];
		barriers[barrierCount].before = RHI::ResourceState::CopyDestination;
		barriers[barrierCount].after = RHI::ResourceState::ShaderResource;
		++barrierCount;
	}
	if(barrierCount != 0) commandList->ResourceBarrier(barriers.data(), barrierCount);
	commandList->Close();
	std::array<RHI::ICommandList*, 1> commandLists = { commandList };
	const bool submitted = device->Submit(commandLists.data(), 1);
	return submitted && !uploadFailed;
}

void Renderer::EnsureDepthStencilTarget(RHI::IDevice* device)
{
	if(device == nullptr) return;

	if(m_config.depthStencilFormat == RHI::Format::Unknown)
	{
		if(m_depthStencilTarget != nullptr)
		{
			device->DestroyTexture(m_depthStencilTarget);
			m_depthStencilTarget = nullptr;
			m_depthStencilState = RHI::ResourceState::Undefined;
		}
		return;
	}

	RHI::TextureHandle backBuffer = device->GetBackBuffer();
	if(backBuffer == nullptr || backBuffer->GetDesc().width == 0u || backBuffer->GetDesc().height == 0u) return;

	const bool recreate =
		m_depthStencilTarget == nullptr ||
		m_depthStencilTarget->GetDesc().width != backBuffer->GetDesc().width ||
		m_depthStencilTarget->GetDesc().height != backBuffer->GetDesc().height ||
		m_depthStencilTarget->GetDesc().format != m_config.depthStencilFormat;

	if(!recreate) return;

	if(m_depthStencilTarget != nullptr)
	{
		device->DestroyTexture(m_depthStencilTarget);
		m_depthStencilTarget = nullptr;
		m_depthStencilState = RHI::ResourceState::Undefined;
	}

	RHI::TextureDesc depthDesc = {};
	depthDesc.width = backBuffer->GetDesc().width;
	depthDesc.height = backBuffer->GetDesc().height;
	depthDesc.depthOrArraySize = 1;
	depthDesc.mipLevels = 1;
	depthDesc.format = m_config.depthStencilFormat;
	depthDesc.usage = RHI::TextureUsage::DepthStencil;
	m_depthStencilTarget = device->CreateTexture(depthDesc);
}

void Renderer::EnsureShadowDepthTarget(RHI::IDevice* device)
{
	if(device == nullptr) return;

	const uint32_t resolution = m_shadowPassEnabled ? m_config.shadowMap.resolution : 1u;
	if(resolution == 0) return;
	if(m_shadowDepthTarget != nullptr &&
		m_shadowDepthTarget->GetDesc().width == resolution &&
		m_shadowDepthTarget->GetDesc().height == resolution)
	{
		return;
	}

	if(m_shadowDepthTarget != nullptr)
	{
		device->DestroyTexture(m_shadowDepthTarget);
		m_shadowDepthTarget = nullptr;
		m_shadowDepthState = RHI::ResourceState::Undefined;
	}

	RHI::TextureDesc shadowDesc = {};
	shadowDesc.width = resolution;
	shadowDesc.height = resolution;
	shadowDesc.depthOrArraySize = 1;
	shadowDesc.mipLevels = 1;
	shadowDesc.format = m_config.shadowFormat;
	shadowDesc.usage = RHI::TextureUsage::DepthStencil | RHI::TextureUsage::ShaderResource;
	m_shadowDepthTarget = device->CreateTexture(shadowDesc);
}

void Renderer::EnsureMaterialStateCapacity(std::size_t materialCount)
{
	if(m_materialStates.size() < materialCount)
	{
		m_materialStates.resize(materialCount);
	}
}

void Renderer::UpdateMaterialStates(const Scene& scene)
{
	const uint32_t materialCount = scene.GetMaterialCount();
	for(uint32_t materialIndex = 0; materialIndex < materialCount; ++materialIndex)
	{
		const MaterialDesc& material = scene.GetMaterial(static_cast<MaterialID>(materialIndex));
		SceneMaterialState& materialState = m_materialStates[materialIndex];
		materialState.textures[kMaterialBaseColorTextureSlot] = m_gpuScene.ResolveTexture(material.baseColorTexture);
		materialState.textures[kMaterialMetallicRoughnessTextureSlot] = m_gpuScene.ResolveTexture(material.metallicRoughnessTexture);
		materialState.textures[kMaterialNormalTextureSlot] = m_gpuScene.ResolveTexture(material.normalTexture);
		materialState.textures[kMaterialOcclusionTextureSlot] = m_gpuScene.ResolveTexture(material.occlusionTexture);
		materialState.textures[kMaterialEmissiveTextureSlot] = m_gpuScene.ResolveTexture(material.emissiveTexture);

		uint32_t textureFlags = 0;
		if(materialState.textures[kMaterialBaseColorTextureSlot] != nullptr) textureFlags |= RENDERER_TEXTURE_FLAG_BASE_COLOR;
		if(materialState.textures[kMaterialMetallicRoughnessTextureSlot] != nullptr) textureFlags |= RENDERER_TEXTURE_FLAG_METALLIC_ROUGHNESS;
		if(materialState.textures[kMaterialNormalTextureSlot] != nullptr) textureFlags |= RENDERER_TEXTURE_FLAG_NORMAL;
		if(materialState.textures[kMaterialOcclusionTextureSlot] != nullptr) textureFlags |= RENDERER_TEXTURE_FLAG_OCCLUSION;
		if(materialState.textures[kMaterialEmissiveTextureSlot] != nullptr) textureFlags |= RENDERER_TEXTURE_FLAG_EMISSIVE;
		materialState.textureFlags = textureFlags;

		if(materialState.textures[kMaterialBaseColorTextureSlot] == nullptr)
			materialState.textures[kMaterialBaseColorTextureSlot] = m_defaultMaterialTextures[0];
		if(materialState.textures[kMaterialMetallicRoughnessTextureSlot] == nullptr)
			materialState.textures[kMaterialMetallicRoughnessTextureSlot] = m_defaultMaterialTextures[0];
		if(materialState.textures[kMaterialNormalTextureSlot] == nullptr)
			materialState.textures[kMaterialNormalTextureSlot] = m_defaultMaterialTextures[1];
		if(materialState.textures[kMaterialOcclusionTextureSlot] == nullptr)
			materialState.textures[kMaterialOcclusionTextureSlot] = m_defaultMaterialTextures[0];
		if(materialState.textures[kMaterialEmissiveTextureSlot] == nullptr)
			materialState.textures[kMaterialEmissiveTextureSlot] = m_defaultMaterialTextures[2];
	}
}

bool Renderer::UpdateLightingBuffer(
	const Scene& scene,
	RHI::IDevice* device,
	RHI::ICommandList& commandList)
{
	RHI::TextureHandle backBuffer = device->GetBackBuffer();
	if(backBuffer == nullptr) return false;

	if(m_lightingBuffer == nullptr)
	{
		m_lightingBuffer = device->CreateBuffer(RHI::BufferDesc{
			static_cast<uint32_t>(sizeof(Layout::RendererLightingConstants)),
			static_cast<uint32_t>(sizeof(Layout::RendererLightingConstants)),
			RHI::BufferUsage::Constant,
			RHI::ResourceState::CopyDestination
		});
	}
	if(m_lightingBuffer == nullptr) return false;

	const DirectionalLight* light = GetPrimaryDirectionalLight(scene);
	const PointLight* pointLight = GetPrimaryPointLight(scene);
	const Math::float3 lightDirection = light != nullptr
		? light->direction
		: Math::float3(0.0f, 0.0f, 1.0f);
	const Math::float3 lightColor = light != nullptr
		? light->color
		: Math::float3(0.0f, 0.0f, 0.0f);
	const float lightIntensity = light != nullptr ? light->intensity : 0.0f;
	const bool shadowsEnabled = m_shadowPassEnabled && pointLight == nullptr &&
		light != nullptr && light->castShadow;
	const float shadowStrength = shadowsEnabled ? light->shadowStrength : 0.0f;

	Layout::RendererLightingConstants lighting = {};
	lighting.cameraPosition = Math::float4(
		m_config.cameraPosition.x,
		m_config.cameraPosition.y,
		m_config.cameraPosition.z,
		shadowsEnabled ? shadowStrength : 0.0f);
	lighting.directionalLightDirection = Math::float4(
		lightDirection.x,
		lightDirection.y,
		lightDirection.z,
		shadowsEnabled ? 1.0f : 0.0f);
	lighting.directionalLightColor = Math::float4(lightColor.x, lightColor.y, lightColor.z, lightIntensity);
	lighting.ambientColor = Math::float4(
		m_config.ambientColor.x * m_config.environment.diffuseColor.x,
		m_config.ambientColor.y * m_config.environment.diffuseColor.y,
		m_config.ambientColor.z * m_config.environment.diffuseColor.z,
		m_config.ambientIntensity * m_config.environment.diffuseIntensity);
	lighting.shadowParams = Math::float4(
		m_config.shadowDepthBias,
		m_config.shadowSlopeBias,
		m_config.shadowNormalBias,
		static_cast<float>(m_config.shadowPcfRadius));
	lighting.pbrParams = Math::float4(
		m_config.pbr.minRoughness,
		m_config.pbr.ambientSpecularStrength,
		RHI::IsSrgbFormat(backBuffer->GetDesc().format) ? 0.0f : 1.0f,
		0.0f);
	lighting.environmentColor = Math::float4(
		m_config.environment.specularColor.x,
		m_config.environment.specularColor.y,
		m_config.environment.specularColor.z,
		m_config.environment.specularIntensity);
	if(pointLight != nullptr)
	{
		lighting.pointLightPositionRange = Math::float4(
			pointLight->position.x, pointLight->position.y, pointLight->position.z, pointLight->range);
		lighting.pointLightColorIntensity = Math::float4(
			pointLight->color.x, pointLight->color.y, pointLight->color.z, pointLight->intensity);
	}

	if(m_lightingBufferReady)
	{
		const RHI::ResourceBarrierDesc before = {
			m_lightingBuffer, nullptr,
			RHI::ResourceState::ConstantBuffer,
			RHI::ResourceState::CopyDestination,
			{}
		};
		commandList.ResourceBarrier(&before, 1);
	}
	if(!device->UpdateBuffer(
			commandList,
			m_lightingBuffer,
			0,
			&lighting,
			static_cast<uint32_t>(sizeof(lighting))))
	{
		if(m_lightingBufferReady)
		{
			const RHI::ResourceBarrierDesc restore = {
				m_lightingBuffer, nullptr,
				RHI::ResourceState::CopyDestination,
				RHI::ResourceState::ConstantBuffer,
				{}
			};
			commandList.ResourceBarrier(&restore, 1);
		}
		return false;
	}
	const RHI::ResourceBarrierDesc after = {
		m_lightingBuffer, nullptr,
		RHI::ResourceState::CopyDestination,
		RHI::ResourceState::ConstantBuffer,
		{}
	};
	commandList.ResourceBarrier(&after, 1);
	return true;
}

bool Renderer::UpdateShadowBuffer(
	const Scene& scene,
	RHI::IDevice* device,
	RHI::ICommandList& commandList)
{
	if(m_shadowMatrixBuffer == nullptr)
	{
		m_shadowMatrixBuffer = device->CreateBuffer(RHI::BufferDesc{
			static_cast<uint32_t>(sizeof(Layout::RendererShadowConstants)),
			static_cast<uint32_t>(sizeof(Layout::RendererShadowConstants)),
			RHI::BufferUsage::Constant,
			RHI::ResourceState::CopyDestination
		});
	}
	if(m_shadowMatrixBuffer == nullptr) return false;

	const DirectionalLight* light = GetPrimaryDirectionalLight(scene);
	const bool shadowsEnabled = m_shadowPassEnabled && GetPrimaryPointLight(scene) == nullptr &&
		light != nullptr && light->castShadow;
	const Math::float3 lightDirection = light != nullptr
		? light->direction
		: Math::float3(0.0f, 0.0f, 1.0f);
	ShadowMapDesc shadowMap = m_config.shadowMap;
	const Math::Bounds3 bounds = shadowsEnabled && m_config.autoFitShadowMap
		? ComputeShadowBounds(scene)
		: Math::Bounds3{};
	if(bounds.valid)
	{
		shadowMap = FitDirectionalShadowMapToBounds(
			lightDirection, m_config.shadowMap, bounds.min, bounds.max, m_config.shadowBoundsPadding);
	}

	Layout::RendererShadowConstants shadow = {};
	shadow.lightViewProjectionMatrix = shadowsEnabled
		? ComputeDirectionalLightViewProj(lightDirection, shadowMap)
		: Math::float4x4::Identity();

	if(m_shadowMatrixBufferReady)
	{
		const RHI::ResourceBarrierDesc before = {
			m_shadowMatrixBuffer, nullptr,
			RHI::ResourceState::ConstantBuffer,
			RHI::ResourceState::CopyDestination,
			{}
		};
		commandList.ResourceBarrier(&before, 1);
	}
	if(!device->UpdateBuffer(
			commandList,
			m_shadowMatrixBuffer,
			0,
			&shadow,
			static_cast<uint32_t>(sizeof(shadow))))
	{
		if(m_shadowMatrixBufferReady)
		{
			const RHI::ResourceBarrierDesc restore = {
				m_shadowMatrixBuffer, nullptr,
				RHI::ResourceState::CopyDestination,
				RHI::ResourceState::ConstantBuffer,
				{}
			};
			commandList.ResourceBarrier(&restore, 1);
		}
		return false;
	}
	const RHI::ResourceBarrierDesc after = {
		m_shadowMatrixBuffer, nullptr,
		RHI::ResourceState::CopyDestination,
		RHI::ResourceState::ConstantBuffer,
		{}
	};
	commandList.ResourceBarrier(&after, 1);
	return true;
}

bool Renderer::IsRenderPassEnabled(RenderPassKind passKind) const
{
	for(const RenderPassDesc& pass : m_renderPasses)
	{
		if(pass.kind == passKind)
		{
			return pass.enabled;
		}
	}
	return false;
}
