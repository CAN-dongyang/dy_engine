#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "Core/Types.h"
#include "Graphics/GpuScene.h"
#include "Graphics/RenderPass.h"
#include "Graphics/RenderPath.h"
#include "Graphics/RendererConfig.h"
#include "RHI/ResourceHandles.h"

namespace dy::RHI
{
	class ICommandList;
	class IDevice;
}

namespace dy::Graphics
{
	class Scene;

	class ISceneRenderer
	{
	public:
		virtual ~ISceneRenderer() = default;

		virtual bool Initialize(RHI::IDevice* device, const RendererDesc& desc = {}) = 0;
		virtual void Shutdown(RHI::IDevice* device) = 0;
		virtual void Render(const Scene& scene, RHI::IDevice* device) = 0;
	};

	// 단일 Renderer. 바인딩 전략은 IRenderPath 로 위임하고, 공유 리소스와
	// 렌더 패스 플랜만 직접 소유한다(전략별 코드는 RenderPath.cpp).
	class Renderer : public ISceneRenderer
	{
	public:
		Renderer() = default;
		explicit Renderer(RendererBindingMode bindingMode);
		explicit Renderer(const RendererDesc& desc);
		~Renderer() = default;

		bool Initialize(RHI::IDevice* device, const RendererDesc& desc = {}) override;
		void Shutdown(RHI::IDevice* device) override;
		void Render(const Scene& scene, RHI::IDevice* device) override;
		
		void SetCamera(const CameraDesc& camera);
		void SetViewProjection(const Math::float4x4& viewProjection);
		void SetCameraPosition(const Math::float3& cameraPosition);
		void SetAmbientLight(const Math::float3& color, float intensity);
		void SetPBR(const PBRDesc& pbr);
		void SetEnvironmentLight(const EnvironmentDesc& environment);

	private:
		void BuildPipelineStates(RHI::IDevice* device);
		void BuildRenderPassPlan();
		[[nodiscard]] bool CreateDefaultMaterialTextures(RHI::IDevice* device);
		void EnsureDepthStencilTarget(RHI::IDevice* device);
		void EnsureShadowDepthTarget(RHI::IDevice* device);
		void EnsureMaterialStateCapacity(std::size_t materialCount);
		void UpdateMaterialStates(const Scene& scene);
		[[nodiscard]] bool UpdateLightingBuffer(const Scene& scene, RHI::IDevice* device, RHI::ICommandList& commandList);
		[[nodiscard]] bool UpdateShadowBuffer(const Scene& scene, RHI::IDevice* device, RHI::ICommandList& commandList);
		[[nodiscard]] bool IsRenderPassEnabled(RenderPassKind passKind) const;

		RendererDesc m_config = {};
		std::vector<RenderPassDesc> m_renderPasses;
		RHI::ShaderHandle m_vertexShader = nullptr;
		RHI::ShaderHandle m_fragmentShader = nullptr;
		RHI::ShaderHandle m_shadowVertexShader = nullptr;
		RHI::PipelineHandle m_pipeline = nullptr;
		RHI::PipelineHandle m_shadowPipeline = nullptr;
		RHI::TextureHandle m_depthStencilTarget = nullptr;
		RHI::TextureHandle m_shadowDepthTarget = nullptr;
		RHI::BufferHandle m_lightingBuffer = nullptr;
		RHI::BufferHandle m_shadowMatrixBuffer = nullptr;
		std::array<RHI::TextureHandle, 3> m_defaultMaterialTextures = {};
		RHI::ResourceState m_depthStencilState = RHI::ResourceState::Undefined;
		RHI::ResourceState m_shadowDepthState = RHI::ResourceState::Undefined;
		bool m_lightingBufferReady = false;
		bool m_shadowMatrixBufferReady = false;
		bool m_shadowPassEnabled = false;
		GpuScene m_gpuScene;
		std::vector<SceneMaterialState> m_materialStates;
		std::unique_ptr<IRenderPath> m_path;
		RendererBindingMode m_initialBindingMode = RendererBindingMode::PerDrawBind;
		bool m_hasInitialConfig = false;
	};
}
