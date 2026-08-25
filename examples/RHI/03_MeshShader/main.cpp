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
#include "RHI/Binding.h"
#include "RHI/Buffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/Pipeline.h"
#include "RHI/Rendering.h"
#include "RHI/ResourceSet.h"
#include "RHI/Shader.h"
#include "RHI/Texture.h"

// 한 workgroup이 한 meshlet을 출력하는 최소 Mesh Shader 계약이다.
// task/amplification shader와 culling은 이 첫 계약에 포함하지 않는다.

namespace
{
	using namespace dy;

	struct MeshletVertex
	{
		float position[3];
		float color[3];
	};

	constexpr std::array<MeshletVertex, 3> Vertices = {{
		{{-0.7f, -0.6f, 0.0f}, {1.0f, 0.2f, 0.1f}},
		{{ 0.7f, -0.6f, 0.0f}, {0.1f, 1.0f, 0.2f}},
		{{ 0.0f,  0.7f, 0.0f}, {0.2f, 0.4f, 1.0f}}
	}};
	constexpr std::array<uint32_t, 3> Indices = {{ 0, 1, 2 }};

#if defined(ENABLE_D3D12)
	constexpr const char* MeshShaderPath = "Shaders/D3D12/Meshlet.ms.dxil";
	constexpr const char* FragmentShaderPath = "Shaders/D3D12/Meshlet.ps.dxil";
#elif defined(ENABLE_VULKAN)
	constexpr const char* MeshShaderPath = "Shaders/Vulkan/Meshlet.mesh.spv";
	constexpr const char* FragmentShaderPath = "Shaders/Vulkan/Meshlet.frag.spv";
#elif defined(ENABLE_METAL)
	constexpr const char* MeshShaderPath = "Shaders/Metal/Meshlet.metallib";
	constexpr const char* FragmentShaderPath = "Shaders/Metal/Meshlet.metallib";
#else
	constexpr const char* MeshShaderPath = "Shaders/Null/Meshlet.ms.bin";
	constexpr const char* FragmentShaderPath = "Shaders/Null/Meshlet.ps.bin";
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

	[[nodiscard]] RHI::ShaderHandle CreateShader(
		RHI::IDevice& device,
		const char* path,
		RHI::ShaderStage stage,
		const char* entryPoint)
	{
		const std::vector<std::byte> binary = ReadBinary(path);
		RHI::ShaderHandle shader = device.CreateShader({
			stage, entryPoint, binary.data(), binary.size()
		});
		Require(shader != nullptr, "셰이더 생성에 실패했습니다.");
		return shader;
	}
}

int main()
{
	try
	{
		ExampleWindow window(1280, 720, "RHI - Mesh Shader");
		std::unique_ptr<dy::RHI::IDevice> device(
			dy::RHI::IDevice::Create(window.GetHandle()));
		Require(device != nullptr, "RHI device 생성에 실패했습니다.");
		if(!device->Supports(dy::RHI::Feature::MeshShader))
		{
			std::cout << "이 device는 Mesh Shader를 지원하지 않습니다.\n";
			return 0;
		}

		dy::RHI::SwapchainDesc swapchain = {};
		swapchain.format = dy::RHI::Format::B8G8R8A8_UNORM;
		swapchain.minimumImageCount = 2;
		swapchain.presentMode = dy::RHI::PresentMode::Fifo;
		Require(device->CreateSwapchain(swapchain), "swapchain 생성에 실패했습니다.");
		dy::RHI::TextureHandle firstBackBuffer = device->GetBackBuffer();
		Require(firstBackBuffer != nullptr, "backbuffer를 얻지 못했습니다.");

#if defined(ENABLE_METAL)
		constexpr const char* MeshEntryPoint = "meshletMesh";
		constexpr const char* FragmentEntryPoint = "meshletFragment";
#else
		constexpr const char* MeshEntryPoint = "main";
		constexpr const char* FragmentEntryPoint = "main";
#endif
		dy::RHI::ShaderHandle meshShader = CreateShader(
			*device, MeshShaderPath, dy::RHI::ShaderStage::Mesh, MeshEntryPoint);
		dy::RHI::ShaderHandle fragmentShader = CreateShader(
			*device, FragmentShaderPath, dy::RHI::ShaderStage::Fragment, FragmentEntryPoint);

		const std::array<dy::RHI::ResourceBindingLayout, 2> bindings = {{
			{ 0, dy::RHI::ResourceBindingType::ReadOnlyStorageBuffer, 1,
				dy::RHI::ShaderStageFlags::Mesh, {} },
			{ 1, dy::RHI::ResourceBindingType::ReadOnlyStorageBuffer, 1,
				dy::RHI::ShaderStageFlags::Mesh, {} }
		}};
		const dy::RHI::ColorAttachmentDesc colorAttachment = {
			firstBackBuffer->GetDesc().format, {}, dy::RHI::ColorWriteMask::All
		};
		dy::RHI::GraphicsPipelineDesc pipelineDesc = {};
		pipelineDesc.meshShader = meshShader;
		pipelineDesc.fragmentShader = fragmentShader;
		pipelineDesc.topology = dy::RHI::PrimitiveTopology::TriangleList;
		pipelineDesc.raster = {
			dy::RHI::FillMode::Solid,
			dy::RHI::CullMode::None,
			dy::RHI::FrontFace::CounterClockwise,
			0.0f, 0.0f, 0.0f
		};
		pipelineDesc.colorAttachments = &colorAttachment;
		pipelineDesc.colorAttachmentCount = 1;
		pipelineDesc.layout = {
			bindings.data(), static_cast<uint32_t>(bindings.size()),
			0, dy::RHI::ShaderStageFlags::None, 0
		};
		dy::RHI::PipelineHandle pipeline = device->CreateGraphicsPipeline(pipelineDesc);
		Require(pipeline != nullptr, "mesh pipeline 생성에 실패했습니다.");

		dy::RHI::BufferHandle vertexBuffer = device->CreateBuffer({
			static_cast<uint32_t>(sizeof(Vertices)),
			static_cast<uint32_t>(sizeof(MeshletVertex)),
			dy::RHI::BufferUsage::Storage,
			dy::RHI::ResourceState::CopyDestination
		});
		dy::RHI::BufferHandle indexBuffer = device->CreateBuffer({
			static_cast<uint32_t>(sizeof(Indices)),
			static_cast<uint32_t>(sizeof(uint32_t)),
			dy::RHI::BufferUsage::Storage,
			dy::RHI::ResourceState::CopyDestination
		});
		Require(vertexBuffer != nullptr && indexBuffer != nullptr, "meshlet buffer 생성에 실패했습니다.");

		dy::RHI::ICommandList* upload = device->AcquireCommandList();
		Require(upload != nullptr, "upload 명령 목록을 얻지 못했습니다.");
		Require(device->UpdateBuffer(
			*upload, vertexBuffer, 0, Vertices.data(), static_cast<uint32_t>(sizeof(Vertices))),
			"vertex upload에 실패했습니다.");
		Require(device->UpdateBuffer(
			*upload, indexBuffer, 0, Indices.data(), static_cast<uint32_t>(sizeof(Indices))),
			"index upload에 실패했습니다.");
		const std::array<dy::RHI::ResourceBarrierDesc, 2> afterUpload = {{
			{ vertexBuffer, nullptr, dy::RHI::ResourceState::CopyDestination,
				dy::RHI::ResourceState::ShaderResource, {} },
			{ indexBuffer, nullptr, dy::RHI::ResourceState::CopyDestination,
				dy::RHI::ResourceState::ShaderResource, {} }
		}};
		upload->ResourceBarrier(afterUpload.data(), static_cast<uint32_t>(afterUpload.size()));
		upload->Close();
		dy::RHI::ICommandList* uploadLists[] = { upload };
		Require(device->Submit(uploadLists, 1), "meshlet upload 제출에 실패했습니다.");

		const std::array<dy::RHI::ResourceBinding, 2> resources = {{
			{ 0, 0, vertexBuffer, nullptr, 0, static_cast<uint32_t>(sizeof(Vertices)), {} },
			{ 1, 0, indexBuffer, nullptr, 0, static_cast<uint32_t>(sizeof(Indices)), {} }
		}};
		dy::RHI::ResourceSetHandle resourceSet = device->CreateResourceSet({
			pipeline, resources.data(), static_cast<uint32_t>(resources.size())
		});
		Require(resourceSet != nullptr, "meshlet resource set 생성에 실패했습니다.");

		while(window.IsRunning())
		{
			window.PollEvents();
			if(!device->BeginFrame()) continue;
			dy::RHI::TextureHandle backBuffer = device->GetBackBuffer();
			Require(backBuffer != nullptr, "backbuffer를 얻지 못했습니다.");

			dy::RHI::ICommandList* commandList = device->AcquireCommandList();
			Require(commandList != nullptr, "frame 명령 목록을 얻지 못했습니다.");
			const dy::RHI::ResourceBarrierDesc toRenderTarget = {
				nullptr, backBuffer,
				dy::RHI::ResourceState::Present,
				dy::RHI::ResourceState::RenderTarget,
				{}
			};
			commandList->ResourceBarrier(&toRenderTarget, 1);
			dy::RHI::ColorAttachment color = {};
			color.texture = backBuffer;
			color.loadOp = dy::RHI::LoadOp::Clear;
			color.storeOp = dy::RHI::StoreOp::Store;
			color.clearColor[0] = 0.04f;
			color.clearColor[1] = 0.05f;
			color.clearColor[2] = 0.08f;
			color.clearColor[3] = 1.0f;
			commandList->BeginRendering({ &color, 1, nullptr });
			commandList->BindGraphicsPipeline(pipeline);
			commandList->BindResourceSet(resourceSet);
			commandList->SetViewport({
				0.0f, 0.0f,
				static_cast<float>(backBuffer->GetDesc().width),
				static_cast<float>(backBuffer->GetDesc().height), 0.0f, 1.0f
			});
			commandList->SetScissor({
				0, 0, backBuffer->GetDesc().width, backBuffer->GetDesc().height
			});
			commandList->DispatchMesh(1, 1, 1);
			commandList->EndRendering();
			const dy::RHI::ResourceBarrierDesc toPresent = {
				nullptr, backBuffer,
				dy::RHI::ResourceState::RenderTarget,
				dy::RHI::ResourceState::Present,
				{}
			};
			commandList->ResourceBarrier(&toPresent, 1);
			commandList->Close();
			dy::RHI::ICommandList* lists[] = { commandList };
			Require(device->Submit(lists, 1), "mesh frame 제출에 실패했습니다.");
			device->Present();
		}

		device->DestroyResourceSet(resourceSet);
		device->DestroyBuffer(indexBuffer);
		device->DestroyBuffer(vertexBuffer);
		device->DestroyPipeline(pipeline);
		device->DestroyShader(fragmentShader);
		device->DestroyShader(meshShader);
		return 0;
	}
	catch(const std::exception& exception)
	{
		std::cerr << exception.what() << '\n';
		return 1;
	}
}

// 필요한 공개 RHI 추가:
// - Feature::MeshShader, IDevice::Supports
// - ShaderStage::Mesh, ShaderStageFlags::Mesh
// - GraphicsPipelineDesc::meshShader (vertexShader와 둘 중 하나만 지정)
// - ICommandList::DispatchMesh
