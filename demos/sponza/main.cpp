#include "vkdf.hpp"

// ================================= CONFIG ===================================

/* Window resolution */
const float    WIN_WIDTH                 = 1024.0f;
const float    WIN_HEIGHT                = 768.0f;
const bool     WIN_FULLSCREEN            = false;

/* Sponza flag mesh */
const bool     SHOW_SPONZA_FLAG_MESH     = false;
const uint32_t SPONZA_FLAG_MESH_IDX      = 4;

/* Show debug texture */
const bool     SHOW_DEBUG_TILE           = false;

/* Pipeline options */
const bool     ENABLE_CLIPPING           = true;
const bool     ENABLE_DEPTH_PREPASS      = true;
const bool     ENABLE_DEFERRED_RENDERING = true;

/* Anisotropic filtering */
const float    MAX_ANISOTROPY            = 16.0f; // Min=0.0 (disabled)

/* Screen Space Ambient Occlusion */
const bool     ENABLE_SSAO               = true;
const uint32_t SSAO_NUM_SAMPLES          = 24;
const float    SSAO_RADIUS               = 0.75f;
const float    SSAO_BIAS                 = 0.05f;
const float    SSAO_INTENSITY            = 3.0f;
const uint32_t SSAO_BLUR_SIZE            = 2;     // Min=0 (no blur)
const float    SSAO_DOWNSAMPLING         = 1.0f;  // Min=1.0 (no downsampling)
const VkFilter SSAO_FILTER               = VK_FILTER_LINEAR;


// =============================== Declarations ===============================

enum {
   DIFFUSE_TEX_BINDING  = 0,
   NORMAL_TEX_BINDING   = 1,
   SPECULAR_TEX_BINDING = 2,
   OPACITY_TEX_BINDING  = 3,
};

struct PCBData {
   uint8_t proj[sizeof(glm::mat4)];
};

typedef struct {
   VkdfContext *ctx;

   VkdfScene *scene;

   VkdfCamera *camera;

   struct {
      VkDescriptorPool static_ubo_pool;
      VkDescriptorPool sampler_pool;
   } descriptor_pool;

   VkCommandPool cmd_pool;

   struct {
      struct {
         VkDescriptorSetLayout camera_view_layout;
         VkDescriptorSet camera_view_set;

         VkDescriptorSetLayout obj_layout;
         VkDescriptorSet obj_set;

         VkDescriptorSetLayout light_layout;
         VkDescriptorSet light_set;

         VkDescriptorSetLayout obj_tex_layout;         /* diffuse, normal, specular */
         VkDescriptorSetLayout obj_tex_opacity_layout; /* diffuse, normal, specular, opacity */
         VkDescriptorSet obj_tex_set[32];

         VkDescriptorSetLayout depth_prepass_tex_layout; /* opacity */
         VkDescriptorSet depth_prepass_tex_set[32];

         VkDescriptorSetLayout shadow_map_sampler_layout;
         VkDescriptorSet shadow_map_sampler_set;

         VkDescriptorSetLayout gbuffer_tex_layout;
         VkDescriptorSet gbuffer_tex_set;
      } descr;

      struct {
         VkPipelineLayout depth_prepass;
         VkPipelineLayout depth_prepass_opacity;
         VkPipelineLayout base;
         VkPipelineLayout opacity;
         VkPipelineLayout gbuffer_base;
         VkPipelineLayout gbuffer_opacity;
         VkPipelineLayout gbuffer_merge;
      } layout;

      VkPipeline depth_prepass;
      VkPipeline depth_prepass_opacity;
      VkPipeline sponza;
      VkPipeline sponza_opacity;
      VkPipeline gbuffer_merge;
   } pipelines;

   struct {
      struct {
         VkdfBuffer buf;
         VkDeviceSize size;
      } camera_view;
   } ubos;

   struct {
      struct {
         VkShaderModule vs;
         VkShaderModule vs_opacity;
         VkShaderModule fs_opacity;
      } depth_prepass;

      struct {
         VkShaderModule vs;
         VkShaderModule fs;
         VkShaderModule fs_opacity;
      } obj_forward;

      struct {
         VkShaderModule vs;
         VkShaderModule fs;
         VkShaderModule fs_opacity;
      } obj_gbuffer;

      struct {
         VkShaderModule vs;
         VkShaderModule fs;
         VkShaderModule fs_ssao;
      } gbuffer_merge;
   } shaders;

   VkdfMesh *tile_mesh;
   VkdfModel *sponza_model;
   VkdfObject *sponza_obj;
   bool sponza_mesh_visible[400];

   VkSampler sponza_sampler;
   VkSampler gbuffer_sampler;
   VkSampler ssao_sampler;

   struct {
      VkdfImage image;
      VkSampler sampler;
      struct {
         VkShaderModule vs;
         VkShaderModule fs;
      } shaders;
      struct {
         VkDescriptorSetLayout sampler_set_layout;
         VkDescriptorSet sampler_set;
         VkPipelineLayout layout;
         VkPipeline pipeline;
      } pipeline;
      VkRenderPass renderpass;
      VkFramebuffer framebuffer;
      VkCommandBuffer cmd_buf;
   } debug;
} SceneResources;

static void
postprocess_draw(VkdfContext *ctx,
                 VkSemaphore scene_draw_sem,
                 VkSemaphore postprocess_draw_sem,
                 void *data);


// ============================== Implementation ==============================

static inline VkdfBuffer
create_ubo(VkdfContext *ctx, uint32_t size, uint32_t usage, uint32_t mem_props)
{
   usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
   VkdfBuffer buf = vkdf_create_buffer(ctx, 0, size, usage, mem_props);
   return buf;
}

static VkDescriptorSet
create_descriptor_set(VkdfContext *ctx,
                      VkDescriptorPool pool,
                      VkDescriptorSetLayout layout)
{
   VkDescriptorSet set;
   VkDescriptorSetAllocateInfo alloc_info[1];
   alloc_info[0].sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
   alloc_info[0].pNext = NULL;
   alloc_info[0].descriptorPool = pool;
   alloc_info[0].descriptorSetCount = 1;
   alloc_info[0].pSetLayouts = &layout;
   VK_CHECK(vkAllocateDescriptorSets(ctx->device, alloc_info, &set));

   return set;
}

static void
init_ubos(SceneResources *res)
{
   // Camera view matrix
   res->ubos.camera_view.size = sizeof(glm::mat4);
   res->ubos.camera_view.buf = create_ubo(res->ctx,
                                          res->ubos.camera_view.size,
                                          VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
}

void
update_visible_sponza_meshes(SceneResources *res)
{
   VkdfCamera *camera = vkdf_scene_get_camera(res->scene);
   if (!vkdf_camera_is_dirty(camera))
      return;

   const VkdfBox *cam_box = vkdf_camera_get_frustum_box(camera);
   const VkdfPlane *cam_planes = vkdf_camera_get_frustum_planes(camera);
   vkdf_object_get_visible_meshes(res->sponza_obj,
                                  cam_box, cam_planes,
                                  res->sponza_mesh_visible);
}

static bool
record_update_resources_command(VkdfContext *ctx,
                                VkCommandBuffer cmd_buf,
                                void *data)
{
   SceneResources *res = (SceneResources *) data;

   // Update camera view matrix
   VkdfCamera *camera = vkdf_scene_get_camera(res->scene);
   if (!vkdf_camera_is_dirty(camera))
      return false;

   glm::mat4 view = vkdf_camera_get_view_matrix(res->camera);
   vkCmdUpdateBuffer(cmd_buf,
                     res->ubos.camera_view.buf.buf,
                     0, sizeof(glm::mat4),
                     &view[0][0]);

   return true;
}

static void
record_instanced_draw(VkCommandBuffer cmd_buf,
                      VkPipeline pipeline,
                      VkPipeline pipeline_opacity,
                      VkdfModel *model,
                      bool *mesh_visible,
                      uint32_t count,
                      uint32_t first_instance,
                      VkPipelineLayout pipeline_layout,
                      VkPipelineLayout pipeline_opacity_layout,
                      uint32_t descr_set_offset,
                      VkDescriptorSet *obj_tex_set,
                      bool for_depth_prepass)
{
   VkPipeline bound_pipeline = 0;

   for (uint32_t i = 0; i < model->meshes.size(); i++) {
      VkdfMesh *mesh = model->meshes[i];

      if (mesh->active == false)
         continue;

      if (mesh_visible[i] == false)
         continue;

      bool has_opacity =
         model->materials[mesh->material_idx].opacity_tex_count > 0;

      VkPipelineLayout required_pipeline_layout;
      VkPipeline required_pipeline;
       if (has_opacity) {
         required_pipeline_layout = pipeline_opacity_layout;
         required_pipeline = pipeline_opacity;
      } else {
         required_pipeline_layout = pipeline_layout;
         required_pipeline = pipeline;
      }

      if (!for_depth_prepass) {
         // We need to have a valid sampler even if the material for this mesh
         // doesn't use textures because we have a single shader that handles both
         // solid-only and solid+texture materials.
         VkDescriptorSet tex_set = obj_tex_set[mesh->material_idx];
         assert(tex_set);

         // Bind descriptor set with texture samplers for this material
         vkCmdBindDescriptorSets(cmd_buf,
                                 VK_PIPELINE_BIND_POINT_GRAPHICS,
                                 required_pipeline_layout,
                                 descr_set_offset,       // First decriptor set
                                 1,                      // Descriptor set count
                                 &tex_set,               // Descriptor sets
                                 0,                      // Dynamic offset count
                                 NULL);                  // Dynamic offsets
      } else if (has_opacity) {
         assert(for_depth_prepass);

         VkDescriptorSet tex_set = obj_tex_set[mesh->material_idx];
         assert(tex_set);

         vkCmdBindDescriptorSets(cmd_buf,
                                 VK_PIPELINE_BIND_POINT_GRAPHICS,
                                 required_pipeline_layout,
                                 descr_set_offset,       // First decriptor set
                                 1,                      // Descriptor set count
                                 &tex_set,               // Descriptor sets
                                 0,                      // Dynamic offset count
                                 NULL);                  // Dynamic offsets
      }

      const VkDeviceSize offsets[1] = { 0 };
      vkCmdBindVertexBuffers(cmd_buf,
                             0,                         // Start Binding
                             1,                         // Binding Count
                             &mesh->vertex_buf.buf,     // Buffers
                             offsets);                  // Offsets

      // Bind pipeline
      if (bound_pipeline != required_pipeline) {
         vkCmdBindPipeline(cmd_buf,
                           VK_PIPELINE_BIND_POINT_GRAPHICS,
                           required_pipeline);
         bound_pipeline = required_pipeline;
      }

      vkdf_mesh_draw(mesh, cmd_buf, count, first_instance);
   }
}

static void
record_forward_scene_commands(VkdfContext *ctx, VkCommandBuffer cmd_buf,
                              GHashTable *sets, bool is_dynamic,
                              bool is_depth_prepass, void *data)
{
   assert(!ENABLE_DEFERRED_RENDERING);

   SceneResources *res = (SceneResources *) data;

   // Push constants: camera projection matrix
   struct PCBData pcb_data;
   glm::mat4 *proj = vkdf_camera_get_projection_ptr(res->scene->camera);
   memcpy(&pcb_data.proj, &(*proj)[0][0], sizeof(pcb_data.proj));

   uint32_t descriptor_set_count;
   if (!is_depth_prepass) {
      vkCmdPushConstants(cmd_buf,
                         res->pipelines.layout.base,
                         VK_SHADER_STAGE_VERTEX_BIT,
                         0, sizeof(pcb_data), &pcb_data);

      // Bind descriptor sets for everything but textures
      VkDescriptorSet descriptor_sets[] = {
         res->pipelines.descr.camera_view_set,
         res->pipelines.descr.obj_set,
         res->pipelines.descr.light_set,
         res->pipelines.descr.shadow_map_sampler_set,
      };

      descriptor_set_count =
         sizeof(descriptor_sets) / sizeof(descriptor_sets[0]);

      vkCmdBindDescriptorSets(cmd_buf,
                              VK_PIPELINE_BIND_POINT_GRAPHICS,
                              res->pipelines.layout.base,
                              0,                        // First decriptor set
                              descriptor_set_count,     // Descriptor set count
                              descriptor_sets,          // Descriptor sets
                              0,                        // Dynamic offset count
                              NULL);                    // Dynamic offsets
   } else {
      vkCmdPushConstants(cmd_buf,
                         res->pipelines.layout.depth_prepass,
                         VK_SHADER_STAGE_VERTEX_BIT,
                         0, sizeof(pcb_data), &pcb_data);

      VkDescriptorSet descriptor_sets[] = {
         res->pipelines.descr.camera_view_set,
         res->pipelines.descr.obj_set,
      };

      descriptor_set_count =
         sizeof(descriptor_sets) / sizeof(descriptor_sets[0]);

      vkCmdBindDescriptorSets(cmd_buf,
                              VK_PIPELINE_BIND_POINT_GRAPHICS,
                              res->pipelines.layout.depth_prepass,
                              0,                        // First decriptor set
                              descriptor_set_count,     // Descriptor set count
                              descriptor_sets,          // Descriptor sets
                              0,                        // Dynamic offset count
                              NULL);                    // Dynamic offsets
   }

   // Render objects
   VkPipeline pipeline, pipeline_opacity;
   VkPipelineLayout pipeline_layout, pipeline_opacity_layout;
   VkDescriptorSet *tex_set;
   char *set_id;
   VkdfSceneSetInfo *set_info;
   GHashTableIter iter;
   g_hash_table_iter_init(&iter, sets);
   while (g_hash_table_iter_next(&iter, (void **)&set_id, (void **)&set_info)) {
      if (!set_info || set_info->count == 0)
         continue;

      if (!strcmp(set_id, "sponza")) {
         if (!is_depth_prepass) {
            pipeline = res->pipelines.sponza;
            pipeline_layout = res->pipelines.layout.base;
            pipeline_opacity = res->pipelines.sponza_opacity;
            pipeline_opacity_layout = res->pipelines.layout.opacity;
            tex_set = res->pipelines.descr.obj_tex_set;
         } else {
            pipeline = res->pipelines.depth_prepass;
            pipeline_layout = res->pipelines.layout.depth_prepass;
            pipeline_opacity = res->pipelines.depth_prepass_opacity;
            pipeline_opacity_layout =
               res->pipelines.layout.depth_prepass_opacity;
            tex_set = res->pipelines.descr.depth_prepass_tex_set;
         }

         record_instanced_draw(
               cmd_buf,
               pipeline,
               pipeline_opacity,
               res->sponza_model,
               res->sponza_mesh_visible,
               set_info->count, set_info->start_index,
               pipeline_layout,
               pipeline_opacity_layout,
               descriptor_set_count,
               tex_set,
               is_depth_prepass);
         continue;
      }

      assert(!"unkown object category");
   }
}

static void
record_gbuffer_scene_commands(VkdfContext *ctx, VkCommandBuffer cmd_buf,
                              GHashTable *sets, bool is_dynamic,
                              bool is_depth_prepass, void *data)
{
   assert(ENABLE_DEFERRED_RENDERING);

   SceneResources *res = (SceneResources *) data;

   // Push constants: camera projection matrix
   struct PCBData pcb_data;
   glm::mat4 *proj = vkdf_camera_get_projection_ptr(res->scene->camera);
   memcpy(&pcb_data.proj, &(*proj)[0][0], sizeof(pcb_data.proj));

   uint32_t descriptor_set_count;
   if (!is_depth_prepass) {
      vkCmdPushConstants(cmd_buf,
                         res->pipelines.layout.gbuffer_base,
                         VK_SHADER_STAGE_VERTEX_BIT,
                         0, sizeof(pcb_data), &pcb_data);

      // Bind descriptor sets for the camera view matrix and the scene static
      // object UBO data.
      VkDescriptorSet descriptor_sets[] = {
         res->pipelines.descr.camera_view_set,
         res->pipelines.descr.obj_set,
         res->pipelines.descr.light_set,
      };

      descriptor_set_count =
         sizeof(descriptor_sets) / sizeof(descriptor_sets[0]);

      vkCmdBindDescriptorSets(cmd_buf,
                              VK_PIPELINE_BIND_POINT_GRAPHICS,
                              res->pipelines.layout.gbuffer_base,
                              0,                      // First decriptor set
                              descriptor_set_count,   // Descriptor set count
                              descriptor_sets,        // Descriptor sets
                              0,                      // Dynamic offset count
                              NULL);                  // Dynamic offsets
   } else {
      vkCmdPushConstants(cmd_buf,
                         res->pipelines.layout.depth_prepass,
                         VK_SHADER_STAGE_VERTEX_BIT,
                         0, sizeof(pcb_data), &pcb_data);

      VkDescriptorSet descriptor_sets[] = {
         res->pipelines.descr.camera_view_set,
         res->pipelines.descr.obj_set,
      };

      descriptor_set_count =
         sizeof(descriptor_sets) / sizeof(descriptor_sets[0]);

      vkCmdBindDescriptorSets(cmd_buf,
                              VK_PIPELINE_BIND_POINT_GRAPHICS,
                              res->pipelines.layout.depth_prepass,
                              0,                      // First decriptor set
                              descriptor_set_count,   // Descriptor set count
                              descriptor_sets,        // Descriptor sets
                              0,                      // Dynamic offset count
                              NULL);                  // Dynamic offsets
   }

   // Render objects
   VkPipeline pipeline, pipeline_opacity;
   VkPipelineLayout pipeline_layout, pipeline_opacity_layout;
   VkDescriptorSet *tex_set;
   char *set_id;
   VkdfSceneSetInfo *set_info;
   GHashTableIter iter;
   g_hash_table_iter_init(&iter, sets);
   while (g_hash_table_iter_next(&iter, (void **)&set_id, (void **)&set_info)) {
      if (!set_info || set_info->count == 0)
         continue;

      if (!strcmp(set_id, "sponza")) {
         if (!is_depth_prepass) {
            pipeline = res->pipelines.sponza;
            pipeline_layout = res->pipelines.layout.gbuffer_base;
            pipeline_opacity = res->pipelines.sponza_opacity;
            pipeline_opacity_layout = res->pipelines.layout.gbuffer_opacity;
            tex_set = res->pipelines.descr.obj_tex_set;
         } else {
            pipeline = res->pipelines.depth_prepass;
            pipeline_layout = res->pipelines.layout.depth_prepass;
            pipeline_opacity = res->pipelines.depth_prepass_opacity;
            pipeline_opacity_layout =
               res->pipelines.layout.depth_prepass_opacity;
            tex_set = res->pipelines.descr.depth_prepass_tex_set;
         }

         record_instanced_draw(
            cmd_buf,
            pipeline,
            pipeline_opacity,
            res->sponza_model,
            res->sponza_mesh_visible,
            set_info->count, set_info->start_index,
            pipeline_layout,
            pipeline_opacity_layout,
            descriptor_set_count,
            tex_set,
            is_depth_prepass);
         continue;
      }

      assert(!"unkown object category");
   }
}

static void
record_gbuffer_merge_commands(VkdfContext *ctx,
                              VkCommandBuffer cmd_buf,
                              void *data)
{
   assert(ENABLE_DEFERRED_RENDERING);

   SceneResources *res = (SceneResources *) data;

   // Bind descriptor sets
   VkDescriptorSet descriptor_sets[] = {
      res->pipelines.descr.light_set,
      res->pipelines.descr.shadow_map_sampler_set,
      res->pipelines.descr.gbuffer_tex_set
   };

   vkCmdBindDescriptorSets(cmd_buf,
                           VK_PIPELINE_BIND_POINT_GRAPHICS,
                           res->pipelines.layout.gbuffer_merge,
                           0,                        // First decriptor set
                           3,                        // Descriptor set count
                           descriptor_sets,          // Descriptor sets
                           0,                        // Dynamic offset count
                           NULL);                    // Dynamic offsets

   // Bind pipeline
   vkCmdBindPipeline(cmd_buf,
                     VK_PIPELINE_BIND_POINT_GRAPHICS,
                     res->pipelines.gbuffer_merge);

   // Draw 4 vertices for the full-screen quad
   vkCmdDraw(cmd_buf, 4, 1, 0, 0);
}

static void
update_camera(SceneResources *res)
{
   const float mov_speed = 0.15f;
   const float rot_speed = 1.0f;

   VkdfCamera *cam = vkdf_scene_get_camera(res->scene);
   GLFWwindow *window = res->ctx->window;

   float base_speed = 1.0f;

   // Rotation
   if (glfwGetKey(window, GLFW_KEY_LEFT) != GLFW_RELEASE)
      vkdf_camera_rotate(cam, 0.0f, base_speed * rot_speed, 0.0f);
   else if (glfwGetKey(window, GLFW_KEY_RIGHT) != GLFW_RELEASE)
      vkdf_camera_rotate(cam, 0.0f, -base_speed * rot_speed, 0.0f);

   if (glfwGetKey(window, GLFW_KEY_PAGE_UP) != GLFW_RELEASE)
      vkdf_camera_rotate(cam, base_speed * rot_speed, 0.0f, 0.0f);
   else if (glfwGetKey(window, GLFW_KEY_PAGE_DOWN) != GLFW_RELEASE)
      vkdf_camera_rotate(cam, -base_speed * rot_speed, 0.0f, 0.0f);

   // Stepping
   if (glfwGetKey(window, GLFW_KEY_UP) != GLFW_RELEASE) {
      float step_speed = base_speed * mov_speed;
      vkdf_camera_step(cam, step_speed, 1, 1, 1);
   } else if (glfwGetKey(window, GLFW_KEY_DOWN) != GLFW_RELEASE) {
      float step_speed = -base_speed * mov_speed;
      vkdf_camera_step(cam, step_speed, 1, 1, 1);
   }
}

static void
scene_update(void *data)
{
   SceneResources *res = (SceneResources *) data;
   update_camera(res);
   if (ENABLE_CLIPPING)
      update_visible_sponza_meshes(res);
}

static void
init_scene(SceneResources *res)
{
   VkdfContext *ctx = res->ctx;

   res->camera = vkdf_camera_new(-20.0f, 3.0f, -1.0f,
                                 0.0f, 180.0f, 0.0f,
                                 45.0f, 0.1f, 500.0f, WIN_WIDTH / WIN_HEIGHT);

   vkdf_camera_look_at(res->camera, 10.0f, 5.0f, 0.0f);

   glm::vec3 scene_origin = glm::vec3(-100.0f, -100.0f, -100.0f);
   glm::vec3 scene_size = glm::vec3(200.0f, 200.0f, 200.0f);
   glm::vec3 tile_size = glm::vec3(200.0f, 200.0f, 200.0f);
   uint32_t cache_size = 0;
   res->scene = vkdf_scene_new(ctx,
                               res->camera,
                               scene_origin, scene_size, tile_size, 1,
                               cache_size, 1);

   vkdf_scene_set_scene_callbacks(res->scene,
                                  scene_update,
                                  record_update_resources_command,
                                  ENABLE_DEFERRED_RENDERING ?
                                     record_gbuffer_scene_commands :
                                     record_forward_scene_commands,
                                  SHOW_DEBUG_TILE ? postprocess_draw : NULL,
                                  res);

   VkClearValue color_clear;
   vkdf_color_clear_set(&color_clear, glm::vec4(0.2f, 0.4f, 0.8f, 1.0f));

   VkClearValue depth_clear;
   vkdf_depth_stencil_clear_set(&depth_clear, 1.0f, 0);

   /* For deferred rendering we skip color clearing. Pixels not rendered during
    * the gbuffer pass will be rendered in the clear color in the shader
    * directly, saving us a full render target clear per frame.
    */
   vkdf_scene_set_clear_values(res->scene,
                               ENABLE_DEFERRED_RENDERING ? NULL : &color_clear,
                               &depth_clear);

   glm::vec4 direction = glm::vec4(1.0f, -4.5f, -1.25f, 0.0f);
   glm::vec4 diffuse = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
   glm::vec4 ambient = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);
   glm::vec4 specular = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);

   VkdfLight *light =
      vkdf_light_new_directional(direction, diffuse, ambient, specular);

   vkdf_light_enable_shadows(light, true);

   VkdfSceneShadowSpec shadow_spec;
   vkdf_scene_shadow_spec_set(&shadow_spec, 4096, 5.0f, 110.0f, 1.0f, 2.0f, 2);

   vkdf_scene_add_light(res->scene, light, &shadow_spec);

   if (ENABLE_DEPTH_PREPASS)
      vkdf_scene_enable_depth_prepass(res->scene);

   if (ENABLE_DEFERRED_RENDERING) {
      /* 0: Eye position          : rgba16f
       * 1: Eye normal            : rgba16f
       * 2: Eye light position    : rgba16f
       * 3: Light space position  : rgba32f
       * 4: Diffuse color         : rgba8
       * 5: Specular color        : rgba8
       *
       * We encode material shininess in the alpha component of the normal,
       * we don't use specular's alpha because rgba_unorm isn't good for
       * it.
       */
      vkdf_scene_enable_deferred_rendering(res->scene,
                                           record_gbuffer_merge_commands,
                                           6,
                                           VK_FORMAT_R16G16B16A16_SFLOAT,
                                           VK_FORMAT_R16G16B16A16_SFLOAT,
                                           VK_FORMAT_R16G16B16A16_SFLOAT,
                                           VK_FORMAT_R32G32B32A32_SFLOAT,
                                           VK_FORMAT_R8G8B8A8_UNORM,
                                           VK_FORMAT_R8G8B8A8_UNORM);
   }

   if (ENABLE_SSAO) {
      vkdf_scene_enable_ssao(res->scene,
                             SSAO_DOWNSAMPLING,
                             SSAO_NUM_SAMPLES,
                             SSAO_RADIUS,
                             SSAO_BIAS,
                             SSAO_INTENSITY,
                             SSAO_BLUR_SIZE);
   }
}

static void
create_sponza_texture_descriptor_sets(SceneResources *res)
{
   res->sponza_sampler =
         vkdf_create_sampler(res->ctx,
                             VK_SAMPLER_ADDRESS_MODE_REPEAT,
                             VK_FILTER_LINEAR,
                             VK_SAMPLER_MIPMAP_MODE_LINEAR,
                             MAX_ANISOTROPY);

   VkdfModel *model = res->sponza_model;
   assert(model->tex_materials.size() == model->materials.size());

   for (uint32_t i = 0; i < model->materials.size(); i++) {
      VkdfMaterial *m = &model->materials[i];

      VkdfTexMaterial *tm = &model->tex_materials[i];

      // We have a single shader that handles both solid+texture materials
      // and also solid-only materials. This means the shader always has
      // sampler bindings and these need to be valid even if the material
      // for the mesh we're rendering doesn't have any actual textures
      // so just bind the texture from a textured material
      if (m->opacity_tex_count == 0) {
         res->pipelines.descr.obj_tex_set[i] =
            create_descriptor_set(res->ctx,
                                  res->descriptor_pool.sampler_pool,
                                  res->pipelines.descr.obj_tex_layout);
      } else {
         res->pipelines.descr.obj_tex_set[i] =
            create_descriptor_set(res->ctx,
                                  res->descriptor_pool.sampler_pool,
                                  res->pipelines.descr.obj_tex_opacity_layout);
      }

      if (ENABLE_DEPTH_PREPASS) {
         if (m->opacity_tex_count > 0) {
            res->pipelines.descr.depth_prepass_tex_set[i] =
               create_descriptor_set(res->ctx,
                                     res->descriptor_pool.sampler_pool,
                                     res->pipelines.descr.depth_prepass_tex_layout);
         }
      }

      if (m->diffuse_tex_count > 0) {
         assert(tm->diffuse.view);
         vkdf_descriptor_set_sampler_update(res->ctx,
                                            res->pipelines.descr.obj_tex_set[i],
                                            res->sponza_sampler,
                                            tm->diffuse.view,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                            DIFFUSE_TEX_BINDING, 1);
      } else {
         vkdf_info("Material %u doesn't have a diffuse texture\n", i);
         vkdf_descriptor_set_sampler_update(res->ctx,
                                            res->pipelines.descr.obj_tex_set[i],
                                            res->sponza_sampler,
                                            model->tex_materials[16].diffuse.view,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                            DIFFUSE_TEX_BINDING, 1);
      }

      if (m->specular_tex_count > 0) {
         assert(tm->specular.view);
         vkdf_descriptor_set_sampler_update(res->ctx,
                                            res->pipelines.descr.obj_tex_set[i],
                                            res->sponza_sampler,
                                            tm->specular.view,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                            SPECULAR_TEX_BINDING, 1);
      } else {
         vkdf_info("Material %u doesn't have a specular texture\n", i);
         vkdf_descriptor_set_sampler_update(res->ctx,
                                            res->pipelines.descr.obj_tex_set[i],
                                            res->sponza_sampler,
                                            model->tex_materials[1].normal.view,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                            SPECULAR_TEX_BINDING, 1);
      }

      if (m->normal_tex_count > 0) {
         assert(tm->normal.view);
         vkdf_descriptor_set_sampler_update(res->ctx,
                                            res->pipelines.descr.obj_tex_set[i],
                                            res->sponza_sampler,
                                            tm->normal.view,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                            NORMAL_TEX_BINDING, 1);
      } else {
         vkdf_info("Material %u doesn't have a normal texture\n", i);
         vkdf_descriptor_set_sampler_update(res->ctx,
                                            res->pipelines.descr.obj_tex_set[i],
                                            res->sponza_sampler,
                                            model->tex_materials[1].normal.view,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                            NORMAL_TEX_BINDING, 1);
      }

      if (m->opacity_tex_count > 0) {
         assert(tm->opacity.view);
         vkdf_descriptor_set_sampler_update(res->ctx,
                                            res->pipelines.descr.obj_tex_set[i],
                                            res->sponza_sampler,
                                            tm->opacity.view,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                            OPACITY_TEX_BINDING, 1);

         if (ENABLE_DEPTH_PREPASS) {
            vkdf_descriptor_set_sampler_update(res->ctx,
                                               res->pipelines.descr.depth_prepass_tex_set[i],
                                               res->sponza_sampler,
                                               tm->opacity.view,
                                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                               0, 1);
         }
      }
   }
}

static void
init_pipeline_descriptors(SceneResources *res,
                          bool deferred,
                          bool depth_prepass)
{
   if (res->pipelines.layout.base)
      return;

   /* Push constant range */
   VkPushConstantRange pcb_range;
   pcb_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
   pcb_range.offset = 0;
   pcb_range.size = sizeof(PCBData);

   VkPushConstantRange pcb_ranges[] = {
      pcb_range,
   };

   /* Descriptor set layouts */
   res->pipelines.descr.camera_view_layout =
      vkdf_create_ubo_descriptor_set_layout(res->ctx, 0, 1,
                                            VK_SHADER_STAGE_VERTEX_BIT,
                                            false);

   res->pipelines.descr.obj_layout =
      vkdf_create_ubo_descriptor_set_layout(res->ctx, 0, 2,
                                            VK_SHADER_STAGE_VERTEX_BIT |
                                               VK_SHADER_STAGE_FRAGMENT_BIT,
                                            false);

   res->pipelines.descr.obj_tex_layout =
      vkdf_create_sampler_descriptor_set_layout(res->ctx,
                                                0, 3,
                                                VK_SHADER_STAGE_FRAGMENT_BIT);

   res->pipelines.descr.obj_tex_opacity_layout =
      vkdf_create_sampler_descriptor_set_layout(res->ctx,
                                                0, 4,
                                                VK_SHADER_STAGE_FRAGMENT_BIT);

   if (depth_prepass) {
      res->pipelines.descr.depth_prepass_tex_layout =
         vkdf_create_sampler_descriptor_set_layout(res->ctx,
                                                   0, 1,
                                                   VK_SHADER_STAGE_FRAGMENT_BIT);
   }

   res->pipelines.descr.light_layout =
      vkdf_create_ubo_descriptor_set_layout(res->ctx, 0, 2,
                                            VK_SHADER_STAGE_VERTEX_BIT |
                                               VK_SHADER_STAGE_FRAGMENT_BIT,
                                            false);

   res->pipelines.descr.shadow_map_sampler_layout =
      vkdf_create_sampler_descriptor_set_layout(res->ctx, 0, 1,
                                                VK_SHADER_STAGE_FRAGMENT_BIT);

   if (!deferred) {
      /* Base pipeline layout (for forward opaque meshes) */
      VkDescriptorSetLayout layouts[] = {
         res->pipelines.descr.camera_view_layout,
         res->pipelines.descr.obj_layout,
         res->pipelines.descr.light_layout,
         res->pipelines.descr.shadow_map_sampler_layout,
         res->pipelines.descr.obj_tex_layout,
      };

      VkPipelineLayoutCreateInfo pipeline_layout_info;
      pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
      pipeline_layout_info.pNext = NULL;
      pipeline_layout_info.pushConstantRangeCount = 1;
      pipeline_layout_info.pPushConstantRanges = pcb_ranges;
      pipeline_layout_info.setLayoutCount = 5;
      pipeline_layout_info.pSetLayouts = layouts;
      pipeline_layout_info.flags = 0;

      VK_CHECK(vkCreatePipelineLayout(res->ctx->device,
                                      &pipeline_layout_info,
                                      NULL,
                                      &res->pipelines.layout.base));

      /* Opacity pipeline (for forward meshes with opacity textures) */
      layouts[4] = res->pipelines.descr.obj_tex_opacity_layout;
      VK_CHECK(vkCreatePipelineLayout(res->ctx->device,
                                      &pipeline_layout_info,
                                      NULL,
                                      &res->pipelines.layout.opacity));
   } else {
      /* Base pipeline layout (for deferred opaque meshes) */
      VkDescriptorSetLayout layouts[] = {
         res->pipelines.descr.camera_view_layout,
         res->pipelines.descr.obj_layout,
         res->pipelines.descr.light_layout,
         res->pipelines.descr.obj_tex_layout,
      };

      VkPipelineLayoutCreateInfo pipeline_layout_info;
      pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
      pipeline_layout_info.pNext = NULL;
      pipeline_layout_info.pushConstantRangeCount = 1;
      pipeline_layout_info.pPushConstantRanges = pcb_ranges;
      pipeline_layout_info.setLayoutCount = 4;
      pipeline_layout_info.pSetLayouts = layouts;
      pipeline_layout_info.flags = 0;

      VK_CHECK(vkCreatePipelineLayout(res->ctx->device,
                                      &pipeline_layout_info,
                                      NULL,
                                      &res->pipelines.layout.gbuffer_base));

      /* Opacity pipeline (for forward meshes with opacity textures) */
      layouts[3] = res->pipelines.descr.obj_tex_opacity_layout;
      VK_CHECK(vkCreatePipelineLayout(res->ctx->device,
                                      &pipeline_layout_info,
                                      NULL,
                                      &res->pipelines.layout.gbuffer_opacity));
   }

   /* Descriptor sets */

   /* Camera view matrix */
   res->pipelines.descr.camera_view_set =
      create_descriptor_set(res->ctx,
                            res->descriptor_pool.static_ubo_pool,
                            res->pipelines.descr.camera_view_layout);

   VkDeviceSize ubo_offset = 0;
   VkDeviceSize ubo_size = res->ubos.camera_view.size;
   vkdf_descriptor_set_buffer_update(res->ctx,
                                     res->pipelines.descr.camera_view_set,
                                     res->ubos.camera_view.buf.buf,
                                     0, 1, &ubo_offset, &ubo_size, false, true);

   /* Object data */
   res->pipelines.descr.obj_set =
      create_descriptor_set(res->ctx,
                            res->descriptor_pool.static_ubo_pool,
                            res->pipelines.descr.obj_layout);

   VkdfBuffer *obj_ubo = vkdf_scene_get_dynamic_object_ubo(res->scene);
   VkDeviceSize obj_ubo_size = vkdf_scene_get_dynamic_object_ubo_size(res->scene);
   ubo_offset = 0;
   ubo_size = obj_ubo_size;
   vkdf_descriptor_set_buffer_update(res->ctx,
                                     res->pipelines.descr.obj_set,
                                     obj_ubo->buf,
                                     0, 1, &ubo_offset, &ubo_size, false, true);

   VkdfBuffer *material_ubo = vkdf_scene_get_dynamic_material_ubo(res->scene);
   VkDeviceSize material_ubo_size =
      vkdf_scene_get_dynamic_material_ubo_size(res->scene);
   ubo_offset = 0;
   ubo_size = material_ubo_size;
   vkdf_descriptor_set_buffer_update(res->ctx,
                                     res->pipelines.descr.obj_set,
                                     material_ubo->buf,
                                     1, 1, &ubo_offset, &ubo_size, false, true);

   /* Light and shadow map descriptions */
   res->pipelines.descr.light_set =
      create_descriptor_set(res->ctx,
                            res->descriptor_pool.static_ubo_pool,
                            res->pipelines.descr.light_layout);

   VkdfBuffer *light_ubo = vkdf_scene_get_light_ubo(res->scene);
   vkdf_scene_get_light_ubo_range(res->scene, &ubo_offset, &ubo_size);
   vkdf_descriptor_set_buffer_update(res->ctx,
                                     res->pipelines.descr.light_set,
                                     light_ubo->buf,
                                     0, 1, &ubo_offset, &ubo_size, false, true);

   vkdf_scene_get_shadow_map_ubo_range(res->scene, &ubo_offset, &ubo_size);
   vkdf_descriptor_set_buffer_update(res->ctx,
                                     res->pipelines.descr.light_set,
                                     light_ubo->buf,
                                     1, 1, &ubo_offset, &ubo_size, false, true);

   /* Samplers for the sponza model textures (one set per mesh) */
   create_sponza_texture_descriptor_sets(res);

   /* Shadow map sampler */
   res->pipelines.descr.shadow_map_sampler_set =
      create_descriptor_set(res->ctx,
                            res->descriptor_pool.sampler_pool,
                            res->pipelines.descr.shadow_map_sampler_layout);

   VkSampler shadow_map_sampler =
      vkdf_scene_light_get_shadow_map_sampler(res->scene, 0);

   VkdfImage *shadow_map_image =
      vkdf_scene_light_get_shadow_map_image(res->scene, 0);

   vkdf_descriptor_set_sampler_update(res->ctx,
                                      res->pipelines.descr.shadow_map_sampler_set,
                                      shadow_map_sampler,
                                      shadow_map_image->view,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                      0, 1);

   if (deferred) {
      /* Gbuffer textures */
      uint32_t gbuffer_size =
         res->scene->rt.gbuffer_size + (res->scene->ssao.enabled ? 1 : 0);

      res->pipelines.descr.gbuffer_tex_layout =
         vkdf_create_sampler_descriptor_set_layout(res->ctx, 0,
                                                   gbuffer_size,
                                                   VK_SHADER_STAGE_FRAGMENT_BIT);

      res->pipelines.descr.gbuffer_tex_set =
         create_descriptor_set(res->ctx,
                               res->descriptor_pool.sampler_pool,
                               res->pipelines.descr.gbuffer_tex_layout);

      res->gbuffer_sampler =
         vkdf_create_sampler(res->ctx,
                             VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                             VK_FILTER_NEAREST,
                             VK_SAMPLER_MIPMAP_MODE_NEAREST,
                             0.0f);

      uint32_t tex_idx = 0;
      for (; tex_idx < res->scene->rt.gbuffer_size; tex_idx++) {
         VkdfImage *image = vkdf_scene_get_gbuffer_image(res->scene, tex_idx);
         vkdf_descriptor_set_sampler_update(res->ctx,
                                            res->pipelines.descr.gbuffer_tex_set,
                                            res->gbuffer_sampler,
                                            image->view,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                            tex_idx, 1);
      }

      if (res->scene->ssao.enabled) {
         VkdfImage *ssao_image = vkdf_scene_get_ssao_image(res->scene);
         res->ssao_sampler =
            vkdf_ssao_create_ssao_sampler(res->ctx, SSAO_FILTER);
         vkdf_descriptor_set_sampler_update(res->ctx,
                                            res->pipelines.descr.gbuffer_tex_set,
                                            res->ssao_sampler,
                                            ssao_image->view,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                            tex_idx, 1);
      }

      /* Gbuffer merge pipeline layout */
      VkDescriptorSetLayout gbuffer_merge_layouts[] = {
         res->pipelines.descr.light_layout,
         res->pipelines.descr.shadow_map_sampler_layout,
         res->pipelines.descr.gbuffer_tex_layout
      };

      VkPipelineLayoutCreateInfo pipeline_layout_info;
      pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
      pipeline_layout_info.pNext = NULL;
      pipeline_layout_info.pushConstantRangeCount = 0;
      pipeline_layout_info.pPushConstantRanges = NULL;
      pipeline_layout_info.setLayoutCount = 3;
      pipeline_layout_info.pSetLayouts = gbuffer_merge_layouts;
      pipeline_layout_info.flags = 0;

      VK_CHECK(vkCreatePipelineLayout(res->ctx->device,
                                      &pipeline_layout_info,
                                      NULL,
                                      &res->pipelines.layout.gbuffer_merge));
   }

   if (depth_prepass) {
      VkDescriptorSetLayout depth_prepass_layouts[] = {
         res->pipelines.descr.camera_view_layout,
         res->pipelines.descr.obj_layout,
      };

      VkPipelineLayoutCreateInfo pipeline_layout_info;
      pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
      pipeline_layout_info.pNext = NULL;
      pipeline_layout_info.pushConstantRangeCount = 1;
      pipeline_layout_info.pPushConstantRanges = pcb_ranges;
      pipeline_layout_info.setLayoutCount = 2;
      pipeline_layout_info.pSetLayouts = depth_prepass_layouts;
      pipeline_layout_info.flags = 0;

      VK_CHECK(vkCreatePipelineLayout(res->ctx->device,
                                      &pipeline_layout_info,
                                      NULL,
                                      &res->pipelines.layout.depth_prepass));

      VkDescriptorSetLayout depth_prepass_opacity_layouts[] = {
         res->pipelines.descr.camera_view_layout,
         res->pipelines.descr.obj_layout,
         res->pipelines.descr.depth_prepass_tex_layout
      };

      pipeline_layout_info.setLayoutCount = 3;
      pipeline_layout_info.pSetLayouts = depth_prepass_opacity_layouts;

      VK_CHECK(vkCreatePipelineLayout(res->ctx->device,
                                      &pipeline_layout_info,
                                      NULL,
                                      &res->pipelines.layout.depth_prepass_opacity));
   }
}

static void
create_forward_pipelines(SceneResources *res,
                         uint32_t num_vi_bindings,
                         VkVertexInputBindingDescription *vi_bindings,
                         uint32_t num_vi_attribs,
                         VkVertexInputAttributeDescription *vi_attribs)
{
   /* FIXME: In theory we need to create different pipelines for static and
    * dynamic objects since we have different render passes associated
    * with them, they are compatible from the POV of the pipeline though...
    */
   VkRenderPass renderpass = vkdf_scene_get_static_render_pass(res->scene);

   res->pipelines.sponza =
      vkdf_create_gfx_pipeline(res->ctx,
                               NULL,
                               1,
                               vi_bindings,
                               6,
                               vi_attribs,
                               true,
                               ENABLE_DEPTH_PREPASS ?
                                  VK_COMPARE_OP_EQUAL : VK_COMPARE_OP_LESS,
                               renderpass,
                               res->pipelines.layout.base,
                               VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                               VK_CULL_MODE_BACK_BIT,
                               1,
                               res->shaders.obj_forward.vs,
                               res->shaders.obj_forward.fs);

   res->pipelines.sponza_opacity =
      vkdf_create_gfx_pipeline(res->ctx,
                               NULL,
                               1,
                               vi_bindings,
                               6,
                               vi_attribs,
                               true,
                               ENABLE_DEPTH_PREPASS ?
                                  VK_COMPARE_OP_EQUAL : VK_COMPARE_OP_LESS,
                               renderpass,
                               res->pipelines.layout.opacity,
                               VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                               VK_CULL_MODE_BACK_BIT,
                               1,
                               res->shaders.obj_forward.vs,
                               res->shaders.obj_forward.fs_opacity);
}

static inline VkPipeline
create_gbuffer_pipeline(VkdfContext *ctx,
                        VkPipelineCache *pipeline_cache,
                        uint32_t num_vi_bindings,
                        VkVertexInputBindingDescription *vi_bindings,
                        uint32_t num_vi_attribs,
                        VkVertexInputAttributeDescription *vi_attribs,
                        VkRenderPass render_pass,
                        uint32_t gbuffer_size,
                        VkPipelineLayout pipeline_layout,
                        VkPrimitiveTopology primitive,
                        VkCullModeFlagBits cull_mode,
                        VkShaderModule vs_module,
                        VkShaderModule fs_module)
{
   VkPipeline pipeline =
      vkdf_create_gfx_pipeline(ctx,
                               pipeline_cache,
                               num_vi_bindings,
                               vi_bindings,
                               num_vi_attribs,
                               vi_attribs,
                               true,
                               ENABLE_DEPTH_PREPASS ?
                                  VK_COMPARE_OP_EQUAL : VK_COMPARE_OP_LESS,
                               render_pass,
                               pipeline_layout,
                               primitive,
                               cull_mode,
                               gbuffer_size,
                               vs_module,
                               fs_module);
   return pipeline;
}

static inline VkPipeline
create_gbuffer_merge_pipeline(SceneResources *res, bool use_ssao)
{
   const VkRenderPass renderpass =
      vkdf_scene_get_gbuffer_merge_render_pass(res->scene);

   VkShaderModule fs = use_ssao ? res->shaders.gbuffer_merge.fs_ssao :
                                  res->shaders.gbuffer_merge.fs;

   VkPipeline pipeline =
      vkdf_create_gfx_pipeline(res->ctx,
                               NULL,
                               0,
                               NULL,
                               0,
                               NULL,
                               true,
                               VK_COMPARE_OP_EQUAL,
                               renderpass,
                               res->pipelines.layout.gbuffer_merge,
                               VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
                               VK_CULL_MODE_BACK_BIT,
                               1,
                               res->shaders.gbuffer_merge.vs,
                               fs);
   return pipeline;
}

static void
create_deferred_pipelines(SceneResources *res,
                          uint32_t num_vi_bindings,
                          VkVertexInputBindingDescription *vi_bindings,
                          uint32_t num_vi_attribs,
                          VkVertexInputAttributeDescription *vi_attribs)
{
   VkRenderPass renderpass = vkdf_scene_get_static_render_pass(res->scene);

   res->pipelines.sponza =
      create_gbuffer_pipeline(res->ctx, NULL,
                              num_vi_bindings, vi_bindings,
                              num_vi_attribs, vi_attribs,
                              renderpass,
                              res->scene->rt.gbuffer_size,
                              res->pipelines.layout.gbuffer_base,
                              VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                              VK_CULL_MODE_BACK_BIT,
                              res->shaders.obj_gbuffer.vs,
                              res->shaders.obj_gbuffer.fs);

   res->pipelines.sponza_opacity =
      create_gbuffer_pipeline(res->ctx, NULL,
                              num_vi_bindings, vi_bindings,
                              num_vi_attribs, vi_attribs,
                              renderpass,
                              res->scene->rt.gbuffer_size,
                              res->pipelines.layout.gbuffer_opacity,
                              VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                              VK_CULL_MODE_BACK_BIT,
                              res->shaders.obj_gbuffer.vs,
                              res->shaders.obj_gbuffer.fs_opacity);

   res->pipelines.gbuffer_merge =
      create_gbuffer_merge_pipeline(res, res->scene->ssao.enabled);
}

static void
create_depth_prepass_pipelines(SceneResources *res)
{
   const VkRenderPass renderpass =
      vkdf_scene_get_depth_prepass_static_render_pass(res->scene);

   // Base pipeline
   VkVertexInputBindingDescription vi_binding[1];
   VkVertexInputAttributeDescription vi_attribs[2];

   uint32_t stride =
      vkdf_mesh_get_vertex_data_stride(res->sponza_model->meshes[0]);
   vkdf_vertex_binding_set(&vi_binding[0],
                           0, VK_VERTEX_INPUT_RATE_VERTEX, stride);

   /* binding 0, location 0: position */
   vkdf_vertex_attrib_set(&vi_attribs[0], 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0);

   res->pipelines.depth_prepass =
      vkdf_create_gfx_pipeline(res->ctx,
                               NULL,
                               1,
                               vi_binding,
                               1,
                               vi_attribs,
                               true,
                               VK_COMPARE_OP_LESS,
                               renderpass,
                               res->pipelines.layout.depth_prepass,
                               VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                               VK_CULL_MODE_BACK_BIT,
                               0,
                               res->shaders.depth_prepass.vs,
                               NULL);

   // Opacity pipeline (needs UV attribute & fragment shader)

   /* binding 0, location 1: UV coords */
   vkdf_vertex_attrib_set(&vi_attribs[1], 0, 1, VK_FORMAT_R32G32_SFLOAT, 48);

   res->pipelines.depth_prepass_opacity =
      vkdf_create_gfx_pipeline(res->ctx,
                               NULL,
                               1,
                               vi_binding,
                               2,
                               vi_attribs,
                               true,
                               VK_COMPARE_OP_LESS,
                               renderpass,
                               res->pipelines.layout.depth_prepass_opacity,
                               VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                               VK_CULL_MODE_BACK_BIT,
                               0,
                               res->shaders.depth_prepass.vs_opacity,
                               res->shaders.depth_prepass.fs_opacity);
}

static void
init_sponza_pipelines(SceneResources *res)
{
   VkVertexInputBindingDescription vi_bindings[1];
   VkVertexInputAttributeDescription vi_attribs[6];

   // Vertex attribute binding 0: position, normal, material
   uint32_t stride =
      vkdf_mesh_get_vertex_data_stride(res->sponza_model->meshes[0]);
   vkdf_vertex_binding_set(&vi_bindings[0],
                           0, VK_VERTEX_INPUT_RATE_VERTEX, stride);

   /* binding 0, location 0: position
    * binding 0, location 1: normal
    * binding 0, location 2: tangent
    * binding 0, location 3: bitangent
    * binding 0, location 4: uv
    * binding 0, location 5: material idx
    */
   vkdf_vertex_attrib_set(&vi_attribs[0], 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0);
   vkdf_vertex_attrib_set(&vi_attribs[1], 0, 1, VK_FORMAT_R32G32B32_SFLOAT, 12);
   vkdf_vertex_attrib_set(&vi_attribs[2], 0, 2, VK_FORMAT_R32G32B32_SFLOAT, 24);
   vkdf_vertex_attrib_set(&vi_attribs[3], 0, 3, VK_FORMAT_R32G32B32_SFLOAT, 36);
   vkdf_vertex_attrib_set(&vi_attribs[4], 0, 4, VK_FORMAT_R32G32_SFLOAT, 48);
   vkdf_vertex_attrib_set(&vi_attribs[5], 0, 5, VK_FORMAT_R32_UINT, 56);

   if (!ENABLE_DEFERRED_RENDERING)
      create_forward_pipelines(res, 1, vi_bindings, 6, vi_attribs);
   else
      create_deferred_pipelines(res, 1, vi_bindings, 6, vi_attribs);

   if (ENABLE_DEPTH_PREPASS)
      create_depth_prepass_pipelines(res);
}

static void
init_cmd_bufs(SceneResources *res)
{
   if (!res->cmd_pool)
      res->cmd_pool = vkdf_create_gfx_command_pool(res->ctx, 0);
}

static void
init_shaders(SceneResources *res)
{
   // Depth prepass
   res->shaders.depth_prepass.vs =
      vkdf_create_shader_module(res->ctx, "obj.depthprepass.vert.spv");
   res->shaders.depth_prepass.vs_opacity =
      vkdf_create_shader_module(res->ctx, "obj.depthprepass.opacity.vert.spv");
   res->shaders.depth_prepass.fs_opacity =
      vkdf_create_shader_module(res->ctx, "obj.depthprepass.opacity.frag.spv");

   // Forward rendering
   res->shaders.obj_forward.vs = vkdf_create_shader_module(res->ctx, "obj.vert.spv");
   res->shaders.obj_forward.fs = vkdf_create_shader_module(res->ctx, "obj.frag.spv");
   res->shaders.obj_forward.fs_opacity =
      vkdf_create_shader_module(res->ctx, "obj_opacity.frag.spv");

   // Deferred rendering
   res->shaders.obj_gbuffer.vs =
      vkdf_create_shader_module(res->ctx, "obj.deferred.vert.spv");
   res->shaders.obj_gbuffer.fs =
      vkdf_create_shader_module(res->ctx, "obj.deferred.frag.spv");
   res->shaders.obj_gbuffer.fs_opacity =
      vkdf_create_shader_module(res->ctx, "obj_opacity.deferred.frag.spv");

   res->shaders.gbuffer_merge.vs =
      vkdf_create_shader_module(res->ctx, "gbuffer-merge.vert.spv");
   res->shaders.gbuffer_merge.fs =
      vkdf_create_shader_module(res->ctx, "gbuffer-merge.frag.spv");

   // SSAO (deferred)
   res->shaders.gbuffer_merge.fs_ssao =
      vkdf_create_shader_module(res->ctx, "gbuffer-merge.ssao.frag.spv");

   // Debug
   if (SHOW_DEBUG_TILE) {
      res->debug.shaders.vs =
         vkdf_create_shader_module(res->ctx, "debug-tile.vert.spv");
      res->debug.shaders.fs =
         vkdf_create_shader_module(res->ctx, "debug-tile.frag.spv");
   }
}

static inline void
init_pipelines(SceneResources *res)
{
   init_pipeline_descriptors(res,
                             ENABLE_DEFERRED_RENDERING,
                             ENABLE_DEPTH_PREPASS);
   init_sponza_pipelines(res);
}

static void
init_meshes(SceneResources *res)
{
   // Sponza model
   res->sponza_model = vkdf_model_load("./sponza.obj");
   vkdf_model_fill_vertex_buffers(res->ctx, res->sponza_model, true);
   vkdf_model_load_textures(res->ctx, res->cmd_pool, res->sponza_model);

   if (SHOW_SPONZA_FLAG_MESH == false)
      res->sponza_model->meshes[SPONZA_FLAG_MESH_IDX]->active = false;

   // Make all meshes visible by default
   memset(res->sponza_mesh_visible, 1, sizeof(res->sponza_mesh_visible));

   // 2D tile mesh, used for debug display
   res->tile_mesh = vkdf_2d_tile_mesh_new(res->ctx);
   vkdf_mesh_fill_vertex_buffer(res->ctx, res->tile_mesh);
}

static void
init_objects(SceneResources *res)
{
   glm::vec3 pos;
   VkdfObject *obj;

   pos = glm::vec3(0.0f, 0.0f, 0.0f);
   obj = vkdf_object_new_from_model(pos, res->sponza_model);
   vkdf_object_set_scale(obj, glm::vec3(0.02f, 0.02f, 0.02f));
   vkdf_object_set_material_idx_base(obj, 0);
   vkdf_object_set_lighting_behavior(obj, true, true);
   vkdf_object_set_dynamic(obj, true);
   vkdf_scene_add_object(res->scene, "sponza", obj);

   res->sponza_obj = obj;
}

static void
init_descriptor_pools(SceneResources *res)
{
   res->descriptor_pool.static_ubo_pool =
      vkdf_create_descriptor_pool(res->ctx,
                                  VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 8);

   res->descriptor_pool.sampler_pool =
      vkdf_create_descriptor_pool(res->ctx,
                                  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                  256);
}

static void
create_debug_tile_pipeline(SceneResources *res)
{
   // Pipeline layout: debug sampler descriptor set
   res->debug.pipeline.sampler_set_layout =
      vkdf_create_sampler_descriptor_set_layout(res->ctx,
                                                0, 1,
                                                VK_SHADER_STAGE_FRAGMENT_BIT);

   res->debug.pipeline.sampler_set =
      create_descriptor_set(res->ctx,
                            res->descriptor_pool.sampler_pool,
                            res->debug.pipeline.sampler_set_layout);

   res->debug.sampler =
         vkdf_create_sampler(res->ctx,
                             VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                             VK_FILTER_NEAREST,
                             VK_SAMPLER_MIPMAP_MODE_NEAREST,
                             0.0f);

   vkdf_descriptor_set_sampler_update(res->ctx,
                                      res->debug.pipeline.sampler_set,
                                      res->debug.sampler,
                                      res->debug.image.view,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                      0, 1);

   VkDescriptorSetLayout layouts[1] = {
      res->debug.pipeline.sampler_set_layout
   };

   VkPipelineLayoutCreateInfo pipeline_layout_info;
   pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
   pipeline_layout_info.pNext = NULL;
   pipeline_layout_info.pushConstantRangeCount = 0;
   pipeline_layout_info.pPushConstantRanges = NULL;
   pipeline_layout_info.setLayoutCount = 1;
   pipeline_layout_info.pSetLayouts = layouts;
   pipeline_layout_info.flags = 0;

   VK_CHECK(vkCreatePipelineLayout(res->ctx->device,
                                   &pipeline_layout_info,
                                   NULL,
                                   &res->debug.pipeline.layout));

   // Pipeline
   VkVertexInputBindingDescription vi_binding[1];
   VkVertexInputAttributeDescription vi_attribs[2];

   uint32_t stride =
      vkdf_mesh_get_vertex_data_stride(res->tile_mesh);
   vkdf_vertex_binding_set(&vi_binding[0],
                           0, VK_VERTEX_INPUT_RATE_VERTEX, stride);

   /* binding 0, location 0: position
    * binding 0, location 1: uv
    */
   vkdf_vertex_attrib_set(&vi_attribs[0], 0, 0, VK_FORMAT_R32G32_SFLOAT, 0);
   vkdf_vertex_attrib_set(&vi_attribs[1], 0, 1, VK_FORMAT_R32G32_SFLOAT, 12);

   res->debug.pipeline.pipeline =
      vkdf_create_gfx_pipeline(res->ctx,
                               NULL,
                               1,
                               vi_binding,
                               2,
                               vi_attribs,
                               false,
                               VK_COMPARE_OP_LESS,
                               res->debug.renderpass,
                               res->debug.pipeline.layout,
                               VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
                               VK_CULL_MODE_BACK_BIT,
                               1,
                               res->debug.shaders.vs,
                               res->debug.shaders.fs);
}

static void
record_debug_tile_cmd_buf(SceneResources *res)
{
   const VkdfMesh *mesh = res->tile_mesh;

   vkdf_create_command_buffer(res->ctx,
                              res->cmd_pool,
                              VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                              1, &res->debug.cmd_buf);

   vkdf_command_buffer_begin(res->debug.cmd_buf,
                             VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT);

   VkRenderPassBeginInfo rp_begin;
   rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
   rp_begin.pNext = NULL;
   rp_begin.renderPass = res->debug.renderpass;
   rp_begin.framebuffer = res->debug.framebuffer;
   rp_begin.renderArea.offset.x = 0;
   rp_begin.renderArea.offset.y = 0;
   rp_begin.renderArea.extent.width = res->ctx->width;
   rp_begin.renderArea.extent.height = res->ctx->height;
   rp_begin.clearValueCount = 0;
   rp_begin.pClearValues = NULL;

   vkCmdBeginRenderPass(res->debug.cmd_buf,
                        &rp_begin,
                        VK_SUBPASS_CONTENTS_INLINE);

   // Viewport and Scissor
   uint32_t width = res->ctx->width * 0.5;
   uint32_t height = res->ctx->height * 0.5;

   VkViewport viewport;
   viewport.width = width;
   viewport.height = height;
   viewport.minDepth = 0.0f;
   viewport.maxDepth = 1.0f;
   viewport.x = 0;
   viewport.y = 0;
   vkCmdSetViewport(res->debug.cmd_buf, 0, 1, &viewport);

   VkRect2D scissor;
   scissor.extent.width = width;
   scissor.extent.height = height;
   scissor.offset.x = 0;
   scissor.offset.y = 0;
   vkCmdSetScissor(res->debug.cmd_buf, 0, 1, &scissor);

   // Pipeline
   vkCmdBindPipeline(res->debug.cmd_buf,
                     VK_PIPELINE_BIND_POINT_GRAPHICS,
                     res->debug.pipeline.pipeline);

   // Vertex buffer: position, uv
   const VkDeviceSize offsets[1] = { 0 };
   vkCmdBindVertexBuffers(res->debug.cmd_buf,
                          0,                       // Start Binding
                          1,                       // Binding Count
                          &mesh->vertex_buf.buf,   // Buffers
                          offsets);                // Offsets

   // Descriptors
   vkCmdBindDescriptorSets(res->debug.cmd_buf,
                           VK_PIPELINE_BIND_POINT_GRAPHICS,
                           res->debug.pipeline.layout,
                           0,                                // First decriptor set
                           1,                                // Descriptor set count
                           &res->debug.pipeline.sampler_set, // Descriptor sets
                           0,                                // Dynamic offset count
                           NULL);                            // Dynamic offsets

   // Draw
   vkCmdDraw(res->debug.cmd_buf,
             mesh->vertices.size(),                // vertex count
             1,                                    // instance count
             0,                                    // first vertex
             0);                                   // first instance

   vkCmdEndRenderPass(res->debug.cmd_buf);

   vkdf_command_buffer_end(res->debug.cmd_buf);
}

static VkRenderPass
create_debug_tile_renderpass(SceneResources *res)
{
   VkAttachmentDescription attachments[1];

   attachments[0].format = res->ctx->surface_format;
   attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
   attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
   attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
   attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
   attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
   attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
   attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
   attachments[0].flags = 0;

   VkAttachmentReference color_ref;
   color_ref.attachment = 0;
   color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

   VkSubpassDescription subpass[1];
   subpass[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
   subpass[0].flags = 0;
   subpass[0].inputAttachmentCount = 0;
   subpass[0].pInputAttachments = NULL;
   subpass[0].colorAttachmentCount = 1;
   subpass[0].pColorAttachments = &color_ref;
   subpass[0].pResolveAttachments = NULL;
   subpass[0].pDepthStencilAttachment = NULL;
   subpass[0].preserveAttachmentCount = 0;
   subpass[0].pPreserveAttachments = NULL;

   VkRenderPassCreateInfo rp_info;
   rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
   rp_info.pNext = NULL;
   rp_info.attachmentCount = 1;
   rp_info.pAttachments = attachments;
   rp_info.subpassCount = 1;
   rp_info.pSubpasses = subpass;
   rp_info.dependencyCount = 0;
   rp_info.pDependencies = NULL;
   rp_info.flags = 0;

   VkRenderPass renderpass;
   VK_CHECK(vkCreateRenderPass(res->ctx->device, &rp_info, NULL, &renderpass));

   return renderpass;
}

static void
init_debug_tile_resources(SceneResources *res)
{
   res->debug.renderpass =
      create_debug_tile_renderpass(res);

   VkdfImage *color_image = vkdf_scene_get_color_render_target(res->scene);
   res->debug.framebuffer =
      vkdf_create_framebuffer(res->ctx,
                              res->debug.renderpass,
                              color_image->view,
                              res->ctx->width, res->ctx->height,
                              0, NULL);

   create_debug_tile_pipeline(res);

   record_debug_tile_cmd_buf(res);
}


static void
init_resources(VkdfContext *ctx, SceneResources *res)
{
   memset(res, 0, sizeof(SceneResources));

   res->ctx = ctx;

   init_descriptor_pools(res);
   init_cmd_bufs(res);
   init_scene(res);
   init_meshes(res);
   init_objects(res);
   init_ubos(res);
   init_shaders(res);

   /* We need to prepare the scene before we build the pipelines, since these
    * will reference and bind resources provided by the scene
    */
   vkdf_scene_prepare(res->scene);
   init_pipelines(res);

   if (SHOW_DEBUG_TILE) {
      // Select source image for debug output.
      if (res->scene->ssao.enabled) {
         VkdfImage *ssao_image = vkdf_scene_get_ssao_image(res->scene);
         res->debug.image = *ssao_image;
      } else if (!ENABLE_DEFERRED_RENDERING) {
         res->debug.image = res->scene->lights[0]->shadow.shadow_map;
      } else {
         res->debug.image = res->scene->rt.gbuffer[0];
      }

      init_debug_tile_resources(res);
   }
}

static void
postprocess_draw(VkdfContext *ctx,
                 VkSemaphore scene_draw_sem,
                 VkSemaphore postprocess_draw_sem,
                 void *data)
{
   SceneResources *res = (SceneResources *) data;

   // Render debug tile
   VkPipelineStageFlags debug_tile_wait_stages =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
   vkdf_command_buffer_execute(ctx,
                               res->debug.cmd_buf,
                               &debug_tile_wait_stages,
                               1, &scene_draw_sem,
                               1, &postprocess_draw_sem);
}

static void
destroy_models(SceneResources *res)
{
   vkdf_mesh_free(res->ctx, res->tile_mesh);
   vkdf_model_free(res->ctx, res->sponza_model);
}

static void
destroy_cmd_bufs(SceneResources *res)
{
   vkDestroyCommandPool(res->ctx->device, res->cmd_pool, NULL);
}

static void
destroy_pipelines(SceneResources *res)
{
   /* Pipelines */
   vkDestroyPipeline(res->ctx->device, res->pipelines.sponza, NULL);
   vkDestroyPipeline(res->ctx->device, res->pipelines.sponza_opacity, NULL);

   if (ENABLE_DEFERRED_RENDERING) {
      vkDestroyPipelineLayout(res->ctx->device,
                              res->pipelines.layout.gbuffer_base, NULL);
      vkDestroyPipelineLayout(res->ctx->device,
                              res->pipelines.layout.gbuffer_opacity, NULL);

      vkDestroyPipeline(res->ctx->device, res->pipelines.gbuffer_merge, NULL);
      vkDestroyPipelineLayout(res->ctx->device,
                              res->pipelines.layout.gbuffer_merge, NULL);
   } else {
      vkDestroyPipelineLayout(res->ctx->device,
                              res->pipelines.layout.base, NULL);
      vkDestroyPipelineLayout(res->ctx->device,
                              res->pipelines.layout.opacity, NULL);
   }

   if (ENABLE_DEPTH_PREPASS) {
      vkDestroyPipeline(res->ctx->device, res->pipelines.depth_prepass, NULL);
      vkDestroyPipelineLayout(res->ctx->device,
                              res->pipelines.layout.depth_prepass , NULL);

      vkDestroyPipeline(res->ctx->device,
                        res->pipelines.depth_prepass_opacity, NULL);
      vkDestroyPipelineLayout(res->ctx->device,
                              res->pipelines.layout.depth_prepass_opacity,
                              NULL);
   }

   /* Descriptor sets */

   /* Camera view */
   vkFreeDescriptorSets(res->ctx->device,
                        res->descriptor_pool.static_ubo_pool,
                        1, &res->pipelines.descr.camera_view_set);
   vkDestroyDescriptorSetLayout(res->ctx->device,
                                res->pipelines.descr.camera_view_layout, NULL);

   /* Object data */
   vkFreeDescriptorSets(res->ctx->device,
                        res->descriptor_pool.static_ubo_pool,
                        1, &res->pipelines.descr.obj_set);
   vkDestroyDescriptorSetLayout(res->ctx->device,
                                res->pipelines.descr.obj_layout, NULL);

   /* Light data */
   vkFreeDescriptorSets(res->ctx->device,
                        res->descriptor_pool.static_ubo_pool,
                        1, &res->pipelines.descr.light_set);
   vkDestroyDescriptorSetLayout(res->ctx->device,
                                res->pipelines.descr.light_layout, NULL);

   /* Sponza samplers */
   for (uint32_t i = 0; i < res->sponza_model->tex_materials.size(); i++) {
      if (res->pipelines.descr.obj_tex_set[i]) {
         vkFreeDescriptorSets(res->ctx->device,
                              res->descriptor_pool.sampler_pool,
                              1, &res->pipelines.descr.obj_tex_set[i]);
      }
   }
   vkDestroyDescriptorSetLayout(res->ctx->device,
                                res->pipelines.descr.obj_tex_layout, NULL);

   vkDestroyDescriptorSetLayout(res->ctx->device,
                                res->pipelines.descr.obj_tex_opacity_layout,
                                NULL);

   if (ENABLE_DEPTH_PREPASS) {
      for (uint32_t i = 0; i < res->sponza_model->tex_materials.size(); i++) {
         if (res->pipelines.descr.depth_prepass_tex_set[i]) {
            vkFreeDescriptorSets(res->ctx->device,
                                 res->descriptor_pool.sampler_pool,
                                 1, &res->pipelines.descr.depth_prepass_tex_set[i]);
         }
      }
      vkDestroyDescriptorSetLayout(res->ctx->device,
                                   res->pipelines.descr.depth_prepass_tex_layout,
                                   NULL);
   }

   /* Shadow map sampler */
   vkFreeDescriptorSets(res->ctx->device,
                        res->descriptor_pool.sampler_pool,
                        1, &res->pipelines.descr.shadow_map_sampler_set);
   vkDestroyDescriptorSetLayout(res->ctx->device,
                                res->pipelines.descr.shadow_map_sampler_layout,
                                NULL);

   /* Gbuffer samplers */
   if (res->scene->rp.do_deferred) {
      vkFreeDescriptorSets(res->ctx->device,
                           res->descriptor_pool.sampler_pool,
                           1, &res->pipelines.descr.gbuffer_tex_set);
   }

   vkDestroyDescriptorSetLayout(res->ctx->device,
                                res->pipelines.descr.gbuffer_tex_layout, NULL);

   /* Descriptor pools */
   vkDestroyDescriptorPool(res->ctx->device,
                           res->descriptor_pool.static_ubo_pool, NULL);
   vkDestroyDescriptorPool(res->ctx->device,
                           res->descriptor_pool.sampler_pool, NULL);
}

static void
destroy_shader_modules(SceneResources *res)
{
   vkDestroyShaderModule(res->ctx->device, res->shaders.depth_prepass.vs, NULL);
   vkDestroyShaderModule(res->ctx->device, res->shaders.depth_prepass.vs_opacity, NULL);
   vkDestroyShaderModule(res->ctx->device, res->shaders.depth_prepass.fs_opacity, NULL);

   vkDestroyShaderModule(res->ctx->device, res->shaders.obj_forward.vs, NULL);
   vkDestroyShaderModule(res->ctx->device, res->shaders.obj_forward.fs, NULL);
   vkDestroyShaderModule(res->ctx->device, res->shaders.obj_forward.fs_opacity, NULL);

   vkDestroyShaderModule(res->ctx->device, res->shaders.obj_gbuffer.vs, NULL);
   vkDestroyShaderModule(res->ctx->device, res->shaders.obj_gbuffer.fs, NULL);
   vkDestroyShaderModule(res->ctx->device, res->shaders.obj_gbuffer.fs_opacity, NULL);

   vkDestroyShaderModule(res->ctx->device, res->shaders.gbuffer_merge.vs, NULL);
   vkDestroyShaderModule(res->ctx->device, res->shaders.gbuffer_merge.fs, NULL);
   vkDestroyShaderModule(res->ctx->device, res->shaders.gbuffer_merge.fs_ssao, NULL);
}

static void
destroy_ubos(SceneResources *res)
{
   vkDestroyBuffer(res->ctx->device, res->ubos.camera_view.buf.buf, NULL);
   vkFreeMemory(res->ctx->device, res->ubos.camera_view.buf.mem, NULL);
}

static void
destroy_debug_tile_resources(SceneResources *res)
{
   vkDestroyShaderModule(res->ctx->device, res->debug.shaders.vs, NULL);
   vkDestroyShaderModule(res->ctx->device, res->debug.shaders.fs, NULL);

   vkDestroyRenderPass(res->ctx->device, res->debug.renderpass, NULL);


   vkDestroyPipelineLayout(res->ctx->device, res->debug.pipeline.layout, NULL);
   vkDestroyPipeline(res->ctx->device, res->debug.pipeline.pipeline, NULL);

   vkFreeDescriptorSets(res->ctx->device,
                        res->descriptor_pool.sampler_pool,
                        1, &res->debug.pipeline.sampler_set);
   vkDestroyDescriptorSetLayout(res->ctx->device,
                                res->debug.pipeline.sampler_set_layout, NULL);

   vkDestroyFramebuffer(res->ctx->device, res->debug.framebuffer, NULL);
}

static void
destroy_samplers(SceneResources *res)
{
   vkDestroySampler(res->ctx->device, res->debug.sampler, NULL);
   vkDestroySampler(res->ctx->device, res->sponza_sampler, NULL);
   vkDestroySampler(res->ctx->device, res->gbuffer_sampler, NULL);
   vkDestroySampler(res->ctx->device, res->ssao_sampler, NULL);
}

void
cleanup_resources(SceneResources *res)
{
   destroy_samplers(res);
   vkdf_scene_free(res->scene);
   if (SHOW_DEBUG_TILE)
      destroy_debug_tile_resources(res);
   destroy_cmd_bufs(res);
   destroy_shader_modules(res);
   destroy_pipelines(res);
   destroy_ubos(res);
   destroy_models(res);

   vkdf_camera_free(res->camera);
}

int
main()
{
   VkdfContext ctx;
   SceneResources resources;

   vkdf_init(&ctx, WIN_WIDTH, WIN_HEIGHT, WIN_FULLSCREEN, false, true);
   init_resources(&ctx, &resources);

   vkdf_scene_event_loop_run(resources.scene);

   cleanup_resources(&resources);
   vkdf_cleanup(&ctx);

   return 0;
}
