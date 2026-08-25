#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "../../Common/ExampleWindow.h"
#include "Math/Math.h"
#include "RHI/Binding.h"
#include "RHI/Buffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/Pipeline.h"
#include "RHI/Rendering.h"
#include "RHI/ResourceSet.h"
#include "RHI/Shader.h"
#include "RHI/Texture.h"

namespace
{
	using namespace dy;

	constexpr uint32_t WindowWidth = 1280;
	constexpr uint32_t WindowHeight = 720;
	constexpr RHI::Format DepthFormat = RHI::Format::D32_FLOAT;

	struct Vertex
	{
		float position[3];
		float uv[2];
	};

	struct CubeConstants
	{
		Math::float4x4 transform;
	};

	constexpr std::array<Vertex, 24> CubeVertices = {{
		{{-1.0f, -1.0f,  1.0f}, {0.0f, 1.0f}},
		{{ 1.0f, -1.0f,  1.0f}, {1.0f, 1.0f}},
		{{ 1.0f,  1.0f,  1.0f}, {1.0f, 0.0f}},
		{{-1.0f,  1.0f,  1.0f}, {0.0f, 0.0f}},
		{{ 1.0f, -1.0f, -1.0f}, {0.0f, 1.0f}},
		{{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f}},
		{{-1.0f,  1.0f, -1.0f}, {1.0f, 0.0f}},
		{{ 1.0f,  1.0f, -1.0f}, {0.0f, 0.0f}},
		{{-1.0f, -1.0f, -1.0f}, {0.0f, 1.0f}},
		{{-1.0f, -1.0f,  1.0f}, {1.0f, 1.0f}},
		{{-1.0f,  1.0f,  1.0f}, {1.0f, 0.0f}},
		{{-1.0f,  1.0f, -1.0f}, {0.0f, 0.0f}},
		{{ 1.0f, -1.0f,  1.0f}, {0.0f, 1.0f}},
		{{ 1.0f, -1.0f, -1.0f}, {1.0f, 1.0f}},
		{{ 1.0f,  1.0f, -1.0f}, {1.0f, 0.0f}},
		{{ 1.0f,  1.0f,  1.0f}, {0.0f, 0.0f}},
		{{-1.0f,  1.0f,  1.0f}, {0.0f, 1.0f}},
		{{ 1.0f,  1.0f,  1.0f}, {1.0f, 1.0f}},
		{{ 1.0f,  1.0f, -1.0f}, {1.0f, 0.0f}},
		{{-1.0f,  1.0f, -1.0f}, {0.0f, 0.0f}},
		{{-1.0f, -1.0f, -1.0f}, {0.0f, 1.0f}},
		{{ 1.0f, -1.0f, -1.0f}, {1.0f, 1.0f}},
		{{ 1.0f, -1.0f,  1.0f}, {1.0f, 0.0f}},
		{{-1.0f, -1.0f,  1.0f}, {0.0f, 0.0f}}
	}};

	constexpr std::array<uint16_t, 36> CubeIndices = {{
		 0,  1,  2,  0,  2,  3,
		 4,  5,  6,  4,  6,  7,
		 8,  9, 10,  8, 10, 11,
		12, 13, 14, 12, 14, 15,
		16, 17, 18, 16, 18, 19,
		20, 21, 22, 20, 22, 23
	}};

	constexpr std::array<uint8_t, 4 * 4 * 4> CheckerPixels = {{
		245, 245, 245, 255,  35,  95, 210, 255, 245, 245, 245, 255,  35,  95, 210, 255,
		 35,  95, 210, 255, 245, 245, 245, 255,  35,  95, 210, 255, 245, 245, 245, 255,
		245, 245, 245, 255,  35,  95, 210, 255, 245, 245, 245, 255,  35,  95, 210, 255,
		 35,  95, 210, 255, 245, 245, 245, 255,  35,  95, 210, 255, 245, 245, 245, 255
	}};

	struct ShaderAsset
	{
		const char* path;
		const char* entryPoint;
		RHI::ShaderStage stage;
	};

	// CMake가 선택한 backend 정의를 예제 target에도 전달하고 아래 경로에
	// backend별 shader binary를 생성해야 한다. 바이너리는 소스 저장소에 두지 않는다.
#if defined(ENABLE_D3D12)
	constexpr ShaderAsset VertexShaderAsset = {
		"Shaders/D3D12/TexturedCube.vs.dxil", "main", RHI::ShaderStage::Vertex };
	constexpr ShaderAsset FragmentShaderAsset = {
		"Shaders/D3D12/TexturedCube.ps.dxil", "main", RHI::ShaderStage::Fragment };
#elif defined(ENABLE_VULKAN)
	constexpr ShaderAsset VertexShaderAsset = {
		"Shaders/Vulkan/TexturedCube.vert.spv", "main", RHI::ShaderStage::Vertex };
	constexpr ShaderAsset FragmentShaderAsset = {
		"Shaders/Vulkan/TexturedCube.frag.spv", "main", RHI::ShaderStage::Fragment };
#elif defined(ENABLE_METAL)
	constexpr ShaderAsset VertexShaderAsset = {
		"Shaders/Metal/TexturedCube.metallib", "texturedCubeVertex", RHI::ShaderStage::Vertex };
	constexpr ShaderAsset FragmentShaderAsset = {
		"Shaders/Metal/TexturedCube.metallib", "texturedCubeFragment", RHI::ShaderStage::Fragment };
#else
	constexpr ShaderAsset VertexShaderAsset = {
		"Shaders/Null/TexturedCube.vs.bin", "main", RHI::ShaderStage::Vertex };
	constexpr ShaderAsset FragmentShaderAsset = {
		"Shaders/Null/TexturedCube.ps.bin", "main", RHI::ShaderStage::Fragment };
#endif

	[[nodiscard]] std::vector<std::byte> ReadBinary(const std::filesystem::path& path)
	{
		std::ifstream stream(path, std::ios::binary | std::ios::ate);
		if(!stream) throw std::runtime_error("셰이더 바이너리를 열 수 없습니다: " + path.string());
		const std::streamsize size = stream.tellg();
		if(size <= 0) throw std::runtime_error("셰이더 바이너리가 비어 있습니다: " + path.string());
		stream.seekg(0, std::ios::beg);
		std::vector<std::byte> binary(static_cast<std::size_t>(size));
		if(!stream.read(reinterpret_cast<char*>(binary.data()), size))
			throw std::runtime_error("셰이더 바이너리를 읽을 수 없습니다: " + path.string());
		return binary;
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
		if(shader == nullptr) throw std::runtime_error("셰이더 생성에 실패했습니다: " + std::string(asset.path));
		return shader;
	}

	[[nodiscard]] RHI::SamplerDesc LinearSampler()
	{
		RHI::SamplerDesc sampler = {};
		sampler.minFilter = RHI::SamplerFilter::Linear;
		sampler.magFilter = RHI::SamplerFilter::Linear;
		sampler.mipFilter = RHI::SamplerFilter::Linear;
		sampler.addressU = RHI::SamplerAddressMode::Repeat;
		sampler.addressV = RHI::SamplerAddressMode::Repeat;
		sampler.addressW = RHI::SamplerAddressMode::Repeat;
		sampler.mipLodBias = 0.0f;
		sampler.minLod = 0.0f;
		sampler.maxLod = 0.0f;
		return sampler;
	}

	struct Resources
	{
		RHI::BufferHandle vertexBuffer = nullptr;
		RHI::BufferHandle indexBuffer = nullptr;
		RHI::TextureHandle sourceTexture = nullptr;
		RHI::TextureHandle depthTexture = nullptr;
		RHI::ShaderHandle vertexShader = nullptr;
		RHI::ShaderHandle fragmentShader = nullptr;
		RHI::PipelineHandle pipeline = nullptr;
		RHI::ResourceSetHandle resourceSet = nullptr;
		RHI::ResourceState depthState = RHI::ResourceState::Undefined;
		uint32_t depthWidth = 0;
		uint32_t depthHeight = 0;

		void DestroyDepth(RHI::IDevice& device)
		{
			if(depthTexture != nullptr) device.DestroyTexture(depthTexture);
			depthTexture = nullptr;
			depthState = RHI::ResourceState::Undefined;
			depthWidth = 0;
			depthHeight = 0;
		}

		void Destroy(RHI::IDevice& device)
		{
			DestroyDepth(device);
			if(resourceSet != nullptr) device.DestroyResourceSet(resourceSet);
			if(pipeline != nullptr) device.DestroyPipeline(pipeline);
			if(fragmentShader != nullptr) device.DestroyShader(fragmentShader);
			if(vertexShader != nullptr) device.DestroyShader(vertexShader);
			if(sourceTexture != nullptr) device.DestroyTexture(sourceTexture);
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

	void CreatePipeline(RHI::IDevice& device, RHI::Format backBufferFormat, Resources& resources)
	{
		resources.vertexShader = CreateShader(device, VertexShaderAsset);
		resources.fragmentShader = CreateShader(device, FragmentShaderAsset);

		const RHI::VertexBufferLayout vertexLayout = {
			0, static_cast<uint32_t>(sizeof(Vertex)), RHI::VertexStepMode::Vertex };
		const std::array<RHI::VertexAttribute, 2> attributes = {{
			{ 0, 0, RHI::Format::R32G32B32_FLOAT, static_cast<uint32_t>(offsetof(Vertex, position)) },
			{ 1, 0, RHI::Format::R32G32_FLOAT, static_cast<uint32_t>(offsetof(Vertex, uv)) }
		}};
		const std::array<RHI::ResourceBindingLayout, 2> bindings = {{
			{ 0, RHI::ResourceBindingType::SampledTexture, 1, RHI::ShaderStageFlags::Fragment, {} },
			{ 1, RHI::ResourceBindingType::StaticSampler, 1, RHI::ShaderStageFlags::Fragment, LinearSampler() }
		}};
		const RHI::ColorAttachmentDesc colorAttachment = {
			backBufferFormat, {}, RHI::ColorWriteMask::All };

		RHI::GraphicsPipelineDesc pipeline = {};
		pipeline.vertexShader = resources.vertexShader;
		pipeline.fragmentShader = resources.fragmentShader;
		pipeline.topology = RHI::PrimitiveTopology::TriangleList;
		pipeline.vertexBuffers = &vertexLayout;
		pipeline.vertexBufferCount = 1;
		pipeline.vertexAttributes = attributes.data();
		pipeline.vertexAttributeCount = static_cast<uint32_t>(attributes.size());
		pipeline.raster = {
			RHI::FillMode::Solid, RHI::CullMode::Back, RHI::FrontFace::CounterClockwise,
			0.0f, 0.0f, 0.0f };
		pipeline.depthStencil.format = DepthFormat;
		pipeline.depthStencil.depthTestEnabled = true;
		pipeline.depthStencil.depthWriteEnabled = true;
		pipeline.depthStencil.depthCompareOp = RHI::CompareOp::Less;
		pipeline.colorAttachments = &colorAttachment;
		pipeline.colorAttachmentCount = 1;
		pipeline.layout = {
			bindings.data(), static_cast<uint32_t>(bindings.size()),
			static_cast<uint32_t>(sizeof(CubeConstants)), RHI::ShaderStageFlags::Vertex, 2 };
		resources.pipeline = device.CreateGraphicsPipeline(pipeline);
		Require(resources.pipeline != nullptr, "graphics pipeline 생성에 실패했습니다.");
	}

	void CreateAndUploadResources(RHI::IDevice& device, Resources& resources)
	{
		resources.vertexBuffer = device.CreateBuffer({
			static_cast<uint32_t>(sizeof(CubeVertices)), static_cast<uint32_t>(sizeof(Vertex)),
			RHI::BufferUsage::Vertex, RHI::ResourceState::CopyDestination
		});
		resources.indexBuffer = device.CreateBuffer({
			static_cast<uint32_t>(sizeof(CubeIndices)), static_cast<uint32_t>(sizeof(uint16_t)),
			RHI::BufferUsage::Index, RHI::ResourceState::CopyDestination
		});
		resources.sourceTexture = device.CreateTexture({
			4, 4, 1, 1, RHI::Format::R8G8B8A8_UNORM, RHI::TextureUsage::ShaderResource
		});
		Require(resources.vertexBuffer != nullptr && resources.indexBuffer != nullptr &&
			resources.sourceTexture != nullptr, "정적 자원 생성에 실패했습니다.");

		RHI::ICommandList* upload = device.AcquireCommandList();
		Require(upload != nullptr, "upload 명령 목록을 얻지 못했습니다.");
		const RHI::ResourceBarrierDesc beforeTextureUpload = {
			nullptr, resources.sourceTexture,
			RHI::ResourceState::Undefined, RHI::ResourceState::CopyDestination, {}
		};
		upload->ResourceBarrier(&beforeTextureUpload, 1);
		Require(device.UpdateBuffer(
			*upload, resources.vertexBuffer, 0, CubeVertices.data(), static_cast<uint32_t>(sizeof(CubeVertices))),
			"vertex upload에 실패했습니다.");
		Require(device.UpdateBuffer(
			*upload, resources.indexBuffer, 0, CubeIndices.data(), static_cast<uint32_t>(sizeof(CubeIndices))),
			"index upload에 실패했습니다.");
		Require(device.UpdateTexture(
			*upload, resources.sourceTexture, 0, 0, CheckerPixels.data(),
			static_cast<uint32_t>(CheckerPixels.size()), 16, 64),
			"texture upload에 실패했습니다.");

		const std::array<RHI::ResourceBarrierDesc, 3> afterUpload = {{
			{ resources.vertexBuffer, nullptr, RHI::ResourceState::CopyDestination,
				RHI::ResourceState::VertexBuffer, {} },
			{ resources.indexBuffer, nullptr, RHI::ResourceState::CopyDestination,
				RHI::ResourceState::IndexBuffer, {} },
			{ nullptr, resources.sourceTexture, RHI::ResourceState::CopyDestination,
				RHI::ResourceState::ShaderResource, {} }
		}};
		upload->ResourceBarrier(afterUpload.data(), static_cast<uint32_t>(afterUpload.size()));
		Submit(device, upload);

		const RHI::ResourceBinding textureBinding = {
			0, 0, nullptr, resources.sourceTexture, 0, 0, {}
		};
		resources.resourceSet = device.CreateResourceSet({ resources.pipeline, &textureBinding, 1 });
		Require(resources.resourceSet != nullptr, "resource set 생성에 실패했습니다.");
	}

	void EnsureDepth(RHI::IDevice& device, uint32_t width, uint32_t height, Resources& resources)
	{
		if(resources.depthWidth == width && resources.depthHeight == height) return;
		resources.DestroyDepth(device);
		resources.depthTexture = device.CreateTexture({
			width, height, 1, 1, DepthFormat, RHI::TextureUsage::DepthStencil
		});
		Require(resources.depthTexture != nullptr, "depth texture 생성에 실패했습니다.");
		resources.depthWidth = width;
		resources.depthHeight = height;
	}

	[[nodiscard]] CubeConstants MakeCubeConstants(float seconds, float aspect)
	{
		const Math::float4x4 model =
			Math::RotationY(seconds * 0.75f) * Math::RotationX(seconds * 0.35f);
		const Math::float4x4 view = Math::LookAtRH(
			Math::float3(0.0f, 1.2f, 4.0f), Math::float3(0.0f, 0.0f, 0.0f),
			Math::float3(0.0f, 1.0f, 0.0f));
		const Math::float4x4 projection = Math::PerspectiveRH_ZO(1.05f, aspect, 0.1f, 100.0f);
		return { projection * view * model };
	}

	void RenderFrame(RHI::IDevice& device, Resources& resources, float seconds)
	{
		RHI::TextureHandle backBuffer = device.GetBackBuffer();
		Require(backBuffer != nullptr, "backbuffer를 얻지 못했습니다.");
		const uint32_t width = backBuffer->GetDesc().width;
		const uint32_t height = backBuffer->GetDesc().height;
		Require(width != 0 && height != 0, "backbuffer 크기가 0입니다.");
		EnsureDepth(device, width, height, resources);

		RHI::ICommandList* commandList = device.AcquireCommandList();
		Require(commandList != nullptr, "frame 명령 목록을 얻지 못했습니다.");
		std::array<RHI::ResourceBarrierDesc, 2> barriers = {{
			{ nullptr, backBuffer, RHI::ResourceState::Present, RHI::ResourceState::RenderTarget, {} },
			{ nullptr, resources.depthTexture, resources.depthState, RHI::ResourceState::DepthWrite, {} }
		}};
		const uint32_t barrierCount = resources.depthState == RHI::ResourceState::DepthWrite ? 1u : 2u;
		commandList->ResourceBarrier(barriers.data(), barrierCount);

		RHI::ColorAttachment color = {};
		color.texture = backBuffer;
		color.loadOp = RHI::LoadOp::Clear;
		color.storeOp = RHI::StoreOp::Store;
		color.clearColor[0] = 0.03f;
		color.clearColor[1] = 0.05f;
		color.clearColor[2] = 0.09f;
		color.clearColor[3] = 1.0f;
		RHI::DepthStencilAttachment depth = {};
		depth.texture = resources.depthTexture;
		depth.state = RHI::ResourceState::DepthWrite;
		depth.depthLoadOp = RHI::LoadOp::Clear;
		depth.depthStoreOp = RHI::StoreOp::Discard;
		depth.clearDepth = 1.0f;

		commandList->BeginRendering({ &color, 1, &depth });
		commandList->BindGraphicsPipeline(resources.pipeline);
		commandList->BindResourceSet(resources.resourceSet);
		commandList->BindVertexBuffer(0, resources.vertexBuffer, 0);
		commandList->BindIndexBuffer(resources.indexBuffer, RHI::Format::R16_UINT, 0);
		commandList->SetViewport({
			0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f });
		commandList->SetScissor({ 0, 0, width, height });
		const CubeConstants constants = MakeCubeConstants(seconds, static_cast<float>(width) / height);
		commandList->SetInlineConstants(0, static_cast<uint32_t>(sizeof(constants)), &constants);
		commandList->DrawIndexedInstanced(static_cast<uint32_t>(CubeIndices.size()), 1, 0, 0, 0);
		commandList->EndRendering();

		const RHI::ResourceBarrierDesc beforePresent = {
			nullptr, backBuffer, RHI::ResourceState::RenderTarget, RHI::ResourceState::Present, {}
		};
		commandList->ResourceBarrier(&beforePresent, 1);
		Submit(device, commandList);
		resources.depthState = RHI::ResourceState::DepthWrite;
		device.Present();
	}
}

int main()
{
	try
	{
		ExampleWindow window(WindowWidth, WindowHeight, "RHI - Textured Cube");
		std::unique_ptr<dy::RHI::IDevice> device(
			dy::RHI::IDevice::Create(window.GetHandle(), { 2 }));
		if(device == nullptr) throw std::runtime_error("RHI device 생성에 실패했습니다.");

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
			CreatePipeline(*device, backBuffer->GetDesc().format, resources);
			CreateAndUploadResources(*device, resources);
			const auto start = std::chrono::steady_clock::now();
			while(window.IsRunning())
			{
				window.PollEvents();
				if(!device->BeginFrame()) continue;
				const float seconds = std::chrono::duration<float>(
					std::chrono::steady_clock::now() - start).count();
				RenderFrame(*device, resources, seconds);
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
