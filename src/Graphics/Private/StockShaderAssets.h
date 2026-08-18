#pragma once

#include <cstddef>

namespace dy::Graphics::Private
{
	struct ShaderAsset
	{
		const void* binary = nullptr;
		std::size_t binarySize = 0;
		const char* entryPoint = nullptr;
	};

	struct StockShaderAssets
	{
		ShaderAsset vertex;
		ShaderAsset fragment;
		ShaderAsset shadowVertex;
	};

	[[nodiscard]] StockShaderAssets GetStockShaderAssets(bool shadowsEnabled);
}
