#pragma once

#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>

#include "Platform/Window.h"
#include "RHI/IDevice.h"
#include "Graphics/Renderer.h"
#include "Graphics/Scene.h"

namespace dy
{
	class Application
	{
	public:
		Application(
			const char* title,
			uint32_t width = 1280u,
			uint32_t height = 720u,
			Graphics::RendererDesc rendererDesc = {})
			: m_window(std::make_unique<Platform::Window>(width, height, title))
			, m_startTime(std::chrono::steady_clock::now())
			, m_previousFrameTime(m_startTime)
		{
			if(rendererDesc.vertexShaderPath == nullptr)
			{
				m_vertexShaderPath = std::string(DY_SHADER_DIR) + "/mesh_vs" + ShaderExtension();
				rendererDesc.vertexShaderPath = m_vertexShaderPath.c_str();
			}
			if(rendererDesc.pixelShaderPath == nullptr)
			{
				m_pixelShaderPath = std::string(DY_SHADER_DIR) + "/mesh_ps" + ShaderExtension();
				rendererDesc.pixelShaderPath = m_pixelShaderPath.c_str();
			}

			m_device.reset(RHI::IDevice::Create(m_window->GetHandle()));
			if(!m_device) throw std::runtime_error("Failed to create device.");

			m_renderer = std::make_unique<Graphics::Renderer>();
			if(!m_renderer->Initialize(m_device.get(), rendererDesc))
				throw std::runtime_error("Failed to initialize renderer.");

			m_scene = std::make_unique<Graphics::Scene>();
			m_initialized = true;
		}

		~Application()
		{
			if(m_initialized) m_renderer->Shutdown(m_device.get());
		}

		Application(const Application&) = delete;
		Application& operator=(const Application&) = delete;
		Application(Application&&) = delete;
		Application& operator=(Application&&) = delete;

		[[nodiscard]] bool BeginFrame()
		{
			if(!m_initialized || !m_window->IsRunning()) return false;

			m_window->PollEvents();
			const auto now = std::chrono::steady_clock::now();
			m_deltaSeconds = std::chrono::duration<float>(now - m_previousFrameTime).count();
			m_elapsedSeconds = std::chrono::duration<float>(now - m_startTime).count();
			m_previousFrameTime = now;
			m_device->BeginFrame();
			return true;
		}

		void EndFrame()
		{
			m_renderer->Render(*m_scene, m_device.get());
			m_device->Present();
		}

		[[nodiscard]] Graphics::Scene& GetScene() { return *m_scene; }
		[[nodiscard]] Graphics::Renderer& GetRenderer() { return *m_renderer; }
		[[nodiscard]] float GetDeltaSeconds() const { return m_deltaSeconds; }
		[[nodiscard]] float GetElapsedSeconds() const { return m_elapsedSeconds; }

	private:
		[[nodiscard]] static const char* ShaderExtension()
		{
#if defined(ENABLE_METAL)
			return ".metal";
#elif defined(ENABLE_VULKAN)
			return ".spv";
#elif defined(ENABLE_D3D12)
			return ".hlsl";
#else
			return ".glsl";
#endif
		}

		std::unique_ptr<Platform::Window> m_window;
		std::unique_ptr<RHI::IDevice> m_device;
		std::unique_ptr<Graphics::Renderer> m_renderer;
		std::unique_ptr<Graphics::Scene> m_scene;
		std::string m_vertexShaderPath;
		std::string m_pixelShaderPath;
		std::chrono::steady_clock::time_point m_startTime;
		std::chrono::steady_clock::time_point m_previousFrameTime;
		float m_deltaSeconds = 0.0f;
		float m_elapsedSeconds = 0.0f;
		bool m_initialized = false;
	};
}
