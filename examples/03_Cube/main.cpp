// 03_Cube — CreateCubeMesh + perspective camera + directional light.
#include <iostream>
#include <memory>
#include <stdexcept>

#include "Platform/Window.h"
#include "Graphics/Renderer.h"
#include "Graphics/Scene.h"
#include "Graphics/Mesh.h"
#include "Math/Math.h"

using namespace dy;

int main()
{
	try
	{
		Platform::Window window(1280, 720, "Cube");
		Graphics::RendererDesc cfg = {};
		auto renderer = Graphics::Renderer::Create(window.GetHandle(), cfg);
		if(!renderer) return -1;

		Graphics::Camera camera = {};
		camera.position = Math::float3(2.5f, 2.5f, 2.5f);
		camera.view = Math::LookAtRH(camera.position, Math::float3(0.0f, 0.0f, 0.0f), Math::float3(0.0f, 0.0f, 1.0f));
		camera.projection = Math::PerspectiveRH_ZO(1.0472f, 1280.0f / 720.0f, 0.1f, 100.0f);

		Graphics::Scene scene;
		const MeshID cube = scene.CreateMesh(Graphics::CreateCubeMesh(1.0f));
		Graphics::MaterialDesc material = {};
		material.baseColor = Math::float4(0.85f, 0.35f, 0.25f, 1.0f);
		material.roughnessFactor = 0.5f;
		const MaterialID materialId = scene.CreateMaterial(material);
		[[maybe_unused]] const EntityID entity = scene.CreateEntity(cube, materialId);

		Graphics::DirectionalLight light = {};
		light.direction = Math::float3(0.4f, 0.5f, 0.8f);
		light.color = Math::float3(1.0f, 0.96f, 0.9f);
		light.intensity = 3.0f;
		light.castShadow = false;
		[[maybe_unused]] const DirectionalLightID lightId = scene.CreateDirectionalLight(light);

		while(window.IsRunning())
		{
			window.PollEvents();
			renderer->Render(scene, camera);
		}

		return 0;
	}
	catch(const std::exception& exception)
	{
		std::cerr << exception.what() << '\n';
		return -1;
	}
}
