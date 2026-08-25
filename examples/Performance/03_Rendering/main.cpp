#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../../Common/ExampleWindow.h"
#include "Graphics/Camera.h"
#include "Graphics/Entity.h"
#include "Graphics/Light.h"
#include "Graphics/Material.h"
#include "Graphics/Mesh.h"
#include "Graphics/Renderer.h"
#include "Graphics/Scene.h"
#include "Math/Math.h"

using namespace dy;

namespace
{
	using Clock = std::chrono::steady_clock;

	struct Options
	{
		uint32_t drawCount = 1000u;
		uint32_t warmUpFrameCount = 60u;
		uint32_t measuredFrameCount = 300u;
		uint32_t width = 1280u;
		uint32_t height = 720u;
		bool vsync = false;
	};

	struct Statistics
	{
		double meanMilliseconds = 0.0;
		double minimumMilliseconds = 0.0;
		double p50Milliseconds = 0.0;
		double p95Milliseconds = 0.0;
		double maximumMilliseconds = 0.0;
	};

	uint64_t ParsePositiveArgument(int argc, char** argv, const char* name, uint64_t fallback)
	{
		const std::string prefix = std::string(name) + "=";
		for(int i = 1; i < argc; ++i)
		{
			const std::string argument = argv[i] != nullptr ? argv[i] : "";
			if(argument.rfind(prefix, 0) != 0) continue;

			char* end = nullptr;
			const unsigned long long value = std::strtoull(argument.c_str() + prefix.size(), &end, 10);
			if(end == argument.c_str() + prefix.size() || *end != '\0' || value == 0u)
				throw std::invalid_argument("Invalid positive integer for " + std::string(name));
			return static_cast<uint64_t>(value);
		}
		return fallback;
	}

	bool ParseBooleanArgument(int argc, char** argv, const char* name, bool fallback)
	{
		const std::string prefix = std::string(name) + "=";
		for(int i = 1; i < argc; ++i)
		{
			const std::string argument = argv[i] != nullptr ? argv[i] : "";
			if(argument == prefix + "0") return false;
			if(argument == prefix + "1") return true;
			if(argument.rfind(prefix, 0) == 0)
				throw std::invalid_argument("Expected 0 or 1 for " + std::string(name));
		}
		return fallback;
	}

	Options ParseOptions(int argc, char** argv)
	{
		Options options;
		options.drawCount = static_cast<uint32_t>(ParsePositiveArgument(argc, argv, "--draws", options.drawCount));
		options.warmUpFrameCount = static_cast<uint32_t>(ParsePositiveArgument(argc, argv, "--warmup", options.warmUpFrameCount));
		options.measuredFrameCount = static_cast<uint32_t>(ParsePositiveArgument(argc, argv, "--frames", options.measuredFrameCount));
		options.width = static_cast<uint32_t>(ParsePositiveArgument(argc, argv, "--width", options.width));
		options.height = static_cast<uint32_t>(ParsePositiveArgument(argc, argv, "--height", options.height));
		options.vsync = ParseBooleanArgument(argc, argv, "--vsync", options.vsync);
		return options;
	}

	const char* BuildConfiguration()
	{
#if defined(NDEBUG)
		return "Release (NDEBUG)";
#else
		return "Debug (assertions enabled)";
#endif
	}

	Statistics Summarize(std::vector<double> samples)
	{
		if(samples.empty()) throw std::runtime_error("No rendering samples were collected");

		double total = 0.0;
		for(double sample : samples) total += sample;
		std::sort(samples.begin(), samples.end());
		const size_t p50Index = static_cast<size_t>(0.50 * static_cast<double>(samples.size() - 1u));
		const size_t p95Index = static_cast<size_t>(0.95 * static_cast<double>(samples.size() - 1u));
		return Statistics{
			total / static_cast<double>(samples.size()),
			samples.front(),
			samples[p50Index],
			samples[p95Index],
			samples.back()
		};
	}

	void RenderFrames(
		ExampleWindow& window,
		Graphics::Renderer& renderer,
		const Graphics::Scene& scene,
		const Graphics::Camera& camera,
		uint32_t frameCount)
	{
		for(uint32_t frame = 0; frame < frameCount; ++frame)
		{
			if(!window.IsRunning()) throw std::runtime_error("Window closed during warm-up");
			window.PollEvents();
			renderer.Render(scene, camera);
		}
	}

	std::vector<double> MeasureRenderCalls(
		ExampleWindow& window,
		Graphics::Renderer& renderer,
		const Graphics::Scene& scene,
		const Graphics::Camera& camera,
		uint32_t frameCount)
	{
		std::vector<double> samples;
		samples.reserve(frameCount);
		for(uint32_t frame = 0; frame < frameCount; ++frame)
		{
			if(!window.IsRunning()) throw std::runtime_error("Window closed during measurement");
			window.PollEvents();

			const auto start = Clock::now();
			renderer.Render(scene, camera);
			const auto end = Clock::now();
			samples.push_back(std::chrono::duration<double, std::milli>(end - start).count());
		}
		return samples;
	}
}

int main(int argc, char** argv)
{
	try
	{
		const Options options = ParseOptions(argc, argv);
		ExampleWindow window(options.width, options.height, "Rendering performance");

		Graphics::RendererDesc rendererDesc = {};
		rendererDesc.vsync = options.vsync;
		rendererDesc.enableShadows = false;
		auto renderer = Graphics::Renderer::Create(window.GetHandle(), rendererDesc);
		if(!renderer) throw std::runtime_error("Failed to create Renderer");

		Graphics::Scene scene;
		const Graphics::MeshID mesh = scene.CreateMesh(Graphics::CreateCubeMesh(1.0f));
		Graphics::MaterialDesc materialDesc = {};
		materialDesc.baseColor = Math::float4(0.75f, 0.42f, 0.20f, 1.0f);
		materialDesc.roughnessFactor = 0.6f;
		const Graphics::MaterialID material = scene.CreateMaterial(materialDesc);

		const uint32_t side = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<double>(options.drawCount))));
		const float spacing = 1.4f;
		const float origin = -0.5f * static_cast<float>(side - 1u) * spacing;
		for(uint32_t index = 0; index < options.drawCount; ++index)
		{
			const uint32_t x = index % side;
			const uint32_t y = index / side;
			const Math::float3 position(
				origin + static_cast<float>(x) * spacing,
				origin + static_cast<float>(y) * spacing,
				0.0f);
			(void)scene.CreateEntity(mesh, material, Math::Translation(position));
		}

		Graphics::DirectionalLight light = {};
		light.direction = Math::float3(0.4f, 0.5f, 0.8f);
		light.intensity = 3.0f;
		light.castShadow = false;
		(void)scene.CreateDirectionalLight(light);

		const float sceneExtent = static_cast<float>(side) * spacing;
		Graphics::Camera camera = {};
		camera.position = Math::float3(0.0f, -sceneExtent * 0.9f, sceneExtent * 0.9f);
		camera.view = Math::LookAtRH(
			camera.position,
			Math::float3(0.0f, 0.0f, 0.0f),
			Math::float3(0.0f, 0.0f, 1.0f));
		camera.projection = Math::PerspectiveRH_ZO(
			1.0472f,
			static_cast<float>(window.GetWidth()) / static_cast<float>(window.GetHeight()),
			0.1f,
			sceneExtent * 4.0f + 10.0f);

		std::cout << "Rendering performance\n"
		          << "  build       : " << BuildConfiguration() << '\n'
		          << "  input       : " << options.drawCount << " entities/draws, one shared mesh and material\n"
		          << "  output      : " << window.GetWidth() << 'x' << window.GetHeight()
		          << ", vsync=" << (options.vsync ? "on" : "off") << '\n'
		          << "  warm-up     : " << options.warmUpFrameCount << " frames\n"
		          << "  repeats     : " << options.measuredFrameCount << " frames\n"
		          << "  units       : ms per Renderer::Render call attempt, us per requested draw\n"
		          << "  scope       : CPU wall time; Render returns no success flag, so an early-returned frame can be sampled\n\n";

		RenderFrames(window, *renderer, scene, camera, options.warmUpFrameCount);
		const Statistics statistics = Summarize(MeasureRenderCalls(
			window, *renderer, scene, camera, options.measuredFrameCount));

		std::cout << std::fixed << std::setprecision(3)
		          << "Renderer::Render call-attempt CPU wall time\n"
		          << "  mean        : " << statistics.meanMilliseconds << " ms\n"
		          << "  min         : " << statistics.minimumMilliseconds << " ms\n"
		          << "  p50         : " << statistics.p50Milliseconds << " ms\n"
		          << "  p95         : " << statistics.p95Milliseconds << " ms\n"
		          << "  max         : " << statistics.maximumMilliseconds << " ms\n"
		          << "  mean/draw   : " << statistics.meanMilliseconds * 1000.0 / options.drawCount << " us\n"
		          << "GPU frame time             unavailable: no public timestamp-query API\n"
		          << "parallel/graph comparison  unavailable: no public recording or graph API\n";

		return 0;
	}
	catch(const std::exception& exception)
	{
		std::cerr << exception.what() << '\n';
		return 1;
	}
}
