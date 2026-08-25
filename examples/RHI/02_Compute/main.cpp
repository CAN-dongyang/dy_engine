#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

#include "RHI/Binding.h"
#include "RHI/Buffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/Pipeline.h"
#include "RHI/ResourceSet.h"
#include "RHI/Shader.h"

// 이 예제는 아직 없는 Compute 계약을 먼저 고정한다.
// 필요한 추가 API는 파일 끝의 목록과 실제 호출부에만 나타낸다.
// Blocking ReadBuffer를 편의 API로 둘지, readback buffer와 fence를 사용자가
// 명시할지는 이 흐름을 검토한 뒤 결정해야 한다.

namespace
{
	using namespace dy;

	constexpr uint32_t ElementCount = 1024;
	constexpr uint32_t ThreadsPerGroup = 64;

#if defined(ENABLE_D3D12)
	constexpr const char* ComputeShaderPath = "Shaders/D3D12/FillSquares.cs.dxil";
#elif defined(ENABLE_VULKAN)
	constexpr const char* ComputeShaderPath = "Shaders/Vulkan/FillSquares.comp.spv";
#elif defined(ENABLE_METAL)
	constexpr const char* ComputeShaderPath = "Shaders/Metal/Compute.metallib";
#else
	constexpr const char* ComputeShaderPath = "Shaders/Null/FillSquares.cs.bin";
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
}

int main()
{
	try
	{
		// Compute-only 사용은 surface나 native window를 요구하지 않는다.
		std::unique_ptr<dy::RHI::IDevice> device(dy::RHI::IDevice::Create());
		Require(device != nullptr, "RHI device 생성에 실패했습니다.");

		const std::vector<std::byte> binary = ReadBinary(ComputeShaderPath);
		dy::RHI::ShaderHandle shader = device->CreateShader({
			dy::RHI::ShaderStage::Compute,
#if defined(ENABLE_METAL)
			"fillSquares",
#else
			"main",
#endif
			binary.data(),
			binary.size()
		});
		Require(shader != nullptr, "compute shader 생성에 실패했습니다.");

		const dy::RHI::ResourceBindingLayout resultLayout = {
			0,
			dy::RHI::ResourceBindingType::ReadWriteStorageBuffer,
			1,
			dy::RHI::ShaderStageFlags::Compute,
			{}
		};
		dy::RHI::ComputePipelineDesc pipelineDesc = {};
		pipelineDesc.computeShader = shader;
		pipelineDesc.layout = { &resultLayout, 1, 0, dy::RHI::ShaderStageFlags::None, 0 };
		dy::RHI::PipelineHandle pipeline = device->CreateComputePipeline(pipelineDesc);
		Require(pipeline != nullptr, "compute pipeline 생성에 실패했습니다.");

		dy::RHI::BufferHandle resultBuffer = device->CreateBuffer({
			ElementCount * static_cast<uint32_t>(sizeof(uint32_t)),
			static_cast<uint32_t>(sizeof(uint32_t)),
			dy::RHI::BufferUsage::Storage,
			dy::RHI::ResourceState::UnorderedAccess
		});
		Require(resultBuffer != nullptr, "결과 buffer 생성에 실패했습니다.");

		const dy::RHI::ResourceBinding resultBinding = {
			0, 0, resultBuffer, nullptr, 0,
			ElementCount * static_cast<uint32_t>(sizeof(uint32_t)), {}
		};
		dy::RHI::ResourceSetHandle resources = device->CreateResourceSet({
			pipeline, &resultBinding, 1
		});
		Require(resources != nullptr, "compute resource set 생성에 실패했습니다.");

		dy::RHI::ICommandList* commandList = device->AcquireCommandList();
		Require(commandList != nullptr, "명령 목록을 얻지 못했습니다.");
		commandList->BindComputePipeline(pipeline);
		commandList->BindResourceSet(resources);
		commandList->Dispatch((ElementCount + ThreadsPerGroup - 1) / ThreadsPerGroup, 1, 1);
		const dy::RHI::ResourceBarrierDesc beforeReadback = {
			resultBuffer, nullptr,
			dy::RHI::ResourceState::UnorderedAccess,
			dy::RHI::ResourceState::CopySource,
			{}
		};
		commandList->ResourceBarrier(&beforeReadback, 1);
		commandList->Close();
		dy::RHI::ICommandList* lists[] = { commandList };
		Require(device->Submit(lists, 1), "compute 제출에 실패했습니다.");

		// ReadBuffer는 이 최소 예제에서는 완료를 기다리는 blocking readback이다.
		std::array<uint32_t, ElementCount> result = {};
		Require(device->ReadBuffer(
			resultBuffer, 0, result.data(), static_cast<uint32_t>(sizeof(result))),
			"결과 readback에 실패했습니다.");
		for(uint32_t index = 0; index < ElementCount; ++index)
			Require(result[index] == index * index, "compute 결과가 예상과 다릅니다.");

		device->DestroyResourceSet(resources);
		device->DestroyBuffer(resultBuffer);
		device->DestroyPipeline(pipeline);
		device->DestroyShader(shader);
		std::cout << "compute readback 검증 성공\n";
		return 0;
	}
	catch(const std::exception& exception)
	{
		std::cerr << exception.what() << '\n';
		return 1;
	}
}

// 필요한 공개 RHI 추가:
// - window 없이 IDevice::Create() 가능한 headless device 생성
// - ShaderStage::Compute, ShaderStageFlags::Compute
// - ComputePipelineDesc, IDevice::CreateComputePipeline
// - ICommandList::BindComputePipeline, Dispatch
// - ResourceState::CopySource, IDevice::ReadBuffer(blocking)
// - Storage buffer의 CopySource usage 표현 또는 ReadBuffer가 이를 내부 처리한다는 계약
