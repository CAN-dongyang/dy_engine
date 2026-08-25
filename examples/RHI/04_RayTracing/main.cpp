#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

#include "../../Common/ExampleWindow.h"
#include "Math/Math.h"
#include "RHI/Binding.h"
#include "RHI/Buffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/Pipeline.h"
#include "RHI/RayTracing.h"
#include "RHI/Rendering.h"
#include "RHI/ResourceSet.h"
#include "RHI/Shader.h"
#include "RHI/Texture.h"

// 계약 초안: 아래 shader-stage 모델은 DXR/Vulkan에는 가깝지만 Metal과의 공통 계약으로
// 아직 확정하지 않는다. AS build input usage와 build/trace dependency도 공개 RHI에서
// 표현할 방법을 정한 뒤 이 흐름을 확정해야 한다.

namespace
{
	using namespace dy;

	struct Vertex
	{
		float position[3];
	};

	constexpr std::array<Vertex, 6> Vertices = {{
		{{-2.0f, -1.0f, -2.0f}},
		{{ 2.0f, -1.0f, -2.0f}},
		{{ 2.0f, -1.0f,  2.0f}},
		{{-2.0f, -1.0f,  2.0f}},
		{{-0.7f, -1.0f,  0.0f}},
		{{ 0.7f, -1.0f,  0.0f}}
	}};
	constexpr std::array<uint32_t, 9> Indices = {{
		0, 1, 2, 0, 2, 3, 4, 5, 2
	}};

	struct ShaderAsset
	{
		const char* path;
		const char* entryPoint;
		RHI::ShaderStage stage;
	};

#if defined(ENABLE_D3D12)
	constexpr ShaderAsset RayGenerationShader = {
		"Shaders/D3D12/Reflection.rgen.dxil", "rayGeneration", RHI::ShaderStage::RayGeneration };
	constexpr ShaderAsset MissShader = {
		"Shaders/D3D12/Reflection.miss.dxil", "miss", RHI::ShaderStage::Miss };
	constexpr ShaderAsset ClosestHitShader = {
		"Shaders/D3D12/Reflection.chit.dxil", "closestHit", RHI::ShaderStage::ClosestHit };
	constexpr ShaderAsset FullscreenVertexShader = {
		"Shaders/D3D12/FullscreenTriangle.vs.dxil", "main", RHI::ShaderStage::Vertex };
	constexpr ShaderAsset PresentFragmentShader = {
		"Shaders/D3D12/PresentTexture.ps.dxil", "main", RHI::ShaderStage::Fragment };
#elif defined(ENABLE_VULKAN)
	constexpr ShaderAsset RayGenerationShader = {
		"Shaders/Vulkan/Reflection.rgen.spv", "main", RHI::ShaderStage::RayGeneration };
	constexpr ShaderAsset MissShader = {
		"Shaders/Vulkan/Reflection.rmiss.spv", "main", RHI::ShaderStage::Miss };
	constexpr ShaderAsset ClosestHitShader = {
		"Shaders/Vulkan/Reflection.rchit.spv", "main", RHI::ShaderStage::ClosestHit };
	constexpr ShaderAsset FullscreenVertexShader = {
		"Shaders/Vulkan/FullscreenTriangle.vert.spv", "main", RHI::ShaderStage::Vertex };
	constexpr ShaderAsset PresentFragmentShader = {
		"Shaders/Vulkan/PresentTexture.frag.spv", "main", RHI::ShaderStage::Fragment };
#elif defined(ENABLE_METAL)
	constexpr ShaderAsset RayGenerationShader = {
		"Shaders/Metal/Reflection.metallib", "rayGeneration", RHI::ShaderStage::RayGeneration };
	constexpr ShaderAsset MissShader = {
		"Shaders/Metal/Reflection.metallib", "miss", RHI::ShaderStage::Miss };
	constexpr ShaderAsset ClosestHitShader = {
		"Shaders/Metal/Reflection.metallib", "closestHit", RHI::ShaderStage::ClosestHit };
	constexpr ShaderAsset FullscreenVertexShader = {
		"Shaders/Metal/Reflection.metallib", "fullscreenTriangleVertex", RHI::ShaderStage::Vertex };
	constexpr ShaderAsset PresentFragmentShader = {
		"Shaders/Metal/Reflection.metallib", "presentTextureFragment", RHI::ShaderStage::Fragment };
#else
	constexpr ShaderAsset RayGenerationShader = {
		"Shaders/Null/Reflection.rgen.bin", "rayGeneration", RHI::ShaderStage::RayGeneration };
	constexpr ShaderAsset MissShader = {
		"Shaders/Null/Reflection.miss.bin", "miss", RHI::ShaderStage::Miss };
	constexpr ShaderAsset ClosestHitShader = {
		"Shaders/Null/Reflection.chit.bin", "closestHit", RHI::ShaderStage::ClosestHit };
	constexpr ShaderAsset FullscreenVertexShader = {
		"Shaders/Null/FullscreenTriangle.vs.bin", "main", RHI::ShaderStage::Vertex };
	constexpr ShaderAsset PresentFragmentShader = {
		"Shaders/Null/PresentTexture.ps.bin", "main", RHI::ShaderStage::Fragment };
#endif

	[[nodiscard]] std::vector<std::byte> ReadBinary(const std::filesystem::path& path)
	{
		std::ifstream stream(path, std::ios::binary | std::ios::ate);
		if(!stream) throw std::runtime_error("셰이더 바이너리를 열 수 없습니다: " + path.string());
		const std::streamsize size = stream.tellg();
		if(size <= 0) throw std::runtime_error("셰이더 바이너리가 비어 있습니다: " + path.string());
		stream.seekg(0, std::ios::beg);
		std::vector<std::byte> result(static_cast<std::size_t>(size));
		if(!stream.read(reinterpret_cast<char*>(result.data()), size))
			throw std::runtime_error("셰이더 바이너리를 읽을 수 없습니다: " + path.string());
		return result;
	}

	void Require(bool condition, const char* message)
	{
		if(!condition) throw std::runtime_error(message);
	}

	[[nodiscard]] RHI::ShaderHandle CreateShader(RHI::IDevice& device, const ShaderAsset& asset)
	{
		const std::vector<std::byte> binary = ReadBinary(asset.path);
		RHI::ShaderHandle shader = device.CreateShader({
			asset.stage, asset.entryPoint, binary.data(), binary.size()
		});
		Require(shader != nullptr, "셰이더 생성에 실패했습니다.");
		return shader;
	}

	[[nodiscard]] RHI::SamplerDesc LinearSampler()
	{
		RHI::SamplerDesc sampler = {};
		sampler.minFilter = RHI::SamplerFilter::Linear;
		sampler.magFilter = RHI::SamplerFilter::Linear;
		sampler.mipFilter = RHI::SamplerFilter::Linear;
		sampler.addressU = RHI::SamplerAddressMode::ClampToEdge;
		sampler.addressV = RHI::SamplerAddressMode::ClampToEdge;
		sampler.addressW = RHI::SamplerAddressMode::ClampToEdge;
		sampler.mipLodBias = 0.0f;
		sampler.minLod = 0.0f;
		sampler.maxLod = 0.0f;
		return sampler;
	}

	struct Resources
	{
		RHI::BufferHandle vertexBuffer = nullptr;
		RHI::BufferHandle indexBuffer = nullptr;
		RHI::AccelerationStructureHandle bottomLevel = nullptr;
		RHI::AccelerationStructureHandle topLevel = nullptr;
		RHI::TextureHandle reflection = nullptr;
		RHI::ShaderHandle rayGeneration = nullptr;
		RHI::ShaderHandle miss = nullptr;
		RHI::ShaderHandle closestHit = nullptr;
		RHI::ShaderHandle fullscreenVertex = nullptr;
		RHI::ShaderHandle presentFragment = nullptr;
		RHI::PipelineHandle rayPipeline = nullptr;
		RHI::PipelineHandle presentPipeline = nullptr;
		RHI::ResourceSetHandle rayResources = nullptr;
		RHI::ResourceSetHandle presentResources = nullptr;
		RHI::ResourceState reflectionState = RHI::ResourceState::Undefined;
		uint32_t width = 0;
		uint32_t height = 0;

		void DestroyOutput(RHI::IDevice& device)
		{
			if(presentResources != nullptr) device.DestroyResourceSet(presentResources);
			if(rayResources != nullptr) device.DestroyResourceSet(rayResources);
			if(reflection != nullptr) device.DestroyTexture(reflection);
			presentResources = nullptr;
			rayResources = nullptr;
			reflection = nullptr;
			reflectionState = RHI::ResourceState::Undefined;
			width = 0;
			height = 0;
		}

		void Destroy(RHI::IDevice& device)
		{
			DestroyOutput(device);
			if(presentPipeline != nullptr) device.DestroyPipeline(presentPipeline);
			if(rayPipeline != nullptr) device.DestroyPipeline(rayPipeline);
			if(presentFragment != nullptr) device.DestroyShader(presentFragment);
			if(fullscreenVertex != nullptr) device.DestroyShader(fullscreenVertex);
			if(closestHit != nullptr) device.DestroyShader(closestHit);
			if(miss != nullptr) device.DestroyShader(miss);
			if(rayGeneration != nullptr) device.DestroyShader(rayGeneration);
			if(topLevel != nullptr) device.DestroyAccelerationStructure(topLevel);
			if(bottomLevel != nullptr) device.DestroyAccelerationStructure(bottomLevel);
			if(indexBuffer != nullptr) device.DestroyBuffer(indexBuffer);
			if(vertexBuffer != nullptr) device.DestroyBuffer(vertexBuffer);
			*this = {};
		}
	};

	void Submit(RHI::IDevice& device, RHI::ICommandList* commandList)
	{
		Require(commandList != nullptr, "명령 목록을 얻지 못했습니다.");
		commandList->Close();
		RHI::ICommandList* lists[] = { commandList };
		Require(device.Submit(lists, 1), "명령 제출에 실패했습니다.");
	}

	void CreateGeometry(RHI::IDevice& device, Resources& resources)
	{
		resources.vertexBuffer = device.CreateBuffer({
			static_cast<uint32_t>(sizeof(Vertices)),
			static_cast<uint32_t>(sizeof(Vertex)),
			RHI::BufferUsage::Storage,
			RHI::ResourceState::CopyDestination
		});
		resources.indexBuffer = device.CreateBuffer({
			static_cast<uint32_t>(sizeof(Indices)),
			static_cast<uint32_t>(sizeof(uint32_t)),
			RHI::BufferUsage::Storage,
			RHI::ResourceState::CopyDestination
		});
		Require(resources.vertexBuffer != nullptr && resources.indexBuffer != nullptr,
			"geometry buffer 생성에 실패했습니다.");

		RHI::ICommandList* upload = device.AcquireCommandList();
		Require(upload != nullptr, "upload 명령 목록을 얻지 못했습니다.");
		Require(device.UpdateBuffer(
			*upload, resources.vertexBuffer, 0, Vertices.data(), static_cast<uint32_t>(sizeof(Vertices))),
			"vertex upload에 실패했습니다.");
		Require(device.UpdateBuffer(
			*upload, resources.indexBuffer, 0, Indices.data(), static_cast<uint32_t>(sizeof(Indices))),
			"index upload에 실패했습니다.");
		const std::array<RHI::ResourceBarrierDesc, 2> afterUpload = {{
			{ resources.vertexBuffer, nullptr, RHI::ResourceState::CopyDestination,
				RHI::ResourceState::ShaderResource, {} },
			{ resources.indexBuffer, nullptr, RHI::ResourceState::CopyDestination,
				RHI::ResourceState::ShaderResource, {} }
		}};
		upload->ResourceBarrier(afterUpload.data(), static_cast<uint32_t>(afterUpload.size()));
		Submit(device, upload);

		RHI::BottomLevelAccelerationStructureDesc bottomLevel = {};
		bottomLevel.vertexBuffer = resources.vertexBuffer;
		bottomLevel.vertexCount = static_cast<uint32_t>(Vertices.size());
		bottomLevel.vertexStride = static_cast<uint32_t>(sizeof(Vertex));
		bottomLevel.vertexFormat = RHI::Format::R32G32B32_FLOAT;
		bottomLevel.indexBuffer = resources.indexBuffer;
		bottomLevel.indexCount = static_cast<uint32_t>(Indices.size());
		bottomLevel.indexFormat = RHI::Format::R32_UINT;
		resources.bottomLevel = device.CreateBottomLevelAccelerationStructure(bottomLevel);
		Require(resources.bottomLevel != nullptr, "bottom-level acceleration structure 생성에 실패했습니다.");

		RHI::AccelerationStructureInstance instance = {};
		instance.bottomLevel = resources.bottomLevel;
		instance.transform = Math::float4x4::Identity();
		resources.topLevel = device.CreateTopLevelAccelerationStructure({ &instance, 1 });
		Require(resources.topLevel != nullptr, "top-level acceleration structure 생성에 실패했습니다.");

		RHI::ICommandList* build = device.AcquireCommandList();
		Require(build != nullptr, "acceleration structure build 명령 목록을 얻지 못했습니다.");
		build->BuildAccelerationStructure(resources.bottomLevel);
		build->BuildAccelerationStructure(resources.topLevel);
		Submit(device, build);
	}

	void CreatePipelines(RHI::IDevice& device, RHI::Format backBufferFormat, Resources& resources)
	{
		resources.rayGeneration = CreateShader(device, RayGenerationShader);
		resources.miss = CreateShader(device, MissShader);
		resources.closestHit = CreateShader(device, ClosestHitShader);
		resources.fullscreenVertex = CreateShader(device, FullscreenVertexShader);
		resources.presentFragment = CreateShader(device, PresentFragmentShader);

		const std::array<RHI::ResourceBindingLayout, 2> rayBindings = {{
			{ 0, RHI::ResourceBindingType::AccelerationStructure, 1,
				RHI::ShaderStageFlags::RayTracing, {} },
			{ 1, RHI::ResourceBindingType::StorageTexture, 1,
				RHI::ShaderStageFlags::RayTracing, {} }
		}};
		RHI::RayTracingPipelineDesc rayPipeline = {};
		rayPipeline.rayGenerationShader = resources.rayGeneration;
		rayPipeline.missShader = resources.miss;
		rayPipeline.closestHitShader = resources.closestHit;
		rayPipeline.maxRecursionDepth = 1;
		rayPipeline.layout = {
			rayBindings.data(), static_cast<uint32_t>(rayBindings.size()),
			0, RHI::ShaderStageFlags::None, 0
		};
		resources.rayPipeline = device.CreateRayTracingPipeline(rayPipeline);
		Require(resources.rayPipeline != nullptr, "ray tracing pipeline 생성에 실패했습니다.");

		const std::array<RHI::ResourceBindingLayout, 2> presentBindings = {{
			{ 0, RHI::ResourceBindingType::SampledTexture, 1, RHI::ShaderStageFlags::Fragment, {} },
			{ 1, RHI::ResourceBindingType::StaticSampler, 1, RHI::ShaderStageFlags::Fragment, LinearSampler() }
		}};
		const RHI::ColorAttachmentDesc colorAttachment = {
			backBufferFormat, {}, RHI::ColorWriteMask::All
		};
		RHI::GraphicsPipelineDesc present = {};
		present.vertexShader = resources.fullscreenVertex;
		present.fragmentShader = resources.presentFragment;
		present.topology = RHI::PrimitiveTopology::TriangleList;
		present.raster = {
			RHI::FillMode::Solid, RHI::CullMode::None, RHI::FrontFace::CounterClockwise,
			0.0f, 0.0f, 0.0f
		};
		present.colorAttachments = &colorAttachment;
		present.colorAttachmentCount = 1;
		present.layout = {
			presentBindings.data(), static_cast<uint32_t>(presentBindings.size()),
			0, RHI::ShaderStageFlags::None, 0
		};
		resources.presentPipeline = device.CreateGraphicsPipeline(present);
		Require(resources.presentPipeline != nullptr, "present pipeline 생성에 실패했습니다.");
	}

	void CreateOutput(RHI::IDevice& device, uint32_t width, uint32_t height, Resources& resources)
	{
		if(resources.width == width && resources.height == height) return;
		resources.DestroyOutput(device);
		resources.reflection = device.CreateTexture({
			width, height, 1, 1, RHI::Format::R8G8B8A8_UNORM,
			RHI::TextureUsage::Storage | RHI::TextureUsage::ShaderResource
		});
		Require(resources.reflection != nullptr, "reflection texture 생성에 실패했습니다.");

		std::array<RHI::ResourceBinding, 2> rayBindings = {};
		rayBindings[0].binding = 0;
		rayBindings[0].accelerationStructure = resources.topLevel;
		rayBindings[1].binding = 1;
		rayBindings[1].texture = resources.reflection;
		resources.rayResources = device.CreateResourceSet({
			resources.rayPipeline, rayBindings.data(), static_cast<uint32_t>(rayBindings.size())
		});
		Require(resources.rayResources != nullptr, "ray tracing resource set 생성에 실패했습니다.");

		RHI::ResourceBinding presentBinding = {};
		presentBinding.binding = 0;
		presentBinding.texture = resources.reflection;
		resources.presentResources = device.CreateResourceSet({
			resources.presentPipeline, &presentBinding, 1
		});
		Require(resources.presentResources != nullptr, "present resource set 생성에 실패했습니다.");
		resources.width = width;
		resources.height = height;
	}

	void RenderFrame(RHI::IDevice& device, Resources& resources)
	{
		RHI::TextureHandle backBuffer = device.GetBackBuffer();
		Require(backBuffer != nullptr, "backbuffer를 얻지 못했습니다.");
		CreateOutput(device, backBuffer->GetDesc().width, backBuffer->GetDesc().height, resources);

		RHI::ICommandList* commandList = device.AcquireCommandList();
		Require(commandList != nullptr, "frame 명령 목록을 얻지 못했습니다.");
		const RHI::ResourceBarrierDesc beforeTrace = {
			nullptr, resources.reflection,
			resources.reflectionState, RHI::ResourceState::UnorderedAccess, {}
		};
		commandList->ResourceBarrier(&beforeTrace, 1);
		commandList->BindRayTracingPipeline(resources.rayPipeline);
		commandList->BindResourceSet(resources.rayResources);
		commandList->TraceRays(resources.width, resources.height, 1);

		const std::array<RHI::ResourceBarrierDesc, 2> beforePresentPass = {{
			{ nullptr, resources.reflection, RHI::ResourceState::UnorderedAccess,
				RHI::ResourceState::ShaderResource, {} },
			{ nullptr, backBuffer, RHI::ResourceState::Present,
				RHI::ResourceState::RenderTarget, {} }
		}};
		commandList->ResourceBarrier(beforePresentPass.data(),
			static_cast<uint32_t>(beforePresentPass.size()));

		RHI::ColorAttachment color = {};
		color.texture = backBuffer;
		color.loadOp = RHI::LoadOp::Discard;
		color.storeOp = RHI::StoreOp::Store;
		commandList->BeginRendering({ &color, 1, nullptr });
		commandList->BindGraphicsPipeline(resources.presentPipeline);
		commandList->BindResourceSet(resources.presentResources);
		commandList->SetViewport({
			0.0f, 0.0f, static_cast<float>(resources.width),
			static_cast<float>(resources.height), 0.0f, 1.0f
		});
		commandList->SetScissor({ 0, 0, resources.width, resources.height });
		commandList->DrawInstanced(3, 1, 0, 0);
		commandList->EndRendering();

		const RHI::ResourceBarrierDesc beforePresent = {
			nullptr, backBuffer,
			RHI::ResourceState::RenderTarget, RHI::ResourceState::Present, {}
		};
		commandList->ResourceBarrier(&beforePresent, 1);
		Submit(device, commandList);
		resources.reflectionState = RHI::ResourceState::ShaderResource;
		device.Present();
	}
}

int main()
{
	try
	{
		ExampleWindow window(1280, 720, "RHI - Ray-traced Reflection");
		std::unique_ptr<dy::RHI::IDevice> device(
			dy::RHI::IDevice::Create(window.GetHandle()));
		Require(device != nullptr, "RHI device 생성에 실패했습니다.");
		if(!device->Supports(dy::RHI::Feature::RayTracing))
		{
			std::cout << "이 device는 Ray Tracing을 지원하지 않습니다.\n";
			return 0;
		}

		dy::RHI::SwapchainDesc swapchain = {};
		swapchain.format = dy::RHI::Format::B8G8R8A8_UNORM;
		swapchain.minimumImageCount = 2;
		swapchain.presentMode = dy::RHI::PresentMode::Fifo;
		Require(device->CreateSwapchain(swapchain), "swapchain 생성에 실패했습니다.");
		dy::RHI::TextureHandle backBuffer = device->GetBackBuffer();
		Require(backBuffer != nullptr, "초기 backbuffer를 얻지 못했습니다.");

		Resources resources;
		try
		{
			CreateGeometry(*device, resources);
			CreatePipelines(*device, backBuffer->GetDesc().format, resources);
			while(window.IsRunning())
			{
				window.PollEvents();
				if(!device->BeginFrame()) continue;
				RenderFrame(*device, resources);
			}
		}
		catch(...)
		{
			resources.Destroy(*device);
			throw;
		}
		resources.Destroy(*device);
		return 0;
	}
	catch(const std::exception& exception)
	{
		std::cerr << exception.what() << '\n';
		return 1;
	}
}

// 필요한 공개 RHI 추가:
// - Feature::RayTracing, IDevice::Supports
// - RayGeneration/Miss/ClosestHit shader stage와 ShaderStageFlags::RayTracing
// - AccelerationStructureHandle 및 BLAS/TLAS desc·instance, 생성/파괴 API
// - ICommandList::BuildAccelerationStructure
// - ResourceBindingType::AccelerationStructure와 ResourceBinding의 AS handle
// - RayTracingPipelineDesc, IDevice::CreateRayTracingPipeline
// - ICommandList::BindRayTracingPipeline, TraceRays
// - AS build-input buffer usage/state와 BLAS -> TLAS -> trace dependency/barrier
// - staged ray pipeline을 공통화할지 backend-specific extension으로 둘지 결정
