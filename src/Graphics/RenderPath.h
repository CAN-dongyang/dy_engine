#pragma once
/* RenderPath
 *
 * 바인딩 전략(per-draw bind / batched bind)을 캡슐화하는 인터페이스.
 * Renderer 는 공유 리소스(파이프라인, 조명/그림자 버퍼, 머티리얼 상태)만
 * 소유하고, 지오메트리·드로우 리소스의 레지던시와 메인 패스 기록은 path 가 담당한다.
 * 이렇게 해서 단일 Renderer 가 갓 클래스가 되지 않고 전략별로 응집된다.
 */
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "Core/Types.h"
#include "Graphics/RendererConfig.h"
#include "RHI/ResourceHandles.h"
#include "RHI/ResourceState.h"

namespace dy::RHI
{
	class IDevice;
}

namespace dy::Graphics
{
	class Scene;

	inline constexpr uint32_t kMaterialBaseColorTextureSlot = ToIndex(MaterialTextureKind::BaseColor);
	inline constexpr uint32_t kMaterialMetallicRoughnessTextureSlot = ToIndex(MaterialTextureKind::MetallicRoughness);
	inline constexpr uint32_t kMaterialNormalTextureSlot = ToIndex(MaterialTextureKind::Normal);
	inline constexpr uint32_t kMaterialOcclusionTextureSlot = ToIndex(MaterialTextureKind::Occlusion);
	inline constexpr uint32_t kMaterialEmissiveTextureSlot = ToIndex(MaterialTextureKind::Emissive);

	struct SceneMaterialState
	{
		std::array<RHI::TextureHandle, kMaterialTextureCount> textures = {};
		uint32_t textureFlags = 0;
	};

	// Renderer 가 path 에 넘기는 공유 리소스 묶음(소유권은 Renderer 가 유지).
	struct RenderPathContext
	{
		const RendererDesc* config = nullptr;
		RHI::PipelineHandle pipeline = nullptr;
		RHI::TextureHandle depthStencil = nullptr;
		RHI::ResourceState depthStencilState = RHI::ResourceState::Undefined;
		RHI::BufferHandle lightingBuffer = nullptr;
		RHI::BufferHandle shadowMatrixBuffer = nullptr;
		const std::vector<SceneMaterialState>* materialStates = nullptr;

		RHI::PipelineHandle shadowPipeline = nullptr;
		RHI::TextureHandle shadowDepth = nullptr;
		RHI::ResourceState shadowDepthState = RHI::ResourceState::Undefined;
		uint32_t shadowMapResolution = 0;
	};

	class IRenderPath
	{
	public:
		virtual ~IRenderPath() = default;

		// 씬 지오메트리/드로우 리소스를 GPU 에 준비한다(전략별 레이아웃).
		[[nodiscard]] virtual bool PrepareResources(const Scene& scene, RHI::IDevice* device, const RenderPathContext& context) = 0;
		// 메인 포워드 패스의 드로우 명령을 기록/제출한다.
		[[nodiscard]] virtual bool RecordMainPass(const Scene& scene, RHI::IDevice* device, const RenderPathContext& context) = 0;
		// 보유한 GPU 리소스를 해제한다.
		virtual void Shutdown(RHI::IDevice* device) = 0;
	};

	[[nodiscard]] std::unique_ptr<IRenderPath> CreateRenderPath(RendererBindingMode bindingMode);
}
