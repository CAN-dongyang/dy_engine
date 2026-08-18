// 05_LoadModel - load static glTF/FBX/OBJ models through the shared LoadModel API.
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "Platform/Window.h"
#include "Graphics/Camera.h"
#include "Graphics/Light.h"
#include "Graphics/Model.h"
#include "Graphics/Renderer.h"
#include "Graphics/Scene.h"
#include "Math/Math.h"

using namespace dy;

int main(int argc, char** argv)
{
	try
	{
		Platform::Window window(1280, 720, "LoadModel");
		Graphics::RendererDesc cfg = {};
		auto renderer = Graphics::Renderer::Create(window.GetHandle(), cfg);
		if(!renderer) return -1;

		const Math::float3 cameraTarget(0.0f, 0.0f, 0.5f);
		Graphics::Camera camera = {};
		camera.position = Math::float3(3.0f, 3.0f, 2.0f);
		camera.view = Math::LookAtRH(camera.position, cameraTarget, Math::float3(0.0f, 0.0f, 1.0f));
		camera.projection = Math::PerspectiveRH_ZO(1.0472f, 1280.0f / 720.0f, 0.05f, 200.0f);

		Graphics::Scene scene;

		bool loadedAny = false;
		if(argc > 1)
		{
			loadedAny = Graphics::AddModelToScene(scene, argv[1]);
		}
		else
		{
			const std::vector<const char*> models = {
				"Models/Duck/glTF/Duck.gltf",
				"Models/Avocado/glTF/Avocado.gltf",
				"Models/BoomBox/glTF/BoomBox.gltf",
				"Models/DamagedHelmet/glTF/DamagedHelmet.gltf",
				"Models/WaterBottle/glTF/WaterBottle.gltf",
				"Models/Lowpoly_tree/Lowpoly_tree.obj",
				"Models/shiba/scene.FBX",
			};
			const float spacing = 2.3f;
			const int columnCount = 4;
			for(size_t i = 0; i < models.size(); ++i)
			{
				const int column = static_cast<int>(i % columnCount);
				const int row = static_cast<int>(i / columnCount);
				const float x = (static_cast<float>(column) - 0.5f * static_cast<float>(columnCount - 1)) * spacing;
				const float y = (0.5f - static_cast<float>(row)) * spacing;
				loadedAny |= Graphics::AddModelToScene(scene, models[i], Math::float3(x, y, 0.5f));
			}
		}
		if(!loadedAny) return -1;

		Graphics::DirectionalLight light = {};
		light.direction = Math::float3(0.4f, 0.5f, 0.8f);
		light.color = Math::float3(1.0f, 0.96f, 0.9f);
		light.intensity = 3.0f;
		light.castShadow = false;
		[[maybe_unused]] const Graphics::DirectionalLightID lightId = scene.CreateDirectionalLight(light);

		const auto startTime = std::chrono::steady_clock::now();
		while(window.IsRunning())
		{
			window.PollEvents();
			const float t = std::chrono::duration<float>(std::chrono::steady_clock::now() - startTime).count();

			// 나열된 모델 줄을 중심으로 공전(반경은 줄 길이를 담을 정도).
			const float a = t * 0.4f;
			camera.position = Math::float3(cameraTarget.x + 9.0f * std::cos(a), cameraTarget.y + 9.0f * std::sin(a), cameraTarget.z + 4.0f);
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
