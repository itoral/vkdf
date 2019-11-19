#version 450

#extension GL_ARB_separate_shader_objects : enable

const uint MAX_PARTICLES = 100;

struct ParticleData {
   mat4 model;
   uint material_idx;
   float age;
   uint pad1,pad2;
};

layout(std140, binding = 0) uniform ubo
{
    mat4 ViewProjection;
    ParticleData data[MAX_PARTICLES];
} UBO;

layout(location = 0) out flat uint  vs_color_idx;
layout(location = 1) out      float vs_age;

void main()
{
   const vec4 particle_pos = vec4(0, 0, 0, 1);
   ParticleData pd = UBO.data[gl_InstanceIndex];
   gl_Position = UBO.ViewProjection * pd.model * particle_pos;
   vs_color_idx = pd.material_idx;
   vs_age = pd.age;
   gl_PointSize = 2.0;
}
