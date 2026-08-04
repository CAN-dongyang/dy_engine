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

struct MeshVertex
{
    float3 position [[attribute(0)]];
    float3 normal [[attribute(1)]];
    float2 uv [[attribute(2)]];
    float4 tangent [[attribute(3)]];
};

struct RasterData
{
    float4 position [[position]];
    float2 uv [[user(locn0)]];
    float3 worldPosition [[user(locn1)]];
    float3 worldNormal [[user(locn2)]];
    float4 worldTangent [[user(locn3)]];
    float4 lightSpacePosition [[user(locn4)]];
};

vertex RasterData vertexShader(
    MeshVertex input [[stage_in]],
    constant DrawConstants& drawConstants [[buffer(RENDERER_BINDING_INLINE_CONSTANTS)]],
    constant ShadowMatrix& shadowMatrix [[buffer(RENDERER_BINDING_SHADOW_MATRIX)]],
    device const float4x4* transforms [[buffer(RENDERER_BINDING_TRANSFORM_STORAGE)]],
    uint instanceId [[instance_id]])
{
    float4x4 resolvedModelMatrix = drawConstants.modelMatrix;
    if (drawConstants.instanceBase != 0u)
    {
        resolvedModelMatrix = transforms[drawConstants.instanceBase - 1u + instanceId];
    }

    const float4 worldPosition = resolvedModelMatrix * float4(input.position, 1.0f);
    const float3x3 normalMatrix = float3x3(
        resolvedModelMatrix[0].xyz,
        resolvedModelMatrix[1].xyz,
        resolvedModelMatrix[2].xyz);

    RasterData output;
    output.position = drawConstants.viewProjectionMatrix * worldPosition;
    output.uv = input.uv;
    output.worldPosition = worldPosition.xyz;
    output.worldNormal = normalize(normalMatrix * input.normal);
    output.worldTangent = float4(normalize(normalMatrix * input.tangent.xyz), input.tangent.w);
    output.lightSpacePosition = shadowMatrix.lightViewProjectionMatrix * worldPosition;
    return output;
}
