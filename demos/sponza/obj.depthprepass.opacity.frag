#version 450

#extension GL_ARB_separate_shader_objects : enable

layout(set = 2, binding = 0) uniform sampler2D tex_opacity;

layout(location = 0) in vec2 in_uv;

void main()
{
   // Take opacity component from texture
   float opacity = texture(tex_opacity, in_uv).r;
   if (opacity < 0.01) {
      discard;
   }
}
