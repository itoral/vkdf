#version 400

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

struct Material {
    vec4 diffuse;
    vec4 ambient;
    vec4 specular;
    float shininess;
};

layout(std140, set = 0, binding = 2) uniform materials_ubo {
   // The number of materials varies depending on the model, so
   // just make this large enough for whatever the application needs
   Material materials[128];
} Materials;

layout(location = 0) in vec3 in_normal;
layout(location = 1) flat in uint in_material_idx;

layout(location = 0) out vec4 out_color;

void main()
{
   out_color = Materials.materials[in_material_idx].diffuse;
}
