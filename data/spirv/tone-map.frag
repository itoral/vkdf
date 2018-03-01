#version 450

#extension GL_ARB_separate_shader_objects : enable

layout(push_constant) uniform pcb {
   float exposure;
} PCB;

layout(set = 0, binding = 0) uniform sampler2D tex_source;

layout(location = 0) in vec2 in_uv;

layout(location = 0) out vec4 out_color;

void main()
{
   vec4 color = texture(tex_source, in_uv);
   vec3 hdr_color = color.rgb;
   vec3 ldr_color = vec3(1.0) - exp(-hdr_color * PCB.exposure);
   out_color = vec4(ldr_color, color.a);
}
