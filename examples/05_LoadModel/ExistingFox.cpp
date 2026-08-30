#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "FoxLightDemo.h"
#include "Graphics/Renderer.h"
#include "Graphics/Scene.h"
#include "LoadModelOptions.h"
#include "Platform/Window.h"
#include "RHI/IDevice.h"

#ifndef DY_SHADER_DIR
#define DY_SHADER_DIR "./Shaders"
#endif

namespace
{
	const char* ShaderExtension()
	{
#if defined(ENABLE_METAL)
		return ".metal";
#elif defined(ENABLE_VULKAN)
		return ".spv";
#elif defined(ENABLE_D3D12)
		return ".hlsl";
#else
		return ".glsl";
#endif
	}
}

int main(int argc, char** argv)
{
	std::unique_ptr<dy::RHI::IDevice> device;
	dy::Graphics::Renderer renderer;
	bool rendererInitialized = false;
	try
	{
		dy::Examples::LoadModelOptions options;
		std::string optionError;
		if(!dy::Examples::ParseLoadModelOptions(argc, argv, options, optionError))
		{
			std::cerr << optionError << '\n';
			return -1;
		}

		dy::Platform::Window window(1280, 720, "ExistingFox");
		device.reset(dy::RHI::IDevice::Create(window.GetHandle()));
		if(!device) throw std::runtime_error("Failed to create device.");

		const std::string extension = ShaderExtension();
		const std::string vertexShaderPath = std::string(DY_SHADER_DIR) + "/mesh_vs" + extension;
		const std::string pixelShaderPath = std::string(DY_SHADER_DIR) + "/mesh_ps" + extension;
		dy::Graphics::RendererDesc rendererDesc = {};
		rendererDesc.vertexShaderPath = vertexShaderPath.c_str();
		rendererDesc.pixelShaderPath = pixelShaderPath.c_str();

		if(!renderer.Initialize(device.get(), rendererDesc))
			throw std::runtime_error("Failed to initialize renderer.");
		rendererInitialized = true;
		dy::Graphics::Scene scene;
		dy::Examples::FoxLightDemo demo(scene, renderer);

		const auto startTime = std::chrono::steady_clock::now();
		auto previousFrameTime = startTime;
		while(window.IsRunning())
		{
			window.PollEvents();
			const auto now = std::chrono::steady_clock::now();
			const float deltaSeconds = std::chrono::duration<float>(now - previousFrameTime).count();
			const float elapsedSeconds = std::chrono::duration<float>(now - startTime).count();
			previousFrameTime = now;
			demo.Update(deltaSeconds, elapsedSeconds);
			device->BeginFrame();
			renderer.Render(scene, device.get());
			device->Present();
			if(options.smokeSeconds > 0.0f && elapsedSeconds >= options.smokeSeconds) break;
		}

		renderer.Shutdown(device.get());
		rendererInitialized = false;
		return 0;
	}
	catch(const std::exception& exception)
	{
		if(rendererInitialized) renderer.Shutdown(device.get());
		std::cerr << exception.what() << '\n';
		return -1;
	}
}
