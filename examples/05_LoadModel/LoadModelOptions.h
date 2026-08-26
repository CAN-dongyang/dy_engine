#pragma once

#include <cstdint>
#include <string>

namespace dy::Examples
{
	inline constexpr const char* kDefaultLoadModelPath = "Models/SimpleSkin.gltf";

	struct LoadModelOptions
	{
		std::string modelPath;
		uint32_t clipIndex = 0u;
		float timeScale = 1.0f;
		float cameraDistance = 9.0f;
		float smokeSeconds = 0.0f;
		bool paused = false;
		bool loop = true;
		bool showHelp = false;
	};

	[[nodiscard]] bool ParseLoadModelOptions(
		int argumentCount,
		const char* const* arguments,
		LoadModelOptions& outOptions,
		std::string& outError);
}
