#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dy::Graphics
{
	enum class TextureID : uint32_t
	{
		Invalid = 0xFFFFFFFFu
	};

	[[nodiscard]] inline constexpr uint32_t ToIndex(TextureID id)
	{
		return static_cast<uint32_t>(id);
	}

	[[nodiscard]] inline constexpr bool IsValid(TextureID id)
	{
		return id != TextureID::Invalid;
	}

	struct TextureAsset
	{
		std::string sourcePath;
		uint32_t width = 0;
		uint32_t height = 0;
		std::vector<uint8_t> rgba8;
	};
}
