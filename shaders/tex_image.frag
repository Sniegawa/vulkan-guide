#version 450

#extension GL_KHR_vulkan_glsl : enable // Its here just for the language highlighter to leave me alone

layout (location = 0) in vec3 inColor;
layout (location = 1) in vec2 inUV;

layout (location = 0) out vec4 outFragColor;

layout(set = 0, binding = 0) uniform sampler2D displayTextures;

void main()
{
	outFragColor = texture(displayTextures,inUV);
}