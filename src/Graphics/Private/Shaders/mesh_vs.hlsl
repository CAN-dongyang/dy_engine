#include "StockShaderLayout.inc"

#define REGISTER_TOKEN_IMPL(prefix, index) prefix##index
#define REGISTER_TOKEN(prefix, index) REGISTER_TOKEN_IMPL(prefix, index)

cbuffer DrawConstants : register(
    REGISTER_TOKEN(b, RENDERER_BINDING_INLINE_CONSTANTS),
    REGISTER_TOKEN(space, RENDERER_DESCRIPTOR_SET))
{
    column_major float4x4 viewProjectionMatrix;
    column_major float4x4 modelMatrix;
    uint textureFlags;
    uint instanceBase;
    uint padding0;
    uint padding1;
    float4 emissiveColor;
    float4 baseColor;
    float4 materialParams;
};

cbuffer ShadowMatrix : register(
    REGISTER_TOKEN(b, RENDERER_BINDING_SHADOW_MATRIX),
    REGISTER_TOKEN(space, RENDERER_DESCRIPTOR_SET))
{
    column_major float4x4 lightViewProjectionMatrix;
};

StructuredBuffer<float4x4> InstanceTransforms : register(
    REGISTER_TOKEN(t, RENDERER_BINDING_TRANSFORM_STORAGE),
    REGISTER_TOKEN(space, RENDERER_DESCRIPTOR_SET));

struct VSInput
{
    float3 position : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float4 tangent : TEXCOORD3;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float3 worldPosition : TEXCOORD1;
    float3 worldNormal : TEXCOORD2;
    float4 worldTangent : TEXCOORD3;
    float4 lightSpacePosition : TEXCOORD4;
};

VSOutput main(VSInput input, uint instanceId : SV_InstanceID)
{
    float4x4 resolvedModelMatrix = modelMatrix;
    if (instanceBase != 0u)
    {
        resolvedModelMatrix = InstanceTransforms[instanceBase - 1u + instanceId];
    }

    const float4 worldPosition = mul(resolvedModelMatrix, float4(input.position, 1.0));
    const float3x3 normalMatrix = (float3x3)resolvedModelMatrix;

    VSOutput output;
    output.position = mul(viewProjectionMatrix, worldPosition);
    output.uv = input.uv;
    output.worldPosition = worldPosition.xyz;
    output.worldNormal = normalize(mul(normalMatrix, input.normal));
    output.worldTangent = float4(normalize(mul(normalMatrix, input.tangent.xyz)), input.tangent.w);
    output.lightSpacePosition = mul(lightViewProjectionMatrix, worldPosition);
    return output;
}
