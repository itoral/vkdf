#version 400

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout(location = 0) in vec3 in_normal;

layout(location = 0) out vec4 out_color;

void main() {
   out_color = vec4(in_normal.x != 0 ? 1.0 : 0.0,
                    in_normal.y != 0 ? 1.0 : 0.0,
                    in_normal.z != 0 ? 1.0 : 0.0,
                    1.0);
}
