#version 400

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout(push_constant) uniform pcb
{
   mat4 ViewProj;
} PCB;

layout(std140, set = 0, binding = 0) uniform m_ubo
{
   mat4 Model[1024];
} M;

layout(location = 0) in vec3 in_position;

void main()
{
   vec4 pos = vec4(in_position.x, in_position.y, in_position.z, 1.0);
   vec4 world_pos = M.Model[gl_InstanceIndex] * pos;
   gl_Position = PCB.ViewProj * world_pos;
}
