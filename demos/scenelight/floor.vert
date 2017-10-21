#version 400

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

const int MAX_INSTANCES = 16 * 1024;
const int MAX_MATERIALS_PER_MODEL = 32;
const int NUM_LIGHTS = 2;

layout(push_constant) uniform pcb {
   mat4 Projection;
} PCB;

layout(std140, set = 0, binding = 0) uniform ubo_camera {
   mat4 View;
   mat4 ViewInv;
} CD;

struct ObjData {
   mat4 Model;
   uint material_base_idx;
   uint model_idx;
   uint receives_shadows;
};

layout(std140, set = 1, binding = 0) uniform ubo_obj_data {
   ObjData data[MAX_INSTANCES];
} OID;

struct ShadowMapData {
   mat4 light_viewproj;
   uint shadow_map_size;
   uint pfc_kernel_size;
};

layout(std140, set = 2, binding = 1) uniform ubo_shadow_map_data {
   ShadowMapData data[NUM_LIGHTS];
} SMD;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in uint in_material_idx;

layout(location = 0) out vec3 out_normal;
layout(location = 1) flat out uint out_material_idx;
layout(location = 2) out vec4 out_world_pos;
layout(location = 3) out vec3 out_view_dir;
layout(location = 4) flat out uint out_receives_shadows;
layout(location = 5) out vec4 out_light_space_pos[NUM_LIGHTS];

void main()
{
   ObjData obj_data = OID.data[gl_InstanceIndex];

   vec4 pos = vec4(in_position.x, in_position.y, in_position.z, 1.0);
   mat4 Model = obj_data.Model;
   vec4 world_pos = Model * pos;
   vec4 camera_space_pos = CD.View * world_pos;
   gl_Position = PCB.Projection * camera_space_pos;

   mat3 Normal = transpose(inverse(mat3(Model)));
   out_normal = normalize(Normal * in_normal);

   out_material_idx =
      obj_data.model_idx * MAX_MATERIALS_PER_MODEL +
      obj_data.material_base_idx + in_material_idx;

   out_world_pos = world_pos;

   out_view_dir =
      normalize(vec3(CD.ViewInv * vec4(0.0, 0.0, 0.0, 1.0) - out_world_pos));

   out_receives_shadows = obj_data.receives_shadows;

   for (int i = 0; i < NUM_LIGHTS; i++)
      out_light_space_pos[i] = SMD.data[i].light_viewproj * out_world_pos;
}
