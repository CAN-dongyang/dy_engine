#pragma once

#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>

#include "Graphics/Mesh.h"
#include "Graphics/Renderer.h"
#include "Graphics/Scene.h"
#include "Math/Math.h"

namespace dy::Examples
{
	class FoxLightDemo
	{
	public:
		FoxLightDemo(Graphics::Scene& scene, Graphics::Renderer& renderer)
			: m_scene(scene)
		{
			Graphics::CameraDesc camera = {};
			camera.eye = Math::float3(3.0f, 3.0f, 2.0f);
			camera.target = Math::float3(0.0f, 0.0f, 0.5f);
			camera.aspect = 1280.0f / 720.0f;
			camera.nearPlane = 0.05f;
			camera.farPlane = 200.0f;
			renderer.SetCamera(camera);

			Graphics::ModelSceneDesc fox = {};
			fox.path = "Models/Fox.glb";
			if(!Graphics::AddModelToScene(m_scene, fox, &m_fox) || !IsValid(m_fox))
				throw std::runtime_error("Failed to load Models/Fox.glb.");
			m_initialRootTransform = m_scene.GetModelInstance(m_fox).rootTransform;
			if(!m_scene.PlayAnimation(m_fox, 1u, true) || !m_scene.SetAnimationLoop(m_fox, true))
				throw std::runtime_error("Failed to configure Fox animation.");

			Graphics::DirectionalLight directional = {};
			directional.castShadow = false;
			m_directional = m_scene.CreateDirectionalLight(directional);
			Graphics::PointLight point = {};
			point.castShadow = false;
			m_point = m_scene.CreatePointLight(point);
			Graphics::SpotLight spot = {};
			spot.castShadow = false;
			m_spot = m_scene.CreateSpotLight(spot);
		}

		void Update(float deltaSeconds, float elapsedSeconds)
		{
			m_scene.GetModelInstance(m_fox).rootTransform =
				Math::Translation(Math::float3(0.0f, 0.0f, 0.08f * std::sin(elapsedSeconds)))
				* m_initialRootTransform;
			if(!m_scene.UpdateAnimations(deltaSeconds).Succeeded())
				throw std::runtime_error("Failed to update Fox animation.");

			const uint32_t phase = static_cast<uint32_t>(elapsedSeconds / 2.0f) % 3u;
			if(phase != m_lastPhase)
			{
				ApplyLights(phase);
				m_lastPhase = phase;
			}
		}

	private:
		void ApplyLights(uint32_t phase)
		{
			Graphics::DirectionalLight directional = m_scene.GetDirectionalLight(ToIndex(m_directional));
			Graphics::PointLight point = m_scene.GetPointLight(ToIndex(m_point));
			Graphics::SpotLight spot = m_scene.GetSpotLight(ToIndex(m_spot));
			if(phase == 0u)
			{
				directional.color = Math::float3(1.0f, 0.55f, 0.3f);
				point.color = Math::float3(1.0f, 0.22f, 0.08f);
				spot.color = Math::float3(1.0f, 0.78f, 0.36f);
				std::cout << "LIGHT_PHASE=warm\n";
			}
			else if(phase == 1u)
			{
				directional.color = Math::float3(0.3f, 0.5f, 1.0f);
				point.color = Math::float3(0.15f, 0.42f, 1.0f);
				spot.color = Math::float3(0.38f, 0.7f, 1.0f);
				std::cout << "LIGHT_PHASE=cool\n";
			}
			else
			{
				directional.color = Math::float3(1.0f, 0.96f, 0.9f);
				point.color = Math::float3(1.0f, 0.35f, 0.2f);
				spot.color = Math::float3(0.35f, 0.6f, 1.0f);
				std::cout << "LIGHT_PHASE=mixed\n";
			}
			directional.direction = Math::float3(0.4f, 0.5f, 0.8f);
			directional.intensity = 3.0f;
			point.position = Math::float3(1.5f, 1.0f, 1.5f);
			point.range = 5.0f;
			point.intensity = 2.0f;
			spot.position = Math::float3(0.0f, 2.0f, 2.0f);
			spot.direction = Math::float3(0.0f, -0.7f, -0.7f);
			spot.range = 6.0f;
			spot.innerConeRadians = 0.25f;
			spot.outerConeRadians = 0.55f;
			spot.intensity = 5.0f;
			m_scene.SetDirectionalLight(ToIndex(m_directional), directional);
			m_scene.SetPointLight(ToIndex(m_point), point);
			m_scene.SetSpotLight(ToIndex(m_spot), spot);
		}

		Graphics::Scene& m_scene;
		ModelInstanceID m_fox = ModelInstanceID::Invalid;
		DirectionalLightID m_directional = DirectionalLightID::Invalid;
		PointLightID m_point = PointLightID::Invalid;
		SpotLightID m_spot = SpotLightID::Invalid;
		Math::float4x4 m_initialRootTransform = Math::float4x4::Identity();
		uint32_t m_lastPhase = 3u;
	};
}
