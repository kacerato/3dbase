#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 0) out vec4 vertexColor;

layout(push_constant) uniform MeshPushConstants {
    mat4 modelViewProjection;
    vec4 color;
} pushConstants;

void main() {
    gl_Position = pushConstants.modelViewProjection * vec4(inPosition, 1.0);
    vertexColor = pushConstants.color;
}
