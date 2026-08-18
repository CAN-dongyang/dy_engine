#include "Graphics/Private/StockShaderAssets.h"

#if defined(ENABLE_METAL)
#include "StockMetalLibrary.h"
#else
#include "StockFragmentShader.h"
#include "StockFragmentShaderNoShadows.h"
#include "StockShadowVertexShader.h"
#include "StockVertexShader.h"
#include "StockVertexShaderNoShadows.h"
#endif

namespace dy::Graphics::Private
{
	StockShaderAssets GetStockShaderAssets(bool shadowsEnabled)
	{
#if defined(ENABLE_METAL)
		return {
			{ kStockMetalLibrary, kStockMetalLibrarySize, shadowsEnabled ? "vertexShader" : "vertexShaderNoShadows" },
			{ kStockMetalLibrary, kStockMetalLibrarySize, shadowsEnabled ? "fragmentShader" : "fragmentShaderNoShadows" },
			{ kStockMetalLibrary, kStockMetalLibrarySize, "shadowVertexShader" }
		};
#else
		return {
			shadowsEnabled
				? ShaderAsset{ kStockVertexShader, kStockVertexShaderSize, "main" }
				: ShaderAsset{ kStockVertexShaderNoShadows, kStockVertexShaderNoShadowsSize, "main" },
			shadowsEnabled
				? ShaderAsset{ kStockFragmentShader, kStockFragmentShaderSize, "main" }
				: ShaderAsset{ kStockFragmentShaderNoShadows, kStockFragmentShaderNoShadowsSize, "main" },
			{ kStockShadowVertexShader, kStockShadowVertexShaderSize, "main" }
		};
#endif
	}
}
