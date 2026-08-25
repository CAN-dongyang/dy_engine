#include <iostream>
#include <stdexcept>

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

// 목표 동작: 공개된 모든 광원이 같은 frame에 기여하며 point light가 있어도
// directional shadow가 유지된다. 현재 Renderer는 아직 이 결과를 만들지 못한다.

int main()
{
	try
	{
		ExampleWindow window(1280, 720, "Graphics 04 - Lighting");

		Graphics::RendererDesc rendererDesc = {};
		rendererDesc.enableShadows = true;
		auto renderer = Graphics::Renderer::Create(window.GetHandle(), rendererDesc);
		if(!renderer) throw std::runtime_error("Failed to create renderer");

		Graphics::Camera camera = {};
		camera.position = Math::float3(7.0f, -8.0f, 5.0f);
		camera.view = Math::LookAtRH(
			camera.position,
			Math::float3(0.0f, 0.0f, 0.5f),
			Math::float3(0.0f, 0.0f, 1.0f));
		camera.projection = Math::PerspectiveRH_ZO(0.9f, 1280.0f / 720.0f, 0.1f, 100.0f);

		Graphics::Scene scene;
		const Graphics::MeshID cube = scene.CreateMesh(Graphics::CreateCubeMesh());

		Graphics::MaterialDesc floorMaterial = {};
		floorMaterial.baseColor = Math::float4(0.55f, 0.57f, 0.60f, 1.0f);
		floorMaterial.roughnessFactor = 0.8f;
		const Graphics::MaterialID floorMaterialId = scene.CreateMaterial(floorMaterial);

		Graphics::MaterialDesc objectMaterial = {};
		objectMaterial.baseColor = Math::float4(0.75f, 0.72f, 0.66f, 1.0f);
		objectMaterial.roughnessFactor = 0.35f;
		const Graphics::MaterialID objectMaterialId = scene.CreateMaterial(objectMaterial);

		Graphics::RenderFlags floorFlags = {};
		floorFlags.castShadow = false;
		floorFlags.receiveShadow = true;
		(void)scene.CreateEntity(
			cube,
			floorMaterialId,
			Math::Translation(Math::float3(0.0f, 0.0f, -0.7f)) *
				Math::Scaling(Math::float3(10.0f, 8.0f, 0.2f)),
			floorFlags);

		Graphics::RenderFlags objectFlags = {};
		objectFlags.castShadow = true;
		objectFlags.receiveShadow = true;
		(void)scene.CreateEntity(
			cube,
			objectMaterialId,
			Math::Translation(Math::float3(-1.8f, 0.0f, 0.0f)),
			objectFlags);
		(void)scene.CreateEntity(
			cube,
			objectMaterialId,
			Math::Translation(Math::float3(1.8f, 0.0f, 0.8f)),
			objectFlags);

		// Contract: every light contributes, and the shadow-casting sun remains active.
		Graphics::DirectionalLight sun = {};
		sun.direction = Math::float3(0.5f, 0.4f, 0.75f);
		sun.color = Math::float3(1.0f, 0.92f, 0.78f);
		sun.intensity = 3.0f;
		sun.castShadow = true;
		(void)scene.CreateDirectionalLight(sun);

		Graphics::PointLight redLight = {};
		redLight.position = Math::float3(-3.0f, -1.5f, 2.0f);
		redLight.color = Math::float3(1.0f, 0.12f, 0.05f);
		redLight.intensity = 18.0f;
		redLight.range = 7.0f;
		(void)scene.CreatePointLight(redLight);

		Graphics::PointLight blueLight = {};
		blueLight.position = Math::float3(3.0f, 1.5f, 2.0f);
		blueLight.color = Math::float3(0.08f, 0.25f, 1.0f);
		blueLight.intensity = 18.0f;
		blueLight.range = 7.0f;
		(void)scene.CreatePointLight(blueLight);

		while(window.IsRunning())
		{
			window.PollEvents();
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
