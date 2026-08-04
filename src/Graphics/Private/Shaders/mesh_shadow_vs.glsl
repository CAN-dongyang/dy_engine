#version 450
#extension GL_GOOGLE_include_directive : require

#include "StockShaderLayout.inc"

layout(location = 0) in vec3 inPosition;

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

layout(set = RENDERER_DESCRIPTOR_SET, binding = RENDERER_BINDING_SHADOW_MATRIX) uniform ShadowMatrix
{
    mat4 lightViewProjectionMatrix;
} shadowMatrix;

layout(std430, set = RENDERER_DESCRIPTOR_SET, binding = RENDERER_BINDING_TRANSFORM_STORAGE) readonly buffer TransformStorage
{
    mat4 modelMatrices[];
} transforms;

void main()
{
    mat4 resolvedModelMatrix = drawConstants.modelMatrix;
    if (drawConstants.instanceBase != 0u)
    {
        resolvedModelMatrix = transforms.modelMatrices[drawConstants.instanceBase - 1u + gl_InstanceIndex];
    }
    gl_Position = shadowMatrix.lightViewProjectionMatrix * resolvedModelMatrix * vec4(inPosition, 1.0);
}
