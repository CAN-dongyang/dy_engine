#include <cstdint>
#include <iostream>
#include <stdexcept>

#include "../../Common/ExampleWindow.h"
#include "Graphics/Camera.h"
#include "Graphics/Light.h"
#include "Graphics/Material.h"
#include "Graphics/Mesh.h"
#include "Graphics/Renderer.h"
#include "Graphics/Scene.h"
#include "Graphics/Texture.h"
#include "Math/Math.h"

using namespace dy;

// 목표 API: 색 texture는 sRGB, 데이터 texture는 linear로 구분한다.
// 현재 TextureAsset에는 이 구분이 없다.

namespace
{
	Graphics::TextureAsset MakeSolidTexture(
		uint8_t r,
		uint8_t g,
		uint8_t b,
		Graphics::TextureColorSpace colorSpace,
		uint8_t a = 255u)
	{
		Graphics::TextureAsset texture = {};
		texture.width = 1;
		texture.height = 1;
		texture.colorSpace = colorSpace;
		texture.rgba8 = { r, g, b, a };
		return texture;
	}
}

int main()
{
	try
	{
		ExampleWindow window(1280, 720, "Graphics 02 - Materials");
		auto renderer = Graphics::Renderer::Create(window.GetHandle());
		if(!renderer) throw std::runtime_error("Failed to create renderer");

		Graphics::Camera camera = {};
		camera.position = Math::float3(6.5f, -8.0f, 4.0f);
		camera.view = Math::LookAtRH(
			camera.position,
			Math::float3(0.0f, 0.0f, 0.0f),
			Math::float3(0.0f, 0.0f, 1.0f));
		camera.projection = Math::PerspectiveRH_ZO(0.9f, 1280.0f / 720.0f, 0.1f, 100.0f);

		Graphics::Scene scene;
		const Graphics::MeshID cube = scene.CreateMesh(Graphics::CreateCubeMesh());

		Graphics::TextureAsset checker = {};
		checker.width = 2;
		checker.height = 2;
		checker.colorSpace = Graphics::TextureColorSpace::Srgb;
		checker.rgba8 = {
			235, 235, 235, 255,   30, 110, 220, 255,
			30, 110, 220, 255,   235, 235, 235, 255,
		};
		const Graphics::TextureID baseColorMap = scene.CreateTexture(checker);
		const Graphics::TextureID metallicRoughnessMap =
			scene.CreateTexture(MakeSolidTexture(
				255u, 90u, 230u, Graphics::TextureColorSpace::Linear));
		const Graphics::TextureID normalMap =
			scene.CreateTexture(MakeSolidTexture(
				128u, 128u, 255u, Graphics::TextureColorSpace::Linear));
		const Graphics::TextureID occlusionMap =
			scene.CreateTexture(MakeSolidTexture(
				190u, 190u, 190u, Graphics::TextureColorSpace::Linear));
		const Graphics::TextureID emissiveMap =
			scene.CreateTexture(MakeSolidTexture(
				255u, 80u, 20u, Graphics::TextureColorSpace::Srgb));

		Graphics::MaterialDesc matte = {};
		matte.baseColor = Math::float4(0.75f, 0.12f, 0.08f, 1.0f);
		matte.metallicFactor = 0.0f;
		matte.roughnessFactor = 0.9f;

		Graphics::MaterialDesc polishedMetal = {};
		polishedMetal.baseColor = Math::float4(0.75f, 0.78f, 0.82f, 1.0f);
		polishedMetal.metallicFactor = 1.0f;
		polishedMetal.roughnessFactor = 0.12f;

		Graphics::MaterialDesc textured = {};
		textured.baseColorTexture = baseColorMap;
		textured.metallicFactor = 1.0f;
		textured.roughnessFactor = 1.0f;
		textured.metallicRoughnessTexture = metallicRoughnessMap;
		textured.normalTexture = normalMap;
		textured.normalScale = 1.0f;
		textured.occlusionTexture = occlusionMap;
		textured.occlusionStrength = 1.0f;

		Graphics::MaterialDesc emissive = {};
		emissive.baseColor = Math::float4(0.04f, 0.04f, 0.04f, 1.0f);
		emissive.emissiveColor = Math::float3(2.5f, 0.5f, 0.1f);
		emissive.emissiveTexture = emissiveMap;

		const Graphics::MaterialDesc materials[] = { matte, polishedMetal, textured, emissive };
		for(uint32_t i = 0; i < 4; ++i)
		{
			const Graphics::MaterialID material = scene.CreateMaterial(materials[i]);
			const float x = (static_cast<float>(i) - 1.5f) * 2.0f;
			(void)scene.CreateEntity(cube, material, Math::Translation(Math::float3(x, 0.0f, 0.0f)));
		}

		Graphics::DirectionalLight light = {};
		light.direction = Math::float3(0.35f, 0.55f, 0.75f);
		light.intensity = 4.0f;
		light.castShadow = false;
		(void)scene.CreateDirectionalLight(light);

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
