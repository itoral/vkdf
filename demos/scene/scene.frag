#version 400

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

struct Material {
  vec4 diffuse;
  vec4 ambient;
  vec4 specular;
  vec4 shininess;
};

layout(std140, set = 1, binding = 1) uniform ubo_obj_inst_data {
     Material materials[6];
} OID;

layout(location = 0) flat in uint in_mat_idx;

layout(location = 0) out vec4 out_color;

void main()
{
   Material mat = OID.materials[in_mat_idx];
   out_color = mat.diffuse;
}
