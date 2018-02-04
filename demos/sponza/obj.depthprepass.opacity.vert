#version 400

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

const int MAX_INSTANCES = 16;

layout(push_constant) uniform pcb
{
   mat4 Projection;
} PCB;

layout(std140, set = 0, binding = 0) uniform ubo_camera {
   mat4 View;
} CD;

struct ObjData {
   mat4 Model;
   uint material_base_idx;
   uint model_idx;
   uint receives_shadows;
};

layout(std140, set = 1, binding = 0) uniform m_ubo
{
   ObjData data[MAX_INSTANCES];
} OD;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;

layout(location = 0) out vec2 out_uv;

void main()
{
   vec4 pos = vec4(in_position.x, in_position.y, in_position.z, 1.0);
   vec4 world_pos = OD.data[gl_InstanceIndex].Model * pos;

   // The braces are important to ensure that gl_Position is calculated
   // exactly the same as in the rendering pass, otherwise computed
   // depth values may be slightly different for the depth prepass
   // and not pass the depth test with EQUAL on the rendering pass
   gl_Position = PCB.Projection * (CD.View * world_pos);

   // UV coordinates need y-flipping
   out_uv = vec2(in_uv.x, -in_uv.y);
}
