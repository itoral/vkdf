#version 450 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec2 attr;

layout(binding = 3) buffer ssbo {
  vec2 out_value[];
} SSBO;

void main() {
   SSBO.out_value[gl_VertexIndex] = attr;
}
