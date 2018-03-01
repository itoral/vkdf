#version 450

#extension GL_ARB_separate_shader_objects : enable

layout(set = 0, binding = 0) uniform sampler2D tex_input;

layout(location = 0) in vec2 in_uv;

layout(location = 0) out vec4 out_color;

const float BLEND_STRENGTH       = 1.0f;

void main()
{
   out_color = texture(tex_input, in_uv);
   out_color.a *= BLEND_STRENGTH;
}
