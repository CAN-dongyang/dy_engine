// 06_ShadowCube — directional shadow: a cube casts onto a floor.
#include <chrono>
#include <cmath>
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
		Platform::Window window(1280, 720, "ShadowCube");
		Graphics::RendererDesc cfg = {};
		cfg.enableShadows = true;
		auto renderer = Graphics::Renderer::Create(window.GetHandle(), cfg);
		if(!renderer) return -1;

		const Math::float3 cameraTarget(0.0f, 0.0f, 0.5f);
		Graphics::Camera camera = {};
		camera.position = Math::float3(5.0f, 5.0f, 4.0f);
		camera.view = Math::LookAtRH(camera.position, cameraTarget, Math::float3(0.0f, 0.0f, 1.0f));
		camera.projection = Math::PerspectiveRH_ZO(1.0472f, 1280.0f / 720.0f, 0.1f, 100.0f);

		Graphics::Scene scene;
		const MeshID cube = scene.CreateMesh(Graphics::CreateCubeMesh(1.0f));

		Graphics::MaterialDesc floorMat = {};
		floorMat.baseColor = Math::float4(0.6f, 0.6f, 0.62f, 1.0f);
		const MaterialID floorMatId = scene.CreateMaterial(floorMat);

		Graphics::MaterialDesc cubeMat = {};
		cubeMat.baseColor = Math::float4(0.85f, 0.35f, 0.25f, 1.0f);
		const MaterialID cubeMatId = scene.CreateMaterial(cubeMat);

		// 바닥(넓고 얇은 큐브, 그림자 수신) + 떠 있는 큐브(그림자 생성).
		[[maybe_unused]] const EntityID floorEntity = scene.CreateEntity(
			cube, floorMatId, Math::Translation(Math::float3(0.0f, 0.0f, -1.0f)) * Math::Scaling(Math::float3(8.0f, 8.0f, 0.2f)));
		[[maybe_unused]] const EntityID cubeEntity = scene.CreateEntity(
			cube, cubeMatId, Math::Translation(Math::float3(0.0f, 0.0f, 0.5f)));

		Graphics::DirectionalLight light = {};
		light.direction = Math::float3(0.5f, 0.4f, 0.75f);
		light.color = Math::float3(1.0f, 0.96f, 0.9f);
		light.intensity = 4.0f;
		[[maybe_unused]] const DirectionalLightID lightId = scene.CreateDirectionalLight(light);

		const auto startTime = std::chrono::steady_clock::now();
		while(window.IsRunning())
		{
			window.PollEvents();
			const float t = std::chrono::duration<float>(std::chrono::steady_clock::now() - startTime).count();
			const float a = t * 0.4f;

			// 타깃 중심 공전 카메라.
			camera.position = Math::float3(cameraTarget.x + 7.0f * std::cos(a), cameraTarget.y + 7.0f * std::sin(a), cameraTarget.z + 3.5f);
			camera.view = Math::LookAtRH(camera.position, cameraTarget, Math::float3(0.0f, 0.0f, 1.0f));

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
