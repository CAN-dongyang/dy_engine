#include <metal_stdlib>
using namespace metal;

struct DrawConstants
{
    float4x4 viewProjectionMatrix;
    float4x4 modelMatrix;
    float drawMode;
    uint firstIndex;
    int vertexOffset;
    uint firstVertex;
    float4 emissiveColor;
    float4 baseColor;
    float4 materialParams;
    float4 textureIndices;
};

struct LightingConstants
{
    float4 cameraPosition;
    float4 directionalLightDirection;
    float4 directionalLightColor;
    float4 ambientColor;
    float4 shadowParams;
    float4 pbrParams;
    float4 environmentColor;
    float4 pointLightPositionRange;
    float4 pointLightColorIntensity;
};

struct VertexOutput
{
    float4 position [[position]];
    float3 worldPosition;
    float3 normal;
};

fragment float4 main0(
    VertexOutput input [[stage_in]],
    constant DrawConstants& draw [[buffer(0)]],
    constant LightingConstants& lighting [[buffer(1)]])
{
    const float3 normal = normalize(input.normal);
    const float3 lightDirection = normalize(lighting.directionalLightDirection.xyz);
    const float diffuseFactor = max(dot(normal, lightDirection), 0.0f);
    const float3 directLight = lighting.directionalLightColor.rgb *
        lighting.directionalLightColor.a * diffuseFactor;
    const float3 ambientLight = lighting.ambientColor.rgb * lighting.ambientColor.a;
    const float3 linearColor = draw.baseColor.rgb * (ambientLight + directLight) + draw.emissiveColor.rgb;
    const float3 mappedColor = linearColor / (linearColor + 1.0f);
    const float3 displayColor = pow(mappedColor, float3(1.0f / 2.2f));
    return float4(displayColor, draw.baseColor.a);
}
