#version 450
#extension GL_KHR_vulkan_glsl : enable // Its here just for the language highlighter to leave me alone

layout(location = 0) in vec3 inColor;

layout (location = 0) out vec4 outFragColor;

void main()
{
	outFragColor = vec4(inColor,1.0);
}