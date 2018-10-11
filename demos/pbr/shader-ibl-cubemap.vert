#version 450

#extension GL_ARB_separate_shader_objects : enable

layout(std140, set = 0, binding = 0) uniform vp_ubo {
    mat4 View;
    mat4 Projection;
} VP;

layout(location = 0) in vec3 in_pos;

layout(location = 0) out vec3 out_local_pos;

void main()
{
   out_local_pos = in_pos;

   // Remove the camera translation transform so the cubemap, we want
   // the cubemap to always be cenetered at the camera
   mat4 rot_view = mat4(mat3(VP.View));

   // Compute clip-space position
   vec4 clip_pos = VP.Projection * rot_view * vec4(out_local_pos, 1.0);

   // Make NDC z = 1.0 (max depth)
   gl_Position = clip_pos.xyww;

}
