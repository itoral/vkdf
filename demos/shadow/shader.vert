#version 400

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout(std140, set = 0, binding = 0) uniform vp_ubo {
    mat4 View;
    mat4 Projection;
} VP;

layout(std140, set = 0, binding = 1) uniform m_ubo {
    mat4 Model[501];
} M;

layout(std140, set = 1, binding = 1) uniform light_vp_ubo {
     mat4 ViewProjection;
} LVP;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in uint in_material_idx;

layout(location = 0) out vec3 out_normal;
layout(location = 1) flat out uint out_material_idx;
layout(location = 2) out vec4 out_world_pos;
layout(location = 3) out vec3 out_view_dir;
layout(location = 4) out float out_cam_dist;
layout(location = 5) out vec4 out_shadow_map_coord;

void main()
{
   vec4 pos = vec4(in_position.x, in_position.y, in_position.z, 1.0);
   mat4 Model = M.Model[gl_InstanceIndex];
   vec4 world_pos = Model * pos;
   vec4 camera_space_pos = VP.View * world_pos;

   gl_Position = VP.Projection * camera_space_pos;

   // Compute normal matrix to apply rotation / scale transforms
   // to the normal vectors as well. We need to make sure that
   // normals are normalized so that interpolation preserves
   // direction (note that interpolated normals may not be
   // normalized and additional normalization might be required
   // in the fragment shader)
   mat3 Normal = transpose(inverse(mat3(Model)));
   out_normal = normalize(Normal * in_normal);

   out_material_idx = in_material_idx;
   out_world_pos = world_pos;

   // Compute view vector from the vertex to the camera in world space.
   // We need this to compute specular reflection in the fragment shader.
   // To do this, we need to use the inverse of the View matrix in order
   // to compute world space position of the camera (which is always
   // at (0,0,0)). This could actually be computed in the CPU...
   mat4 ViewInv = inverse(VP.View);
   out_view_dir =
      normalize(vec3(ViewInv * vec4(0.0, 0.0, 0.0, 1.0) - out_world_pos));

   out_cam_dist = length(camera_space_pos.xyz);

   // Compute position in shadow map space:
   //
   // 1. Compute position in light space
   // 2. Do perspective division to get light space NDC coordinates
   // 3. Transform from Light's NDC space to shadow map space (notice
   //    that Vulkan NDC for Z already is in [0,1])
   //
   // Alternatively, we could've made the Light's ViewProjection include this
   // transformation so we avoid the extra work here.
   out_shadow_map_coord = LVP.ViewProjection * world_pos;
   out_shadow_map_coord /= out_shadow_map_coord.w;
   out_shadow_map_coord.xy = out_shadow_map_coord.xy * 0.5 + 0.5;
}
