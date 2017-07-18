#version 400

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout(push_constant) uniform pcb {
   mat4 Projection;
} PCB;

layout(std140, set = 0, binding = 0) uniform ubo_camera {
   mat4 View;
} CD;

struct ObjData {
   mat4 Model;
   uint mat_idx;
};

layout(std140, set = 1, binding = 0) uniform ubo_obj_inst_data {
   ObjData data[1000000];
} OID;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;

layout(location = 0) flat out uint out_mat_idx;

void main()
{
   ObjData obj_data = OID.data[gl_InstanceIndex];

   vec4 pos = vec4(in_position.x, in_position.y, in_position.z, 1.0);
   mat4 Model = obj_data.Model;
   vec4 world_pos = Model * pos;
   vec4 camera_space_pos = CD.View * world_pos;
   gl_Position = PCB.Projection * camera_space_pos;

   out_mat_idx = obj_data.mat_idx;
}
