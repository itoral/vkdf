#version 450

#extension GL_ARB_separate_shader_objects : enable

layout(std140, binding = 0) uniform ubo
{
    mat4 mvp;
} UBO;

layout (location = 0) in vec4 pos;

void main()
{
   gl_Position = UBO.mvp * pos;
}
