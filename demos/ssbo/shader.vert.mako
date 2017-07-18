#version 450 core

#extension GL_AMD_gpu_shader_half_float: enable

layout (location = 7) in ${type_name} attr;

layout(binding = 3) buffer ssbo {
  ${type_name} out_value[];
} SSBO;

void main() {
   SSBO.out_value[gl_VertexIndex] = attr;
}
