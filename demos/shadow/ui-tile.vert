#version 400

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout(std140, set = 0, binding = 0) uniform mvp_ubo {
    mat4 mvp;
} MVP;

layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec2 in_uv;

layout(location = 0) out vec2 out_uv;

void main()
{
   gl_Position = MVP.mvp * vec4(in_pos.x, in_pos.y, 0.0, 1.0);
   out_uv = in_uv;
}
