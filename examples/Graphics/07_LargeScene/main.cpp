#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../../Common/ExampleWindow.h"
#include "Graphics/Camera.h"
#include "Graphics/Light.h"
#include "Graphics/Material.h"
#include "Graphics/Mesh.h"
#include "Graphics/Renderer.h"
#include "Graphics/Scene.h"
#include "Math/Math.h"

using namespace dy;

namespace
{
	uint32_t ParseEntityCount(int argc, char** argv)
	{
		constexpr uint32_t defaultCount = 2000;
		for(int i = 1; i < argc; ++i)
		{
			const std::string argument = argv[i] != nullptr ? argv[i] : "";
			constexpr const char* prefix = "--count=";
			if(argument.rfind(prefix, 0) != 0) continue;

			const unsigned long parsed = std::strtoul(argument.c_str() + 8, nullptr, 10);
			if(parsed > 0) return static_cast<uint32_t>(parsed);
		}
		return defaultCount;
	}

	struct MovingEntity
	{
		Graphics::EntityID id = Graphics::EntityID::Invalid;
		Math::float3 position = Math::float3(0.0f, 0.0f, 0.0f);
		float phase = 0.0f;
	};
}

int main(int argc, char** argv)
{
	try
	{
		const uint32_t entityCount = ParseEntityCount(argc, argv);
		ExampleWindow window(1280, 720, "Graphics 07 - Large Scene");

		Graphics::RendererDesc rendererDesc = {};
		rendererDesc.clearColor = Math::float4(0.02f, 0.03f, 0.05f, 1.0f);
		auto renderer = Graphics::Renderer::Create(window.GetHandle(), rendererDesc);
		if(!renderer) throw std::runtime_error("Failed to create renderer");

		Graphics::Scene scene;
		const Graphics::MeshID cube = scene.CreateMesh(Graphics::CreateCubeMesh(0.9f));

		const Math::float4 colors[] = {
			Math::float4(0.85f, 0.25f, 0.18f, 1.0f),
			Math::float4(0.18f, 0.55f, 0.90f, 1.0f),
			Math::float4(0.20f, 0.78f, 0.42f, 1.0f),
			Math::float4(0.90f, 0.68f, 0.15f, 1.0f),
		};
		Graphics::MaterialID materials[4] = {};
		for(uint32_t i = 0; i < 4; ++i)
		{
			Graphics::MaterialDesc material = {};
			material.baseColor = colors[i];
			material.roughnessFactor = 0.35f + 0.15f * static_cast<float>(i);
			materials[i] = scene.CreateMaterial(material);
		}

		const uint32_t side = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<double>(entityCount))));
		constexpr float spacing = 1.5f;
		const float origin = -0.5f * static_cast<float>(side - 1) * spacing;
		std::vector<MovingEntity> entities;
		entities.reserve(entityCount);

		for(uint32_t i = 0; i < entityCount; ++i)
		{
			const uint32_t x = i % side;
			const uint32_t y = i / side;
			const Math::float3 position(
				origin + static_cast<float>(x) * spacing,
				origin + static_cast<float>(y) * spacing,
				0.0f);
			const Graphics::EntityID id = scene.CreateEntity(
				cube,
				materials[i % 4],
				Math::Translation(position));
			entities.push_back({ id, position, static_cast<float>(i % 31) * 0.2f });
		}

		Graphics::DirectionalLight light = {};
		light.direction = Math::float3(0.45f, 0.55f, 0.75f);
		light.intensity = 3.5f;
		light.castShadow = false;
		(void)scene.CreateDirectionalLight(light);

		const float extent = static_cast<float>(side) * spacing;
		Graphics::Camera camera = {};
		camera.position = Math::float3(extent * 0.6f, -extent * 0.9f, extent * 0.7f);
		camera.view = Math::LookAtRH(
			camera.position,
			Math::float3(0.0f, 0.0f, 0.0f),
			Math::float3(0.0f, 0.0f, 1.0f));
		camera.projection = Math::PerspectiveRH_ZO(0.9f, 1280.0f / 720.0f, 0.1f, extent * 4.0f);

		const auto start = std::chrono::steady_clock::now();
		while(window.IsRunning())
		{
			window.PollEvents();
			const float seconds = std::chrono::duration<float>(
				std::chrono::steady_clock::now() - start).count();

			for(const MovingEntity& entity : entities)
			{
				scene.GetTransform(entity.id).worldMatrix =
					Math::Translation(entity.position) *
					Math::RotationZ(seconds * 0.5f + entity.phase);
			}

			renderer->Render(scene, camera);
		}
	}
	catch(const std::exception& exception)
	{
		std::cerr << exception.what() << '\n';
		return 1;
	}

	return 0;
}
