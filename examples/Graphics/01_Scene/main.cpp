#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>

#include "../../Common/ExampleWindow.h"
#include "Graphics/Camera.h"
#include "Graphics/Light.h"
#include "Graphics/Material.h"
#include "Graphics/Mesh.h"
#include "Graphics/Renderer.h"
#include "Graphics/Scene.h"
#include "Math/Math.h"

using namespace dy;

int main()
{
	try
	{
		ExampleWindow window(1280, 720, "Graphics 01 - Scene");
		auto renderer = Graphics::Renderer::Create(window.GetHandle());
		if(!renderer) throw std::runtime_error("Failed to create renderer");

		Graphics::Camera camera = {};
		camera.position = Math::float3(3.0f, 3.0f, 2.2f);
		camera.view = Math::LookAtRH(
			camera.position,
			Math::float3(0.0f, 0.0f, 0.0f),
			Math::float3(0.0f, 0.0f, 1.0f));
		camera.projection = Math::PerspectiveRH_ZO(1.0472f, 1280.0f / 720.0f, 0.1f, 100.0f);

		Graphics::Scene scene;
		const Graphics::MeshID mesh = scene.CreateMesh(Graphics::CreateCubeMesh());

		Graphics::MaterialDesc material = {};
		material.baseColor = Math::float4(0.85f, 0.30f, 0.18f, 1.0f);
		material.roughnessFactor = 0.45f;
		const Graphics::MaterialID materialId = scene.CreateMaterial(material);
		const Graphics::EntityID cube = scene.CreateEntity(mesh, materialId);

		Graphics::DirectionalLight light = {};
		light.direction = Math::float3(0.4f, 0.6f, 0.8f);
		light.castShadow = false;
		(void)scene.CreateDirectionalLight(light);

		const auto start = std::chrono::steady_clock::now();
		while(window.IsRunning())
		{
			window.PollEvents();
			const float seconds = std::chrono::duration<float>(
				std::chrono::steady_clock::now() - start).count();

			scene.GetTransform(cube).worldMatrix =
				Math::Translation(Math::float3(0.0f, 0.0f, 0.2f * std::sin(seconds))) *
				Math::RotationZ(seconds * 0.7f) *
				Math::RotationX(seconds * 0.4f);

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
