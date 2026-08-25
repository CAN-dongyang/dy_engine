#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "Graphics/Entity.h"
#include "Graphics/Material.h"
#include "Graphics/Mesh.h"
#include "Graphics/Scene.h"
#include "Math/Math.h"

using namespace dy;

namespace
{
	using Clock = std::chrono::steady_clock;

	struct Options
	{
		uint32_t entityCount = 100000u;
		uint32_t warmUpCount = 3u;
		uint32_t repeatCount = 10u;
	};

	struct Statistics
	{
		double meanMilliseconds = 0.0;
		double minimumMilliseconds = 0.0;
		double p95Milliseconds = 0.0;
	};

	volatile uint64_t g_integerSink = 0u;
	volatile float g_floatSink = 0.0f;

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

	Options ParseOptions(int argc, char** argv)
	{
		Options options;
		options.entityCount = static_cast<uint32_t>(ParsePositiveArgument(argc, argv, "--count", options.entityCount));
		options.warmUpCount = static_cast<uint32_t>(ParsePositiveArgument(argc, argv, "--warmup", options.warmUpCount));
		options.repeatCount = static_cast<uint32_t>(ParsePositiveArgument(argc, argv, "--repeat", options.repeatCount));
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
		double total = 0.0;
		for(double sample : samples) total += sample;
		std::sort(samples.begin(), samples.end());
		const size_t p95Index = static_cast<size_t>(0.95 * static_cast<double>(samples.size() - 1u));
		return Statistics{ total / static_cast<double>(samples.size()), samples.front(), samples[p95Index] };
	}

	void Consume(uint64_t value)
	{
		g_integerSink = g_integerSink + value;
		std::atomic_signal_fence(std::memory_order_seq_cst);
	}

	void Consume(float value)
	{
		g_floatSink = g_floatSink + value;
		std::atomic_signal_fence(std::memory_order_seq_cst);
	}

	std::vector<double> MeasureCreation(
		const Graphics::MeshData& mesh,
		const Graphics::MaterialDesc& material,
		const Options& options)
	{
		std::vector<double> samples;
		samples.reserve(options.repeatCount);
		const uint32_t totalRuns = options.warmUpCount + options.repeatCount;

		for(uint32_t run = 0; run < totalRuns; ++run)
		{
			Graphics::Scene scene;
			const auto start = Clock::now();
			const Graphics::MeshID meshId = scene.CreateMesh(mesh);
			const Graphics::MaterialID materialId = scene.CreateMaterial(material);
			for(uint32_t entity = 0; entity < options.entityCount; ++entity)
				(void)scene.CreateEntity(meshId, materialId);
			const auto end = Clock::now();

			Consume(static_cast<uint64_t>(scene.GetEntityCount()));
			if(run >= options.warmUpCount)
				samples.push_back(std::chrono::duration<double, std::milli>(end - start).count());
		}
		return samples;
	}

	float TraverseScene(const Graphics::Scene& scene, const std::vector<Graphics::EntityID>& entities)
	{
		float checksum = 0.0f;
		for(Graphics::EntityID entity : entities)
		{
			checksum += static_cast<float>(Graphics::ToIndex(scene.GetEntityMesh(entity)));
			checksum += static_cast<float>(Graphics::ToIndex(scene.GetEntityMaterial(entity)));
			checksum += scene.GetTransform(entity).worldMatrix.m[12];
			checksum += scene.GetRenderFlags(entity).castShadow ? 1.0f : 0.0f;
		}
		return checksum;
	}

	std::vector<double> MeasureTraversal(
		const Graphics::Scene& scene,
		const std::vector<Graphics::EntityID>& entities,
		const Options& options)
	{
		std::vector<double> samples;
		samples.reserve(options.repeatCount);
		const uint32_t totalRuns = options.warmUpCount + options.repeatCount;

		for(uint32_t run = 0; run < totalRuns; ++run)
		{
			const auto start = Clock::now();
			const float checksum = TraverseScene(scene, entities);
			const auto end = Clock::now();
			Consume(checksum);
			if(run >= options.warmUpCount)
				samples.push_back(std::chrono::duration<double, std::milli>(end - start).count());
		}
		return samples;
	}

	std::vector<double> MeasureTransformUpdates(
		Graphics::Scene& scene,
		const std::vector<Graphics::EntityID>& entities,
		const std::vector<Math::float4x4>& transforms,
		const Options& options)
	{
		std::vector<double> samples;
		samples.reserve(options.repeatCount);
		std::vector<Math::float4x4> alternateTransforms = transforms;
		for(Math::float4x4& transform : alternateTransforms) transform.m[12] += 0.5f;
		const uint32_t totalRuns = options.warmUpCount + options.repeatCount;

		for(uint32_t run = 0; run < totalRuns; ++run)
		{
			const std::vector<Math::float4x4>& source = (run % 2u) == 0u ? transforms : alternateTransforms;
			const auto start = Clock::now();
			for(size_t index = 0; index < entities.size(); ++index)
				scene.GetTransform(entities[index]).worldMatrix = source[index];
			const auto end = Clock::now();

			float checksum = 0.0f;
			for(Graphics::EntityID entity : entities)
				checksum += scene.GetTransform(entity).worldMatrix.m[12];
			Consume(checksum);
			if(run >= options.warmUpCount)
				samples.push_back(std::chrono::duration<double, std::milli>(end - start).count());
		}
		return samples;
	}

	void PrintStatistics(const char* name, const Statistics& statistics, uint32_t entityCount)
	{
		const double nanosecondsPerEntity = statistics.meanMilliseconds * 1000000.0 / entityCount;
		std::cout << std::left << std::setw(28) << name
		          << " mean=" << std::right << std::setw(9) << statistics.meanMilliseconds << " ms"
		          << ", min=" << std::setw(9) << statistics.minimumMilliseconds << " ms"
		          << ", p95=" << std::setw(9) << statistics.p95Milliseconds << " ms"
		          << ", " << std::setw(9) << nanosecondsPerEntity << " ns/entity\n";
	}
}

int main(int argc, char** argv)
{
	try
	{
		const Options options = ParseOptions(argc, argv);
		const Graphics::MeshData cubeMesh = Graphics::CreateCubeMesh(1.0f);
		Graphics::MaterialDesc material = {};
		material.baseColor = Math::float4(0.7f, 0.4f, 0.2f, 1.0f);

		std::cout << "Scene performance\n"
		          << "  build       : " << BuildConfiguration() << '\n'
		          << "  input       : " << options.entityCount << " entities, one shared mesh and material\n"
		          << "  warm-up     : " << options.warmUpCount << " runs per measurement\n"
		          << "  repeats     : " << options.repeatCount << " runs per measurement\n"
		          << "  units       : ms per run, ns per entity\n\n";

		const Statistics creation = Summarize(MeasureCreation(cubeMesh, material, options));

		Graphics::Scene scene;
		const Graphics::MeshID meshId = scene.CreateMesh(cubeMesh);
		const Graphics::MaterialID materialId = scene.CreateMaterial(material);
		std::vector<Graphics::EntityID> entities;
		entities.reserve(options.entityCount);
		std::vector<Math::float4x4> transforms(options.entityCount);
		for(uint32_t index = 0; index < options.entityCount; ++index)
		{
			entities.push_back(scene.CreateEntity(meshId, materialId));
			transforms[index] = Math::Translation(Math::float3(
				static_cast<float>(index % 1000u),
				static_cast<float>((index / 1000u) % 1000u),
				static_cast<float>(index / 1000000u)));
		}

		const Statistics traversal = Summarize(MeasureTraversal(scene, entities, options));
		const Statistics transformUpdates = Summarize(
			MeasureTransformUpdates(scene, entities, transforms, options));

		std::cout << std::fixed << std::setprecision(3);
		PrintStatistics("create Scene contents", creation, options.entityCount);
		PrintStatistics("traverse public entity data", traversal, options.entityCount);
		PrintStatistics("assign precomputed transforms", transformUpdates, options.entityCount);
		std::cout << "memory usage                unavailable: Scene exposes no public memory statistics\n";

		return 0;
	}
	catch(const std::exception& exception)
	{
		std::cerr << exception.what() << '\n';
		return 1;
	}
}
