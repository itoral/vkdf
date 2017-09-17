#version 400

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

struct Material {
   vec4 diffuse;
   vec4 ambient;
   vec4 specular;
   float shininess;
   uint diffuse_tex_count;
   uint normal_tex_count;
   uint specular_tex_count;
   uint opacity_tex_count;
   uint pad0, pad1, pad2;
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
