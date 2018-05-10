#version 450

#extension GL_ARB_separate_shader_objects : enable

layout(set = 1, binding = 0) uniform sampler2D tex_source;

layout(location = 0) in vec2 in_uv;
layout(location = 1) in flat float in_brightness;

layout(location = 0) out vec4 out_color;

void main()
{
   out_color = texture(tex_source, in_uv) * in_brightness;
}
