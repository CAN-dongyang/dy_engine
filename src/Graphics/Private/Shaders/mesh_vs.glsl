#version 450
#extension GL_GOOGLE_include_directive : require

#include "StockShaderLayout.inc"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec4 inTangent;

layout(location = 0) out vec2 fragUv;
layout(location = 1) out vec3 fragWorldPosition;
layout(location = 2) out vec3 fragNormal;
layout(location = 3) out vec4 fragTangent;
layout(location = 4) out vec4 fragLightSpacePosition;

layout(push_constant) uniform DrawConstants
{
    mat4 viewProjectionMatrix;
    mat4 modelMatrix;
    uint textureFlags;
    uint instanceBase;
    uint padding0;
    uint padding1;
    vec4 emissiveColor;
    vec4 baseColor;
    vec4 materialParams;
} drawConstants;

layout(std430, set = RENDERER_DESCRIPTOR_SET, binding = RENDERER_BINDING_TRANSFORM_STORAGE) readonly buffer TransformStorage
{
    mat4 modelMatrices[];
} transforms;

layout(set = RENDERER_DESCRIPTOR_SET, binding = RENDERER_BINDING_SHADOW_MATRIX) uniform ShadowMatrix
{
    mat4 lightViewProjectionMatrix;
} shadowMatrix;

void main()
{
    mat4 resolvedModelMatrix = drawConstants.modelMatrix;
    if (drawConstants.instanceBase != 0u)
    {
        resolvedModelMatrix = transforms.modelMatrices[drawConstants.instanceBase - 1u + gl_InstanceIndex];
    }

    vec4 worldPosition = resolvedModelMatrix * vec4(inPosition, 1.0);
    gl_Position = drawConstants.viewProjectionMatrix * worldPosition;
    fragUv = inUv;
    fragWorldPosition = worldPosition.xyz;
    fragNormal = normalize(mat3(resolvedModelMatrix) * inNormal);
    fragTangent = vec4(normalize(mat3(resolvedModelMatrix) * inTangent.xyz), inTangent.w);
    fragLightSpacePosition = shadowMatrix.lightViewProjectionMatrix * worldPosition;
}
