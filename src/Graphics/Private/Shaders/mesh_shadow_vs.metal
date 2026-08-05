#include <metal_stdlib>
#include "StockShaderLayout.inc"

using namespace metal;

struct DrawConstants
{
    float4x4 viewProjectionMatrix;
    float4x4 modelMatrix;
    uint textureFlags;
    uint padding0;
    uint padding1;
    uint padding2;
    float4 emissiveColor;
    float4 baseColor;
    float4 materialParams;
};

struct ShadowMatrix
{
    float4x4 lightViewProjectionMatrix;
};

struct ShadowVertex
{
    float3 position [[attribute(0)]];
};

vertex float4 shadowVertexShader(
    ShadowVertex input [[stage_in]],
    constant DrawConstants& drawConstants [[buffer(RENDERER_BINDING_INLINE_CONSTANTS)]],
    constant ShadowMatrix& shadowMatrix [[buffer(RENDERER_BINDING_SHADOW_MATRIX)]]) [[position]]
{
    return shadowMatrix.lightViewProjectionMatrix * drawConstants.modelMatrix * float4(input.position, 1.0f);
}
