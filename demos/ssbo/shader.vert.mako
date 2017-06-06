#version 450 core

layout (location = 0) in ${type_name} attr;

layout(binding = 3) buffer ssbo {
  ${type_name} out_value[];
} SSBO;

void main() {
   SSBO.out_value[gl_VertexIndex] = attr;
}
