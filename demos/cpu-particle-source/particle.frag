#version 400

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in flat uint  vs_color_idx;
layout (location = 1) in      float vs_age;

layout (location = 0) out vec4 fs_color;

const vec4 colors[5] = vec4[](
   vec4(1.0, 0.0, 0.0, 1.0),
   vec4(0.0, 1.0, 0.0, 1.0),
   vec4(0.0, 0.0, 1.0, 1.0),
   vec4(1.0, 1.0, 0.0, 1.0),
   vec4(0.0, 1.0, 1.0, 1.0)
);


void main() {
   fs_color = colors[vs_color_idx] * (1.0f - vs_age);
}
