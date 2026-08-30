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
	std::unique_ptr<dy::Platform::Window> window;
	std::unique_ptr<dy::RHI::IDevice> device;
	dy::Graphics::Renderer renderer;
	bool rendererInitialized = false;
	int result = 0;
	try
	{
		dy::Examples::LoadModelOptions options;
		std::string optionError;
		if(!dy::Examples::ParseFoxComparisonOptions(argc, argv, options, optionError))
		{
			std::cerr << optionError << '\n';
			return -1;
		}

		window = std::make_unique<dy::Platform::Window>(1280, 720, "ExistingFox");
		device.reset(dy::RHI::IDevice::Create(window->GetHandle()));
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

		std::chrono::steady_clock::time_point startTime;
		std::chrono::steady_clock::time_point previousFrameTime;
		bool clockStarted = false;
		while(window->IsRunning())
		{
			window->PollEvents();
			if(!window->IsRunning()) break;

			const auto now = std::chrono::steady_clock::now();
			float deltaSeconds = 0.0f;
			float elapsedSeconds = 0.0f;
			if(!clockStarted)
			{
				startTime = now;
				previousFrameTime = now;
				clockStarted = true;
			}
			else
			{
				deltaSeconds = std::chrono::duration<float>(now - previousFrameTime).count();
				elapsedSeconds = std::chrono::duration<float>(now - startTime).count();
				previousFrameTime = now;
			}
			demo.Update(deltaSeconds, elapsedSeconds);
			device->BeginFrame();
			renderer.Render(scene, device.get());
			device->Present();
			if(options.smokeSeconds > 0.0f && elapsedSeconds >= options.smokeSeconds) break;
		}

	}
	catch(const std::exception& exception)
	{
		std::cerr << exception.what() << '\n';
		result = -1;
	}

	if(rendererInitialized) renderer.Shutdown(device.get());
	device.reset();
	window.reset();
	return result;
}
