#version 450

#extension GL_ARB_separate_shader_objects : enable

layout(std140, set = 0, binding = 0) uniform vp_ubo {
    mat4 View;
    mat4 Projection;
} VP;

const uint NUM_OBJECTS_X = 8;
const uint NUM_OBJECTS_y = 8;

layout(std140, set = 0, binding = 1) uniform m_ubo {
    mat4 Model[NUM_OBJECTS_X * NUM_OBJECTS_y];
} M;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;

layout(location = 0) out      vec3 out_normal;
layout(location = 1) out      vec2 out_uv;
layout(location = 2) out      vec3 out_world_pos;
layout(location = 3) out      vec3 out_view_dir;
layout(location = 4) out flat uint out_row;
layout(location = 5) out flat uint out_col;

void main()
{
   mat4 vp = VP.Projection * VP.View;
   vec4 pos = vec4(in_position.x, in_position.y, in_position.z, 1.0);
   vec4 world_pos = M.Model[gl_InstanceIndex] * pos;
   out_world_pos = world_pos.xyz;

   gl_Position = vp * world_pos;

   out_normal = in_normal;
   out_uv = in_uv;

   // Compute view vector from the vertex to the camera in world space.
   mat4 ViewInv = inverse(VP.View);
   out_view_dir =
      normalize(vec3(ViewInv * vec4(0.0, 0.0, 0.0, 1.0) - world_pos));

   out_row = gl_InstanceIndex % NUM_OBJECTS_X;
   out_col = gl_InstanceIndex / NUM_OBJECTS_X;
}
