#include <metal_stdlib>
#include "StockShaderLayout.inc"

using namespace metal;

struct DrawConstants
{
    float4x4 viewProjectionMatrix;
    float4x4 modelMatrix;
    uint textureFlags;
    uint instanceBase;
    uint padding0;
    uint padding1;
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
    constant ShadowMatrix& shadowMatrix [[buffer(RENDERER_BINDING_SHADOW_MATRIX)]],
    device const float4x4* transforms [[buffer(RENDERER_BINDING_TRANSFORM_STORAGE)]],
    uint instanceId [[instance_id]]) [[position]]
{
    float4x4 resolvedModelMatrix = drawConstants.modelMatrix;
    if (drawConstants.instanceBase != 0u)
    {
        resolvedModelMatrix = transforms[drawConstants.instanceBase - 1u + instanceId];
    }
    return shadowMatrix.lightViewProjectionMatrix * resolvedModelMatrix * float4(input.position, 1.0f);
}
