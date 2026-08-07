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

struct ShadowConstants
{
    float4x4 lightViewProjectionMatrix;
};

struct VertexOutput
{
    float4 position [[position]];
    float3 worldPosition;
    float3 normal;
};

vertex VertexOutput main0(
    uint vertexId [[vertex_id]],
    constant DrawConstants& draw [[buffer(0)]],
    constant ShadowConstants& shadow [[buffer(3)]],
    device const float* vertices [[buffer(4)]],
    device const uint* indices [[buffer(5)]])
{
    (void)shadow;
    const int resolvedVertexIndex = int(indices[draw.firstIndex + vertexId]) + draw.vertexOffset;
    const uint vertexBase = uint(resolvedVertexIndex) * 12u;

    const float3 position = float3(
        vertices[vertexBase],
        vertices[vertexBase + 1u],
        vertices[vertexBase + 2u]);
    const float3 normal = float3(
        vertices[vertexBase + 3u],
        vertices[vertexBase + 4u],
        vertices[vertexBase + 5u]);

    const float4 worldPosition = draw.modelMatrix * float4(position, 1.0f);
    VertexOutput output;
    output.position = draw.viewProjectionMatrix * worldPosition;
    output.worldPosition = worldPosition.xyz;
    output.normal = normalize((draw.modelMatrix * float4(normal, 0.0f)).xyz);
    return output;
}
