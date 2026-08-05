#pragma once

#include <memory>

#include "Graphics/RendererConfig.h"

namespace dy::Graphics
{
	class Scene;

	class Renderer
	{
	public:
		[[nodiscard]] static std::unique_ptr<Renderer> Create(
			const void* windowHandle,
			const RendererDesc& desc = {});

		~Renderer();
		void Render(const Scene& scene, const Camera& camera);

	private:
		struct Impl;

		explicit Renderer(std::unique_ptr<Impl> impl);

		std::unique_ptr<Impl> m_impl;
	};
}
