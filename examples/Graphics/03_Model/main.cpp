#include <iostream>
#include <stdexcept>

#include "../../Common/ExampleWindow.h"
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
		const char* modelPath = argc > 1
			? argv[1]
			: "Models/DamagedHelmet/glTF/DamagedHelmet.gltf";

		ExampleWindow window(1280, 720, "Graphics 03 - Model");
		auto renderer = Graphics::Renderer::Create(window.GetHandle());
		if(!renderer) throw std::runtime_error("Failed to create renderer");

		Graphics::Scene scene;
		Graphics::ModelSceneDesc model = {};
		model.path = modelPath;
		model.normalizedSize = 1.8f;
		if(!Graphics::AddModelToScene(scene, model))
		{
			std::cerr << "Failed to load model: " << modelPath << '\n';
			return 1;
		}

		Graphics::DirectionalLight light = {};
		light.direction = Math::float3(0.35f, 0.55f, 0.75f);
		light.intensity = 4.0f;
		light.castShadow = false;
		(void)scene.CreateDirectionalLight(light);

		Graphics::Camera camera = {};
		camera.position = Math::float3(3.0f, 3.0f, 2.0f);
		camera.view = Math::LookAtRH(
			camera.position,
			Math::float3(0.0f, 0.0f, 0.5f),
			Math::float3(0.0f, 0.0f, 1.0f));
		camera.projection = Math::PerspectiveRH_ZO(1.0f, 1280.0f / 720.0f, 0.05f, 100.0f);

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
