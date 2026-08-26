// 05_LoadModel - load static glTF/FBX/OBJ models through the shared LoadModel API.
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "Platform/Window.h"
#include "RHI/IDevice.h"
#include "Graphics/Renderer.h"
#include "Graphics/Scene.h"
#include "Graphics/Mesh.h"
#include "Math/Math.h"
#include "LoadModelOptions.h"
#include "LoadModelPlayback.h"
#if defined(ENABLE_VULKAN)
#include "Backends/Vulkan/VulkanDevice.h"
#endif

#ifndef DY_SHADER_DIR
#define DY_SHADER_DIR "./Shaders"
#endif

using namespace dy;

static constexpr const char* kUsage =
	"Usage: LoadModel [model] [--clip=N] [--paused] [--timescale=F] [--loop=0|1] [--camera-distance=F] [--smoke-seconds=F]\n"
	"Default model: Models/SimpleSkin.gltf\n"
	"Example: LoadModel \"character.glb\" --clip=1 --timescale=0.8 --loop=1 --camera-distance=3\n";

static const char* ShaderExt()
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

int main(int argc, char** argv)
{
	try
	{
		Examples::LoadModelOptions options;
		std::string optionError;
		if(!Examples::ParseLoadModelOptions(argc, argv, options, optionError))
		{
			std::cerr << optionError << '\n';
			std::cerr << kUsage;
			return -1;
		}
		if(options.showHelp)
		{
			std::cout << kUsage;
			return 0;
		}
		Platform::Window window(1280, 720, "LoadModel");
		std::unique_ptr<RHI::IDevice> device(RHI::IDevice::Create(window.GetHandle()));
		if(!device) return -1;

		const std::filesystem::path executableDirectory =
			std::filesystem::absolute(argv[0]).parent_path();
		std::filesystem::path shaderDirectory = DY_SHADER_DIR;
		if(shaderDirectory.is_relative()) shaderDirectory = executableDirectory / shaderDirectory;
		const std::string ext = ShaderExt();
		const std::string vsPath = (shaderDirectory / ("mesh_vs" + ext)).lexically_normal().string();
		const std::string psPath = (shaderDirectory / ("mesh_ps" + ext)).lexically_normal().string();

		Graphics::Renderer renderer;
		Graphics::RendererDesc cfg = {};
		cfg.vertexShaderPath = vsPath.c_str();
		cfg.pixelShaderPath = psPath.c_str();
		if(!renderer.Initialize(device.get(), cfg)) return -1;

		Graphics::CameraDesc camera = {};
		camera.target = Math::float3(0.0f, 0.0f, 0.5f);
		const float cameraHeight = options.cameraDistance * (4.0f / 9.0f);
		camera.eye = Math::float3(camera.target.x + options.cameraDistance, camera.target.y, camera.target.z + cameraHeight);
		camera.aspect = 1280.0f / 720.0f;
		camera.nearPlane = 0.05f;
		camera.farPlane = 200.0f;
		renderer.SetCamera(camera);

		Graphics::Scene scene;

		std::vector<ModelInstanceID> modelInstances;
		auto addModel = [&scene, &modelInstances](const Graphics::ModelSceneDesc& desc)
		{
			ModelInstanceID instanceId = ModelInstanceID::Invalid;
			const bool loaded = Graphics::AddModelToScene(scene, desc, &instanceId);
			if(loaded && IsValid(instanceId)) modelInstances.push_back(instanceId);
			return loaded;
		};
		std::filesystem::path modelPath = options.modelPath;
		if(options.modelPath == Examples::kDefaultLoadModelPath)
			modelPath = executableDirectory / modelPath;
		modelPath = modelPath.lexically_normal();
		std::cout << "Loading model: " << modelPath.string() << '\n';
		Graphics::ModelSceneDesc desc = {};
		desc.path = modelPath.string();
		if(!addModel(desc))
		{
			std::cerr << "Failed to load model: " << modelPath.string() << '\n';
			return -1;
		}
		std::cout << "Playback settings: clip=" << options.clipIndex
			<< " timescale=" << options.timeScale
			<< " loop=" << (options.loop ? 1 : 0)
			<< " paused=" << (options.paused ? 1 : 0)
			<< " camera-distance=" << options.cameraDistance << '\n';
		Examples::LoadModelPlaybackResult playbackResult;
		std::string playbackError;
		if(!Examples::ConfigureLoadModelAnimations(
			scene,
			modelInstances,
			options,
			std::cout,
			playbackError,
			playbackResult))
		{
			std::cerr << playbackError << '\n';
			return -1;
		}
		std::cout << "Configured animated instances=" << playbackResult.animatedInstances
			<< " static instances=" << playbackResult.staticInstances << '\n';

		Graphics::DirectionalLight light = {};
		light.direction = Math::float3(0.4f, 0.5f, 0.8f);
		light.color = Math::float3(1.0f, 0.96f, 0.9f);
		light.intensity = 3.0f;
		light.castShadow = false;
		[[maybe_unused]] const DirectionalLightID lightId = scene.CreateDirectionalLight(light);

		const auto startTime = std::chrono::steady_clock::now();
		auto previousFrame = startTime;
		bool animationUpdateFailed = false;
		while(window.IsRunning())
		{
			window.PollEvents();
			const auto now = std::chrono::steady_clock::now();
			const float deltaSeconds = std::chrono::duration<float>(now - previousFrame).count();
			previousFrame = now;
			const float t = std::chrono::duration<float>(now - startTime).count();

			// 나열된 모델 줄을 중심으로 공전(반경은 줄 길이를 담을 정도).
			const float a = t * 0.4f;
			camera.eye = Math::float3(
				camera.target.x + options.cameraDistance * std::cos(a),
				camera.target.y + options.cameraDistance * std::sin(a),
				camera.target.z + cameraHeight);
			renderer.SetCamera(camera);
			const Graphics::AnimationUpdateReport animationReport = scene.UpdateAnimations(deltaSeconds);
			if(!animationReport.Succeeded())
			{
				std::cerr << "Animation update failed with " << animationReport.failures.size() << " error(s).\n";
				animationUpdateFailed = true;
				break;
			}

			device->BeginFrame();
			renderer.Render(scene, device.get());
			device->Present();
			if(options.smokeSeconds > 0.0f && t >= options.smokeSeconds) break;
		}

		renderer.Shutdown(device.get());
		#if defined(ENABLE_VULKAN)
		const auto* vulkanDevice = dynamic_cast<const Backends::VulkanDevice*>(device.get());
		const bool validationCaptureEnabled = vulkanDevice != nullptr && vulkanDevice->IsValidationCaptureEnabled();
		const uint32_t validationErrorCount = vulkanDevice != nullptr ? vulkanDevice->GetValidationErrorCount() : 0u;
		const uint32_t validationVuidCount = vulkanDevice != nullptr ? vulkanDevice->GetValidationVuidCount() : 0u;
		const bool deviceLost = vulkanDevice != nullptr && vulkanDevice->IsDeviceLost();
		std::cout << "VULKAN_VALIDATION_CAPTURE_ENABLED=" << (validationCaptureEnabled ? 1 : 0) << '\n';
		std::cout << "VULKAN_VALIDATION_ERROR_COUNT=" << validationErrorCount << '\n';
		std::cout << "VULKAN_VALIDATION_VUID_COUNT=" << validationVuidCount << '\n';
		std::cout << "VULKAN_DEVICE_LOST=" << (deviceLost ? 1 : 0) << '\n';
		#if !defined(NDEBUG)
		if(!validationCaptureEnabled) return -3;
		#endif
		if(validationErrorCount != 0u) return -2;
		if(validationVuidCount != 0u) return -4;
		if(deviceLost) return -4;
		#endif
		if(animationUpdateFailed) return -5;
		return 0;
	}
	catch(const std::exception& exception)
	{
		std::cerr << exception.what() << '\n';
		return -1;
	}
}
