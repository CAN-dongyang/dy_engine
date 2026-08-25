// 목표 API: model instance와 animation sampling이 공개되면 빌드할 예제다.
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>

#include "../../Common/ExampleWindow.h"
#include "Graphics/Animation.h"
#include "Graphics/Camera.h"
#include "Graphics/Light.h"
#include "Graphics/Model.h"
#include "Graphics/Renderer.h"
#include "Graphics/Scene.h"
#include "Math/Math.h"

using namespace dy;

int main()
{
	try
	{
		ExampleWindow window(1280, 720, "Graphics 05 - Animation");
		auto renderer = Graphics::Renderer::Create(window.GetHandle());
		if(!renderer) throw std::runtime_error("Failed to create renderer");

		Graphics::Scene scene;
		Graphics::ModelSceneDesc model = {};
		model.path = "Models/Character.glb";
		const Graphics::ModelInstanceID character = Graphics::CreateModelInstance(scene, model);
		if(!Graphics::IsValid(character)) throw std::runtime_error("Failed to load animated model");

		Graphics::DirectionalLight light = {};
		light.castShadow = false;
		(void)scene.CreateDirectionalLight(light);

		Graphics::Camera camera = {};
		camera.position = Math::float3(3.0f, 3.0f, 2.0f);
		camera.view = Math::LookAtRH(
			camera.position,
			Math::float3(0.0f, 0.0f, 0.8f),
			Math::float3(0.0f, 0.0f, 1.0f));
		camera.projection = Math::PerspectiveRH_ZO(1.0f, 1280.0f / 720.0f, 0.05f, 100.0f);

		const auto start = std::chrono::steady_clock::now();
		while(window.IsRunning())
		{
			window.PollEvents();
			const float seconds = std::chrono::duration<float>(
				std::chrono::steady_clock::now() - start).count();

			scene.SetAnimationTime(character, "Walk", seconds, true);
			scene.SetMorphWeight(character, "Smile", 0.5f + 0.5f * std::sin(seconds));
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
