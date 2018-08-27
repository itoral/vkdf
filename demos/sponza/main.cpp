#include "vkdf.hpp"

// ================================= CONFIG ===================================

/* Window resolution */
const float      WIN_WIDTH                 = 1024.0f;
const float      WIN_HEIGHT                = 768.0f;
const bool       WIN_FULLSCREEN            = false;

/* Framerate target */
const float      FRAMERATE_TARGET          = 30.0f;

/* Sponza flag mesh */
const bool       SHOW_SPONZA_FLAG_MESH     = false;
const uint32_t   SPONZA_FLAG_MESH_IDX      = 4;

/* Show debug texture
 *
 * WARNING: Enabling this produces a GPU hang on Intel Mesa when SSR is also
 *          enabled with deferred rendering. The hang goes away if we remove
 *          the blur pass from the SSR implementation.
 */
const bool       SHOW_DEBUG_TILE           = false;

/* Pipeline options */
const bool       ENABLE_CLIPPING           = true;
const bool       ENABLE_DEPTH_PREPASS      = true;
const bool       ENABLE_DEFERRED_RENDERING = true;

/* Deferred rendering options
 *
 * GBUFFER_OPTIMIZE_FOR_QUALITY uses a 32-bit GBuffer attachment to store
 * fragment positions in light-space which are involved in shadow mapping
 * calculations. These calculations are very sensitive to precision, so
 * using a 32-bit format trades performance for quality. If this is set to
 * False. we use a 16-bit precision format which leads to visible artifacts
 * that can be reduced to some extent by increasing shadow mapping bias
 * parameters at the expense of introducing peter panning.
 */
const uint32_t   GBUFFER_OPTIMIZE_FOR_QUALITY  = true;

/* Anisotropic filtering */
const float      MAX_ANISOTROPY            = 16.0f; // Min=0.0 (disabled)

/* Shadow mapping */
const bool       ENABLE_SHADOWS            = true;
const uint32_t   SHADOW_MAP_SIZE           = 4096;
const int32_t    SHADOW_MAP_SKIP_FRAMES    = -1;    // N < 0: never update, N >= 0: skip N frames
const uint32_t   SHADOW_MAP_PCF_SIZE       = 2;     // Min=1 (disabled)
const uint32_t   SHADOW_MAP_CONST_BIAS     = 1.0f;
const uint32_t   SHADOW_MAP_SLOPE_BIAS     = 2.0f;

/* Screen Space Ambient Occlusion (SSAO)
 *
 * SSAO requires that deferred rendering is enabled.
 */
const bool       ENABLE_SSAO               = true;
const uint32_t   SSAO_NUM_SAMPLES          = 24;
const float      SSAO_RADIUS               = 0.75f;
const float      SSAO_BIAS                 = 0.05f;
const float      SSAO_INTENSITY            = 3.0f;
const uint32_t   SSAO_BLUR_SIZE            = 2;     // Min=0 (no blur)
const float      SSAO_BLUR_THRESHOLD       = 0.05f; // Min > 0.0
const float      SSAO_DOWNSAMPLING         = 1.0f;  // Min=1.0 (no downsampling)
const VkFilter   SSAO_FILTER               = VK_FILTER_NEAREST;

/* High Dynamic Range (HDR) and Tone Mapping */
const bool       ENABLE_HDR                = true;
const float      HDR_EXPOSURE              = 1.5f;  // Min > 0.0

/* Sun light */
const glm::vec4  SUN_DIRECTION             = glm::vec4(1.0f, -4.5f, -1.25f, 0.0f);
const glm::vec4  SUN_DIFFUSE               = glm::vec4(3.0f, 3.0f, 3.0f, 1.0f);
const glm::vec4  SUN_SPECULAR              = glm::vec4(3.0f, 3.0f, 3.0f, 1.0f);
const glm::vec4  SUN_AMBIENT               = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);

/* Screen Space Reflections (SSR) */
const bool       ENABLE_SSR                = true;
const float      SSR_REFLECTION_STRENGTH   = 0.1f;  // Min > 0.0, Max=1.0
const int        SSR_REFLECTION_ROUGHNESS  = 0;     // Min = 0
const int32_t    SSR_MAX_SAMPLES           = 32;
const float      SSR_STEP_SIZE             = 0.02f; // Min > 0.0
const int32_t    SSR_MAX_SEARCH_SAMPLES    = 4;     // Min >= 0
const float      SSR_MAX_REFLECTION_DIST   = 0.7f;  // Min > 0.0

/* Antialiasing (super sampling) */
const float      SUPER_SAMPLING_FACTOR     = 1.0f;  // Min=1.0 (disabled)

/* Antialiasing (FXAA) */
const bool       ENABLE_FXAA               = true;
const float      FXAA_LUMA_MIN             = 0.1f;    // Min > 0.0, Max=1.0
const float      FXAA_LUMA_RANGE_MIN       = 0.1312f; // Min > 0.0, Max=1.0
const float      FXAA_SUBPX_AA             = 0.5f;    // Min=0.0 (disabled)

/* Collision detection */
const bool       ENABLE_COLLISIONS         = true;

/* Automatic camera */
const bool       AUTO_CAMERA_START_ENABLED = false;
const float      AUTO_CAMERA_FADE_SPEED    = 0.005f;
const uint32_t   AUTO_CAMERA_BLANK_FRAMES  = 90;
const VkdfKey    AUTO_CAMERA_ENABLE_KEY    = VKDF_KEY_A;

// =============================== Declarations ===============================

/* Used to render individual meshes and inspect their IDs. Use SPACE to
 * iterate the mesh to render.
 */
#define DEBUG_MESH_IDX 0
#if DEBUG_MESH_IDX
static uint32_t cur_mesh_idx = 0;
#endif

const uint32_t   SPONZA_FLOOR_MATERIAL_IDX = 10;

const bool       SHOW_SPONZA_VASE_MESHES   = true;
const uint32_t   SPONZA_VASE_MESH_IDX[]    = {
   1,   2,
   380, 381,
   378, 379,
   376, 377,
   374, 375,
   372, 373,
   370, 371,
   368, 369
};

const bool       SHOW_SPONZA_SUPPORT_MESHES= true;
const uint32_t   SPONZA_SUPPORT_MESH_IDX[] = {
   332, 333, 334, 335, 336, 337, 338, 339, 340,
   341, 342, 343, 344, 345, 346, 347, 348, 349,
   351, 352, 353, 354, 355, 356, 357, 358, 359,
   360, 361, 362, 363, 364, 365, 366, 367, 368
};

/* Indices of meshes for which we do mesh-level collision detection.
 * Collision for other parts of the model is done through invisible walls.
 */
const uint32_t SPONZA_COLLISION_MESH_IDX[] = {
   /* Vases */
   2, 369, 371, 373, 375, 377, 379, 381,

   /* Columns */
   9, 10, 11, 12, 13, 14, 15, 16,
   22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33,
   120, 121, 122, 123,
   127, 138, 149, 160, 171, 182, 193, 204, 215, 226, 237, 248,

   /* Water pools  */
   382, 383, 384, 385,

   /* Curtains */
   322, 323, 324, 325, 326, 327, 328, 329, 330, 331
};

enum {
   DIFFUSE_TEX_BINDING  = 0,
   NORMAL_TEX_BINDING   = 1,
   SPECULAR_TEX_BINDING = 2,
   OPACITY_TEX_BINDING  = 3,
};

typedef enum {
   AUTO_CAM_SETUP_STATE    = 0,
   AUTO_CAM_FADE_IN_STATE  = 1,
   AUTO_CAM_STABLE_STATE   = 2,
   AUTO_CAM_FADE_OUT_STATE = 3,
   AUTO_CAM_BLANK_STATE    = 4
} AutoCameraState;

struct PCBDataProj {
   uint8_t proj[sizeof(glm::mat4)];
};

struct PCBDataPosRecons {
   uint8_t proj[sizeof(glm::mat4)];
   float aspect_ratio;
   float tan_half_fov;
};

typedef struct {
   VkdfContext *ctx;

   VkdfScene *scene;

   VkdfCamera *camera;

   float auto_camera_todo;
   AutoCameraState auto_camera_state;
   uint32_t auto_camera_blank_timeout;
   bool auto_camera_enabled;

   bool collisions_enabled;

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
   VkSampler sponza_opacity_sampler;
   VkSampler gbuffer_sampler;
   VkSampler ssao_sampler;

   VkdfLight *light;
   VkdfSceneShadowSpec shadow_spec;

   struct {
      int32_t mesh_count;
   } iterative_rendering;

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
   } debug;
} SceneResources;

static void
postprocess_draw(VkdfContext *ctx, VkCommandBuffer cmd_buf, void *data);


// ============================== Implementation ==============================

static inline VkdfBuffer
create_ubo(VkdfContext *ctx, uint32_t size, uint32_t usage, uint32_t mem_props)
{
   usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
   VkdfBuffer buf = vkdf_create_buffer(ctx, 0, size, usage, mem_props);
   return buf;
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

static void
update_auto_camera_state(SceneResources *res, VkCommandBuffer cmd_buf)
{
   float brightness = vkdf_scene_brightness_filter_get_brightness(res->scene);
   switch(res->auto_camera_state) {
   case AUTO_CAM_SETUP_STATE:
      vkdf_scene_brightness_filter_set_brightness(res->scene, cmd_buf, 0.0f);
      res->auto_camera_state = AUTO_CAM_FADE_IN_STATE;
      break;
   case AUTO_CAM_FADE_IN_STATE:
      brightness = MIN2(brightness + AUTO_CAMERA_FADE_SPEED, 1.0f);
      vkdf_scene_brightness_filter_set_brightness(res->scene,
                                                     cmd_buf, brightness);
      if (brightness >= 1.0f)
         res->auto_camera_state = AUTO_CAM_STABLE_STATE;
      break;
   case AUTO_CAM_STABLE_STATE:
      if (res->auto_camera_todo <= 200.0f)
         res->auto_camera_state = AUTO_CAM_FADE_OUT_STATE;
      break;
   case AUTO_CAM_FADE_OUT_STATE:
      brightness = MAX2(brightness - AUTO_CAMERA_FADE_SPEED, 0.0f);
      vkdf_scene_brightness_filter_set_brightness(res->scene,
                                                  cmd_buf, brightness);
      if (brightness <= 0.0f) {
         res->auto_camera_blank_timeout = AUTO_CAMERA_BLANK_FRAMES;
         res->auto_camera_state = AUTO_CAM_BLANK_STATE;
      }
      break;
   case AUTO_CAM_BLANK_STATE:
      assert(brightness <= 0.0f);
      if (res->auto_camera_blank_timeout == 0) {
         vkdf_camera_next_program(res->camera);
         res->auto_camera_state = AUTO_CAM_SETUP_STATE;
      } else {
         res->auto_camera_blank_timeout--;
      }
      break;
   default:
      assert(!"Invalid camera state");
   }
}

static bool
record_update_resources_command(VkdfContext *ctx,
                                VkCommandBuffer cmd_buf,
                                void *data)
{
   SceneResources *res = (SceneResources *) data;
   bool has_updates  = false;

   // Auto-camera state update
   if (res->auto_camera_enabled) {
      update_auto_camera_state(res, cmd_buf);
      has_updates = true;
   } else {
      // Restore brightness if we've just come out of auto-camera mode
      if (vkdf_scene_brightness_filter_get_brightness(res->scene) != 1.0f)
         vkdf_scene_brightness_filter_set_brightness(res->scene, cmd_buf, 1.0f);
   }

   // Update camera view matrix
   VkdfCamera *camera = vkdf_scene_get_camera(res->scene);
   if (vkdf_camera_is_dirty(camera)) {
      glm::mat4 view = vkdf_camera_get_view_matrix(res->camera);
      vkCmdUpdateBuffer(cmd_buf,
                        res->ubos.camera_view.buf.buf,
                        0, sizeof(glm::mat4), &view[0][0]);
      has_updates = true;
   }

   return has_updates;
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

#if DEBUG_MESH_IDX
      if (i != cur_mesh_idx)
         continue;
#endif

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

   // Don't bother rendering if brightness is set to 0
   if (vkdf_scene_brightness_filter_get_brightness(res->scene) == 0.0f)
      return;

   // Push constants: camera projection matrix
   struct PCBDataProj pcb_data;
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
            /* If depth-prepass is enabled we have already done opacity
             * testing then so we use the regular pipeline to render everything.
             * If depth-prepass is disabled, then we need to do opacity
             * testing during the gbuffer generation.
             */
            pipeline = res->pipelines.sponza;
            pipeline_layout = res->pipelines.layout.base;
            pipeline_opacity = ENABLE_DEPTH_PREPASS ?
               res->pipelines.sponza :
               res->pipelines.sponza_opacity;
            pipeline_opacity_layout = ENABLE_DEPTH_PREPASS ?
               res->pipelines.layout.base :
               res->pipelines.layout.opacity;
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

   // Don't bother rendering if brightness is set to 0
   if (vkdf_scene_brightness_filter_get_brightness(res->scene) == 0.0f)
      return;

   // Push constants: camera projection matrix
   struct PCBDataProj pcb_data;
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
            /* If depth-prepass is enabled we have already done opacity
             * testing then so we use the regular pipeline to render everything.
             * If depth-prepass is disabled, then we need to do opacity
             * testing during the gbuffer generation.
             */
            pipeline = res->pipelines.sponza;
            pipeline_layout = res->pipelines.layout.gbuffer_base;
            pipeline_opacity = ENABLE_DEPTH_PREPASS ?
               res->pipelines.sponza :
               res->pipelines.sponza_opacity;
            pipeline_opacity_layout = ENABLE_DEPTH_PREPASS ?
               res->pipelines.layout.gbuffer_base :
               res->pipelines.layout.gbuffer_opacity;
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

   // Push constants (position reconstruction)
   VkdfCamera *cam = vkdf_scene_get_camera(res->scene);

   struct PCBDataPosRecons pcb;
   glm::mat4 *proj = vkdf_camera_get_projection_ptr(cam);
   memcpy(&pcb.proj, &(*proj)[0][0], sizeof(pcb.proj));
   pcb.aspect_ratio = cam->proj.aspect_ratio;
   pcb.tan_half_fov = tanf(glm::radians(cam->proj.fov / 2.0f));

   vkCmdPushConstants(cmd_buf,
                      res->pipelines.layout.gbuffer_merge,
                      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                      0, sizeof(pcb), &pcb);

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
auto_camera_disable(SceneResources *res)
{
   res->auto_camera_enabled = false;
   res->collisions_enabled = ENABLE_COLLISIONS;

   /* Move the camera to a "safe" place to avoid collisions */
   vkdf_camera_set_position(res->camera, 0.0f, 4.0f, 0.0f);
}

static void
auto_camera_enable(SceneResources *res)
{
   res->auto_camera_enabled = true;
   res->auto_camera_state = AUTO_CAM_SETUP_STATE;
   vkdf_camera_program_reset(res->camera, true, true);
   res->collisions_enabled = false;
}

static void
check_camera_collision(VkdfScene *s, VkdfCamera *cam, glm::vec3 prev_pos)
{
   if (prev_pos == cam->pos)
      return;

   if (!vkdf_scene_check_camera_collision(s, NULL))
      return;

   /* Otherwise, try to move in each axis separately so we can slide along
    * collision planes
    */
   glm::vec3 diff = cam->pos - prev_pos;
   cam->pos = prev_pos;
   for (uint32_t i = 0; i < 3; i++) {
      cam->pos += glm::vec3(diff.x * (i == 0 ? 1.0f : 0.0f),
                            diff.y * (i == 1 ? 1.0f : 0.0f),
                            diff.z * (i == 2 ? 1.0f : 0.0f));

      if (prev_pos != cam->pos && vkdf_scene_check_camera_collision(s, NULL))
         cam->pos = prev_pos;

      prev_pos = cam->pos;
   }
}

static void
update_camera(SceneResources *res)
{
   VkdfPlatform *platform = &res->ctx->platform;

   if (!res->auto_camera_enabled) {
      VkdfCamera *cam = vkdf_scene_get_camera(res->scene);

      // Joystick input
      if (vkdf_platform_joy_enabled(platform)) {
         /* Rotation (right thumbstick) */
         const float joy_rot_speed = 2.0f;

         float axis_pos;
         axis_pos = vkdf_platform_joy_check_axis(platform, VKDF_JOY_AXIS_RC_H);
         if (axis_pos != 0.0f)
            vkdf_camera_rotate(cam, 0.0f, joy_rot_speed * axis_pos, 0.0f);

         axis_pos = vkdf_platform_joy_check_axis(platform, VKDF_JOY_AXIS_RC_V);
         if (axis_pos != 0.0f)
            vkdf_camera_rotate(cam, joy_rot_speed * axis_pos, 0.0f, 0.0f);

         /* Movement (left thumbstick) */
         const float joy_step_speed = 0.20f;
         const float joy_strafe_speed = 0.15f;
         bool l3_pressed =
            vkdf_platform_joy_check_button(platform, VKDF_JOY_BTN_L3);

         glm::vec3 prev_pos = cam->pos;
         axis_pos = joy_strafe_speed *
                    vkdf_platform_joy_check_axis(platform, VKDF_JOY_AXIS_LC_H);
         if (axis_pos != 0.0f)
            vkdf_camera_strafe(cam, axis_pos);

         axis_pos = joy_step_speed *
                    vkdf_platform_joy_check_axis(platform, VKDF_JOY_AXIS_LC_V);
         if (axis_pos != 0.0f)
            vkdf_camera_step(cam, (l3_pressed ? 2.0f : 1.0f) * axis_pos, 1, 1, 1);

         if (res->collisions_enabled)
            check_camera_collision(res->scene, cam, prev_pos);

         if (vkdf_platform_joy_check_button(platform, VKDF_JOY_BTN_START)) {
            auto_camera_enable(res);
         }
      }

      // Keyboard input
      {
         float base_speed = 1.0f;
         const float mov_speed = 0.15f;
         const float rot_speed = 1.0f;

         // Rotation
         if (vkdf_platform_key_is_pressed(platform, VKDF_KEY_LEFT))
            vkdf_camera_rotate(cam, 0.0f, base_speed * rot_speed, 0.0f);
         else if (vkdf_platform_key_is_pressed(platform, VKDF_KEY_RIGHT))
            vkdf_camera_rotate(cam, 0.0f, -base_speed * rot_speed, 0.0f);

         if (vkdf_platform_key_is_pressed(platform, VKDF_KEY_PAGE_UP))
            vkdf_camera_rotate(cam, base_speed * rot_speed, 0.0f, 0.0f);
         else if (vkdf_platform_key_is_pressed(platform, VKDF_KEY_PAGE_DOWN))
            vkdf_camera_rotate(cam, -base_speed * rot_speed, 0.0f, 0.0f);

         // Stepping
         glm::vec3 prev_pos = cam->pos;
         float step_speed;
         if (vkdf_platform_key_is_pressed(platform, VKDF_KEY_UP)) {
            step_speed = base_speed * mov_speed;
            vkdf_camera_step(cam, step_speed, 1, 1, 1);
         } else if (vkdf_platform_key_is_pressed(platform, VKDF_KEY_DOWN)) {
            step_speed = -base_speed * mov_speed;
            vkdf_camera_step(cam, step_speed, 1, 1, 1);
         }

         if (res->collisions_enabled)
            check_camera_collision(res->scene, cam, prev_pos);
      }

      // Other keyboad bindings
      if (vkdf_platform_key_is_pressed(platform, VKDF_KEY_L)) {
         glm::vec3 pos = vkdf_camera_get_position(cam);
         glm::vec3 rot = vkdf_camera_get_rotation(cam);
         printf("Camera position: [%.2f, %.2f, %.2f]\n", pos.x, pos.y, pos.z);
         printf("Camera rotation: [%.2f, %.2f, %.2f]\n", rot.x, rot.y, rot.z);
      }

#if DEBUG_MESH_IDX
      if (vkdf_platform_key_is_pressed(platform, VKDF_KEY_SPACE)) {
         if (cur_mesh_idx < res->sponza_model->meshes.size() - 1)
            cur_mesh_idx++;
         else
            cur_mesh_idx = 0;
         printf("Current mesh: %d\n", cur_mesh_idx);
      }
#endif

      if (vkdf_platform_key_is_pressed(platform, AUTO_CAMERA_ENABLE_KEY)) {
         auto_camera_enable(res);
      }
   } else {
      /* Resume manual mode if any of the directional keys are pressed
       * or the joystick thumbsticks are used
       */
      bool keyboard_break =
         vkdf_platform_key_is_pressed(platform, VKDF_KEY_LEFT) ||
         vkdf_platform_key_is_pressed(platform, VKDF_KEY_RIGHT) ||
         vkdf_platform_key_is_pressed(platform, VKDF_KEY_UP) ||
         vkdf_platform_key_is_pressed(platform, VKDF_KEY_DOWN);

      bool joy_break =
         vkdf_platform_joy_enabled(platform) &&
         (fabs(vkdf_platform_joy_check_axis(platform, VKDF_JOY_AXIS_LC_H)) > 0.5f ||
          fabs(vkdf_platform_joy_check_axis(platform, VKDF_JOY_AXIS_LC_H)) > 0.5f ||
          fabs(vkdf_platform_joy_check_axis(platform, VKDF_JOY_AXIS_RC_H)) > 0.5f ||
          fabs(vkdf_platform_joy_check_axis(platform, VKDF_JOY_AXIS_RC_H)) > 0.5f);

      if (keyboard_break || joy_break) {
         auto_camera_disable(res);
      } else {
         if (res->auto_camera_state == AUTO_CAM_SETUP_STATE)
            vkdf_camera_program_reset(res->camera, true, true);
         else
            res->auto_camera_todo = vkdf_camera_program_update(res->camera);
      }
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

void
auto_cam_dynamic_light_start_cb(void *data)
{
   SceneResources *res = (SceneResources *) data;
   res->shadow_spec.skip_frames = 0;
   vkdf_scene_light_update_shadow_spec(res->scene, 0, &res->shadow_spec);
}

void
auto_cam_dynamic_light_update_cb(void *data)
{
   SceneResources *res = (SceneResources *) data;
   VkdfLight *light =  res->scene->lights[0]->light;
   glm::vec4 dir = light->origin + glm::vec4(0.01f, 0.0f, 0.002f, 0.0f);
   vkdf_light_set_direction(light, dir);
}

void
auto_cam_dynamic_light_2_update_cb(void *data)
{
   SceneResources *res = (SceneResources *) data;
   VkdfLight *light =  res->scene->lights[0]->light;
   glm::vec4 dir = light->origin + glm::vec4(-0.0020f, 0.0f, 0.0035f, 0.0f);
   vkdf_light_set_direction(light, dir);
}

void
auto_cam_dynamic_light_end_cb(void *data)
{
   SceneResources *res = (SceneResources *) data;
   VkdfLight *light =  res->scene->lights[0]->light;
   vkdf_light_set_direction(light, SUN_DIRECTION);
   res->shadow_spec.skip_frames = SHADOW_MAP_SKIP_FRAMES;
   vkdf_scene_light_update_shadow_spec(res->scene, 0, &res->shadow_spec);
   res->scene->lights[0]->shadow.frame_counter = -1;

   /* Reset the camera to its default configuration so when the shadow map
    * is next updated we get full scene coverage (useful when the nest
    * camera program doesn't require dynamic light and thus only captures
    * shadow map data once).
    */
   vkdf_camera_set_position(res->camera, -20.0f, 3.0f, -1.0f);
   vkdf_camera_look_at(res->camera, 10.0f, 5.0f, 0.0f);
}

void
auto_cam_iterative_rendering_start_cb(void *data)
{
   SceneResources *res = (SceneResources *) data;

   res->iterative_rendering.mesh_count = -50;
   for (uint32_t i = 0; i < res->sponza_model->meshes.size(); i++)
      res->sponza_model->meshes[i]->active = false;

   auto_cam_dynamic_light_start_cb(data);
}

void
auto_cam_iterative_rendering_update_cb(void *data)
{
   SceneResources *res = (SceneResources *) data;

   res->iterative_rendering.mesh_count++;
   uint32_t mesh_idx = res->iterative_rendering.mesh_count / 4;
   if (mesh_idx >= 0 && mesh_idx < res->sponza_model->meshes.size()) {
      if (mesh_idx != SPONZA_FLAG_MESH_IDX || SHOW_SPONZA_FLAG_MESH) {
         res->sponza_model->meshes[mesh_idx]->active = true;

         /* Mark dirty so the new active mesh makes it to the shadow map */
         vkdf_object_set_dirty(res->sponza_obj, true);
      }
   }
}

void
auto_cam_iterative_rendering_end_cb(void *data)
{
   SceneResources *res = (SceneResources *) data;

   uint32_t mesh_idx = res->iterative_rendering.mesh_count / 4;
   if (mesh_idx >= res->sponza_model->meshes.size())
      auto_cam_dynamic_light_end_cb(data);
}

static void
init_automatic_camera(SceneResources *res)
{
   VkdfCameraProgramSpec prog;
   memset(&prog, 0, sizeof(prog));

   prog.callback_data = res;

#if 0
   /* Iterative rendering of the model
    *
    * This uses the auto-camera program callbacks to activate progressive
    * rendering of the meshes over time rather than animating the camera.
    */
   prog.pos.start = glm::vec3(-25.0f, 14.0f, -3.0f);
   prog.pos.end = glm::vec3(-25.0f, 14.0f, -3.0f);
   prog.pos.speed = 0.0f;
   prog.rot.start = glm::vec3(-15.0f, 260.0f, 0.0f);
   prog.rot.end = glm::vec3(-15.0f, 260.0f, 0.0f);
   prog.rot.speed = 0.0f;
   prog.min_steps = 1850; /* Num meshes * 4 + some margin */
   prog.start_cb = auto_cam_iterative_rendering_start_cb;
   prog.update_cb = auto_cam_iterative_rendering_update_cb;
   prog.end_cb = auto_cam_iterative_rendering_end_cb;
   vkdf_camera_add_program(res->camera, &prog);
   prog.min_steps = 0;
   prog.start_cb = NULL;
   prog.update_cb = NULL;
   prog.end_cb = NULL;
#endif

   /* Lower attrium */
   prog.pos.start = glm::vec3(-30.0f, 3.0f, 3.0f);
   prog.pos.end = glm::vec3(15.0f, 8.0f, 1.0f);
   prog.pos.speed = 0.05f;
   prog.rot.start = glm::vec3(20.0f, -90.0f, 0.0f);
   prog.rot.end = glm::vec3(-20.0f, 75.0f, 0.0f);
   prog.rot.speed = 0.185f;
   vkdf_camera_add_program(res->camera, &prog);

   /* Upper attrium, columns */
   prog.pos.start = glm::vec3(-25.0f, 10.0f, -11.0f);
   prog.pos.end = glm::vec3(22.5f, 14.0f, -10.0f);
   prog.pos.speed = 0.05f;
   prog.rot.start = glm::vec3(0.0f, 270.0f, 0.0f);
   prog.rot.end = glm::vec3(-20.0f, 180.0f, 0.0f);
   prog.rot.speed = 0.1f;
   vkdf_camera_add_program(res->camera, &prog);

   /* Roof view */
   prog.pos.start = glm::vec3(20.0f, 35.0f, -20.0f);
   prog.pos.end = glm::vec3(-30.0f, 35.0f, 5.0f);
   prog.pos.speed = 0.05f;
   prog.rot.start = glm::vec3(-45.0f, 160.0f, 0.0f);
   prog.rot.end = glm::vec3(-45.0f, 300.0f, 0.0f);
   prog.rot.speed = 0.15f;
   vkdf_camera_add_program(res->camera, &prog);

   /* Lower attrium side-way */
   prog.pos.start = glm::vec3(20.0f, 1.0f, -11.0f);
   prog.pos.end = glm::vec3(-25.0f, 6.0f, -9.0f);
   prog.pos.speed = 0.04f;
   prog.rot.start = glm::vec3(-10.0f, 80.0f, 0.0f);
   prog.rot.end = glm::vec3(0.0f, 160.0f, 0.0f);
   prog.rot.speed = 0.07f;
   vkdf_camera_add_program(res->camera, &prog);

   /* Lower attrium, lion */
   prog.pos.start = glm::vec3(-20.0f, 3.0f, -1.0f);
   prog.pos.end = glm::vec3(20.0f, 3.0f, -1.0f);
   prog.pos.speed = 0.03f;
   prog.rot.start = glm::vec3(0.0f, 270.0f, 0.0f);
   prog.rot.end = glm::vec3(0.0f, 180.0f, 0.0f);
   prog.rot.speed = 0.0f;
   prog.min_steps = 0;
   prog.start_cb = NULL;
   prog.update_cb = NULL;
   prog.end_cb = NULL;
   vkdf_camera_add_program(res->camera, &prog);

   /* Lower attrium (dynamic light) */
   prog.pos.start = glm::vec3(-20.0f, 5.0f, -3.0f);
   prog.pos.end = glm::vec3(-20.0f, 5.0f, -3.0f);
   prog.pos.speed = 0.0f;
   prog.rot.start = glm::vec3(5.0f, 255.0f, 0.0f);
   prog.rot.end = glm::vec3(5.0f, 255.0f, 0.0f);
   prog.rot.speed = 0.0f;
   prog.min_steps = 1000;
   prog.start_cb = auto_cam_dynamic_light_start_cb;
   prog.update_cb = auto_cam_dynamic_light_update_cb;
   prog.end_cb = auto_cam_dynamic_light_end_cb;
   vkdf_camera_add_program(res->camera, &prog);

   /* Upper attrium (dynamic light) */
   prog.pos.start = glm::vec3(19.0f, 14.0f, -3.0f);
   prog.pos.end = glm::vec3(-14.0f, 14.0f, -2.0f);
   prog.pos.speed = 0.02f;
   prog.rot.start = glm::vec3(-19.0f, 125.0f, 0.0f);
   prog.rot.end = glm::vec3(-19.0f, 125.0f, 0.0f);
   prog.rot.speed = 0.0f;
   prog.min_steps = 0;
   prog.start_cb = auto_cam_dynamic_light_start_cb;
   prog.update_cb = auto_cam_dynamic_light_2_update_cb;
   prog.end_cb = auto_cam_dynamic_light_end_cb;
   vkdf_camera_add_program(res->camera, &prog);

#if 0
   /* Lower attrium,  courtyard 360º */
   prog.pos.start = glm::vec3(0.0f, 2.0f, 0.0f);
   prog.pos.end = glm::vec3(0.0f, 2.0f, 0.0f);
   prog.pos.speed = 0.0f;
   prog.rot.start = glm::vec3(0.0f, 0.0f, 0.0f);
   prog.rot.end = glm::vec3(60.0f, 360.0f, 0.0f);
   prog.rot.speed = 0.25f;
   prog.min_steps = 0;
   prog.start_cb = NULL;
   prog.update_cb = NULL;
   prog.end_cb = NULL;
   vkdf_camera_add_program(res->camera, &prog);
#endif

   /* Lower attrium, walls */
   prog.pos.start = glm::vec3(-24.0f, 0.0f, 2.0f);
   prog.pos.end = glm::vec3(21.0f, 0.0f, 2.0f);
   prog.pos.speed = 0.03f;
   prog.rot.start = glm::vec3(55.0f, 0.0f, 0.0f);
   prog.rot.end = glm::vec3(55.0f, 45.0f, 0.0f);
   prog.rot.speed = 0.03f;
   prog.min_steps = 0;
   prog.start_cb = NULL;
   prog.update_cb = NULL;
   prog.end_cb = NULL;
   vkdf_camera_add_program(res->camera, &prog);

   prog.min_steps = 0;
   prog.start_cb = NULL;
   prog.update_cb = NULL;
   prog.end_cb = NULL;
}

static void
init_scene(SceneResources *res)
{
   VkdfContext *ctx = res->ctx;

   res->camera = vkdf_camera_new(-20.0f, 3.0f, -1.0f,
                                 0.0f, 180.0f, 0.0f,
                                 45.0f, 0.1f, 500.0f, WIN_WIDTH / WIN_HEIGHT);

   vkdf_camera_look_at(res->camera, 10.0f, 5.0f, 0.0f);

   VkdfMesh *cam_mesh = vkdf_cube_mesh_new(res->ctx);
   vkdf_camera_set_collision_mesh(res->camera, cam_mesh, glm::vec3(0.25f));

   res->collisions_enabled = ENABLE_COLLISIONS;
   init_automatic_camera(res);

   glm::vec3 scene_origin = glm::vec3(0.0f, 0.0f, 0.0f);
   glm::vec3 scene_size = glm::vec3(200.0f, 200.0f, 200.0f);
   glm::vec3 tile_size = glm::vec3(200.0f, 200.0f, 200.0f);
   uint32_t cache_size = 0;

   uint32_t fb_width = (uint32_t) (WIN_WIDTH * SUPER_SAMPLING_FACTOR);
   uint32_t fb_height = (uint32_t) (WIN_HEIGHT * SUPER_SAMPLING_FACTOR);

   res->scene = vkdf_scene_new(ctx,
                               fb_width, fb_height,
                               res->camera,
                               scene_origin, scene_size, tile_size, 1,
                               cache_size, 1);

   VkFilter present_filter =
      SUPER_SAMPLING_FACTOR > 1.0f ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
   vkdf_scene_set_framebuffer_present_filter(res->scene, present_filter);

   vkdf_scene_set_scene_callbacks(res->scene,
                                  scene_update,
                                  record_update_resources_command,
                                  ENABLE_DEFERRED_RENDERING ?
                                     record_gbuffer_scene_commands :
                                     record_forward_scene_commands,
                                  res);

   if (SHOW_DEBUG_TILE) {
      vkdf_scene_enable_postprocessing(res->scene, postprocess_draw, NULL);
   }

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

   res->light = vkdf_light_new_directional(SUN_DIRECTION,
                                           SUN_DIFFUSE,
                                           SUN_AMBIENT,
                                           SUN_SPECULAR);

   res->light->intensity = 1.0f;

   /* Near and Far planes have been empirically chosen, together with the
    * directional offset, to provide the tightest shadow map box that registers
    * shadows that fall into the visible region of the camera. The scale is
    * increased in Z to account for the relatively high walls, so we avoid
    * computing shadow boxes that are not high enough to cover the ceiling
    * of the model.
    */
   vkdf_scene_shadow_spec_set(&res->shadow_spec,
                              SHADOW_MAP_SKIP_FRAMES,
                              SHADOW_MAP_SIZE,
                              0.1f, 60.0f,                 // Near, Far
                              SHADOW_MAP_CONST_BIAS,
                              SHADOW_MAP_SLOPE_BIAS,
                              -10.0f,                      // Directional offset
                              glm::vec3(1.0f, 1.0f, 2.0f), // Directional scale
                              SHADOW_MAP_PCF_SIZE);

   vkdf_scene_add_light(res->scene, res->light,
                        ENABLE_SHADOWS ? &res->shadow_spec : NULL);

   if (ENABLE_DEPTH_PREPASS)
      vkdf_scene_enable_depth_prepass(res->scene);

   if (ENABLE_DEFERRED_RENDERING) {
      /* We use an extra slot to store light-space fragment positions, which
       * we need to compute shadow mapping.
       *
       * We don't store eye-space positions, instead we reconstruct them in the
       * lighting pass (gbuffer merge pass) from the depth buffer for optimal
       * performance.
       */
      VkFormat light_space_pos_format =
         GBUFFER_OPTIMIZE_FOR_QUALITY ? VK_FORMAT_R32G32B32A32_SFLOAT :
                                        VK_FORMAT_R16G16B16A16_SFLOAT;
      vkdf_scene_enable_deferred_rendering(res->scene,
                                           record_gbuffer_merge_commands,
                                           1, light_space_pos_format);
   }

   if (ENABLE_SSAO) {
      vkdf_scene_enable_ssao(res->scene,
                             SSAO_DOWNSAMPLING,
                             SSAO_NUM_SAMPLES,
                             SSAO_RADIUS,
                             SSAO_BIAS,
                             SSAO_INTENSITY,
                             SSAO_BLUR_SIZE,
                             SSAO_BLUR_THRESHOLD);
   }

   if (ENABLE_SSR) {
      VkdfSceneSsrSpec ssr_config;
      vkdf_scene_ssr_spec_init_defaults(&ssr_config);
      ssr_config.max_samples = SSR_MAX_SAMPLES;
      ssr_config.min_step_size = SSR_STEP_SIZE;
      ssr_config.max_step_size = SSR_STEP_SIZE;
      ssr_config.max_binary_search_samples = SSR_MAX_SEARCH_SAMPLES;
      ssr_config.max_reflection_dist = SSR_MAX_REFLECTION_DIST;
      vkdf_scene_enable_ssr(res->scene, &ssr_config);
   }

   if (ENABLE_HDR) {
      vkdf_scene_enable_hdr(res->scene, true, HDR_EXPOSURE);
   }

   vkdf_scene_enable_brightness_filter(res->scene, 1.0f);

   if (ENABLE_FXAA) {
      vkdf_scene_enable_fxaa(res->scene,
                             FXAA_LUMA_MIN,
                             FXAA_LUMA_RANGE_MIN,
                             FXAA_SUBPX_AA);
   }
}

static void
create_sponza_texture_descriptor_sets(SceneResources *res)
{
   /* We use linear filtering and mipmapping for most textures */
   res->sponza_sampler =
         vkdf_create_sampler(res->ctx,
                             VK_SAMPLER_ADDRESS_MODE_REPEAT,
                             VK_FILTER_LINEAR,
                             VK_SAMPLER_MIPMAP_MODE_LINEAR,
                             MAX_ANISOTROPY);

   /* Opacity textures are tricky. We use discard() in the shaders to discard
    * non-opaque pixels (opacity < 1), but linear filtering can turn opaque
    * texels into non-opaque, leading to incorrect results where we don't
    * render all the pixels we should. Mipmapping accumulates this effect
    * further, so that only a few pixels in the mipmap stay with opacity=1,
    * which leads to pixels magically vanishing with distance as we switch
    * to smaller mipmaps.
    *
    * Unfortunately, using nearest filtering leads to very pixelated edges
    * that don't look good at all, specially at short distances, and also to
    * some missing pixels (can happen in opaque areas for very thin geometry
    * such as some vine stems).
    *
    * To get the best results, we make the shaders sample only from LOD 0,
    * to avoid artifacts when switching between mipmaps, and we use linear
    * filtering (within that single LOD) to avoid pixelated edges and missing
    * pixels in "thin" opaque areas. Linear filtering on LOD 0 means that some
    * "edge" pixels will still have opacity slightly < 1 due to some non-opaque
    * pixels ending up contributing to the resulting samples, so we correct that
    * by not dropping pixels unless their opacity goes below a certain
    * threshold. This means that some edges can look a bit odd up close
    * but at least the linear filtering will smooth this out producing a much
    * better result overall.
    *
    * FIXME: we can fix this by using blending instead of discard, but
    * that would require that we render meshes with opacity last and that would
    * not even be sufficient for deferred, which can't do transparency/blending
    * directly.
    */
   res->sponza_opacity_sampler =
         vkdf_create_sampler(res->ctx,
                             VK_SAMPLER_ADDRESS_MODE_REPEAT,
                             VK_FILTER_LINEAR,
                             VK_SAMPLER_MIPMAP_MODE_NEAREST,
                             0.0);

   VkdfModel *model = res->sponza_model;
   assert(model->tex_materials.size() == model->materials.size());

   for (uint32_t i = 0; i < model->materials.size(); i++) {
      VkdfMaterial *m = &model->materials[i];

      VkdfTexMaterial *tm = &model->tex_materials[i];

      /* We have a single shader that handles both solid+texture materials
       * and also solid-only materials. This means the shader always has
       * sampler bindings and these need to be valid even if the material
       * for the mesh we're rendering doesn't have any actual textures
       * so just bind the texture from a textured material
       *
       * When depth-prepass is enabled, opacity testing occurs during the
       * depth pre-pass, and later passes can ignore opacity completely
       * since they will only run for visible pixels.
       */
      if (m->opacity_tex_count == 0 || ENABLE_DEPTH_PREPASS) {
         res->pipelines.descr.obj_tex_set[i] =
            vkdf_descriptor_set_create(res->ctx,
                                       res->descriptor_pool.sampler_pool,
                                       res->pipelines.descr.obj_tex_layout);
      } else {
         res->pipelines.descr.obj_tex_set[i] =
            vkdf_descriptor_set_create(res->ctx,
                                       res->descriptor_pool.sampler_pool,
                                       res->pipelines.descr.obj_tex_opacity_layout);
      }

      if (ENABLE_DEPTH_PREPASS) {
         if (m->opacity_tex_count > 0) {
            res->pipelines.descr.depth_prepass_tex_set[i] =
               vkdf_descriptor_set_create(res->ctx,
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
         vkdf_descriptor_set_sampler_update(res->ctx,
                                            res->pipelines.descr.obj_tex_set[i],
                                            res->sponza_sampler,
                                            model->tex_materials[1].normal.view,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                            NORMAL_TEX_BINDING, 1);
      }

      if (m->opacity_tex_count > 0) {
         assert(tm->opacity.view);
         /* We only care for opacity outside the depth-prepass when
          * depth-prepass is disabled.
          */
         if (!ENABLE_DEPTH_PREPASS) {
            vkdf_descriptor_set_sampler_update(res->ctx,
                                               res->pipelines.descr.obj_tex_set[i],
                                               res->sponza_opacity_sampler,
                                               tm->opacity.view,
                                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                               OPACITY_TEX_BINDING, 1);
         } else {
            vkdf_descriptor_set_sampler_update(res->ctx,
                                               res->pipelines.descr.depth_prepass_tex_set[i],
                                               res->sponza_opacity_sampler,
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

   /* Default push constant range with Projection matrix for VS */
   VkPushConstantRange pcb_range;
   pcb_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
   pcb_range.offset = 0;
   pcb_range.size = sizeof(PCBDataProj);

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
      vkdf_create_ubo_descriptor_set_layout(res->ctx, 0, deferred ? 3 : 2,
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
      vkdf_descriptor_set_create(res->ctx,
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
      vkdf_descriptor_set_create(res->ctx,
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
      vkdf_descriptor_set_create(res->ctx,
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
      vkdf_descriptor_set_create(res->ctx,
                                 res->descriptor_pool.sampler_pool,
                                 res->pipelines.descr.shadow_map_sampler_layout);

   VkSampler sm_sampler;
   VkdfImage *sm_image;
   if (ENABLE_SHADOWS) {
      sm_sampler = vkdf_scene_light_get_shadow_map_sampler(res->scene, 0);
      sm_image = vkdf_scene_light_get_shadow_map_image(res->scene, 0);
   } else {
      /* We still need to provide a dummy descriptor set, even if it won't be
       * accessed by the shader.
       */
      sm_sampler = res->sponza_sampler;
      sm_image = &res->scene->rt.depth;
   }

   vkdf_descriptor_set_sampler_update(res->ctx,
                                      res->pipelines.descr.shadow_map_sampler_set,
                                      sm_sampler,
                                      sm_image->view,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                      0, 1);

   if (deferred) {
      /* Push constant buffer for position reconstruction */
      VkPushConstantRange pcb_recons_range;
      pcb_recons_range.stageFlags =
         VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
      pcb_recons_range.offset = 0;
      pcb_recons_range.size = sizeof(PCBDataPosRecons);

      /* Light eye-space direction */
      vkdf_scene_get_light_eye_space_data_ubo_range(res->scene,
                                                    &ubo_offset, &ubo_size);

      vkdf_descriptor_set_buffer_update(res->ctx,
                                        res->pipelines.descr.light_set,
                                        light_ubo->buf,
                                        2, 1, &ubo_offset, &ubo_size, false, true);

      /* textures: depth + gbuffer + ssao */
      const uint32_t gbuffer_size = res->scene->rt.gbuffer_size;
      uint32_t num_bindings = 1 + gbuffer_size;
      if (res->scene->ssao.enabled)
         num_bindings++;

      res->pipelines.descr.gbuffer_tex_layout =
         vkdf_create_sampler_descriptor_set_layout(res->ctx, 0,
                                                   num_bindings,
                                                   VK_SHADER_STAGE_FRAGMENT_BIT);

      res->pipelines.descr.gbuffer_tex_set =
         vkdf_descriptor_set_create(res->ctx,
                                    res->descriptor_pool.sampler_pool,
                                    res->pipelines.descr.gbuffer_tex_layout);

      res->gbuffer_sampler =
         vkdf_create_sampler(res->ctx,
                             VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                             VK_FILTER_NEAREST,
                             VK_SAMPLER_MIPMAP_MODE_NEAREST,
                             0.0f);

      /* Binding 0: depth buffer */
      uint32_t binding_idx = 0;
      vkdf_descriptor_set_sampler_update(res->ctx,
                                         res->pipelines.descr.gbuffer_tex_set,
                                         res->gbuffer_sampler,
                                         res->scene->rt.depth.view,
                                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                         binding_idx++, 1);

      /* Binding 1..N-1: GBuffer textures */
      for (uint32_t idx = 0; idx < gbuffer_size; idx++) {
         VkdfImage *image = vkdf_scene_get_gbuffer_image(res->scene, idx);
         vkdf_descriptor_set_sampler_update(res->ctx,
                                            res->pipelines.descr.gbuffer_tex_set,
                                            res->gbuffer_sampler,
                                            image->view,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                            binding_idx++, 1);
      }

      /* Binding N: SSAO texture */
      if (res->scene->ssao.enabled) {
         VkdfImage *ssao_image = vkdf_scene_get_ssao_image(res->scene);
         res->ssao_sampler =
            vkdf_ssao_create_ssao_sampler(res->ctx, SSAO_FILTER);
         vkdf_descriptor_set_sampler_update(res->ctx,
                                            res->pipelines.descr.gbuffer_tex_set,
                                            res->ssao_sampler,
                                            ssao_image->view,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                            binding_idx++, 1);
      }

      assert(num_bindings == binding_idx);

      /* Gbuffer merge pipeline layout */
      VkDescriptorSetLayout gbuffer_merge_layouts[] = {
         res->pipelines.descr.light_layout,
         res->pipelines.descr.shadow_map_sampler_layout,
         res->pipelines.descr.gbuffer_tex_layout
      };

      VkPipelineLayoutCreateInfo pipeline_layout_info;
      pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
      pipeline_layout_info.pNext = NULL;
      pipeline_layout_info.pushConstantRangeCount = 1;
      pipeline_layout_info.pPushConstantRanges = &pcb_recons_range;
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

   VkPipelineShaderStageCreateInfo vs_info;
   vkdf_pipeline_fill_shader_stage_info(&vs_info,
                                        VK_SHADER_STAGE_VERTEX_BIT,
                                        res->shaders.gbuffer_merge.vs);

   VkShaderModule fs = use_ssao ? res->shaders.gbuffer_merge.fs_ssao :
                                  res->shaders.gbuffer_merge.fs;

   VkPipelineShaderStageCreateInfo fs_info;
   VkSpecializationMapEntry entry = { 0, 0, sizeof(uint32_t) };
   VkSpecializationInfo fs_spec_info = {
      1,
      &entry,
      sizeof(uint32_t),
      &SHADOW_MAP_PCF_SIZE
   };
   vkdf_pipeline_fill_shader_stage_info(&fs_info,
                                        VK_SHADER_STAGE_FRAGMENT_BIT,
                                        fs, &fs_spec_info);

   VkPipeline pipeline =
      vkdf_create_gfx_pipeline(res->ctx,
                               NULL,
                               0,
                               NULL,
                               0,
                               NULL,
                               false,
                               VK_COMPARE_OP_ALWAYS,
                               renderpass,
                               res->pipelines.layout.gbuffer_merge,
                               VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
                               VK_CULL_MODE_BACK_BIT,
                               1,
                               &vs_info, &fs_info);
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
   vkdf_model_load_textures(res->ctx, res->cmd_pool, res->sponza_model, true);

   if (SHOW_SPONZA_FLAG_MESH == false)
      res->sponza_model->meshes[SPONZA_FLAG_MESH_IDX]->active = false;

   if (SHOW_SPONZA_VASE_MESHES == false) {
      uint32_t num_meshes = sizeof(SPONZA_VASE_MESH_IDX) / sizeof(uint32_t);
      for (uint32_t i = 0; i < num_meshes; i++) {
         uint32_t mesh_idx = SPONZA_VASE_MESH_IDX[i];
         res->sponza_model->meshes[mesh_idx]->active = false;
      }
   }

   if (SHOW_SPONZA_SUPPORT_MESHES == false) {
      uint32_t num_meshes = sizeof(SPONZA_SUPPORT_MESH_IDX) / sizeof(uint32_t);
      for (uint32_t i = 0; i < num_meshes; i++) {
         uint32_t mesh_idx = SPONZA_SUPPORT_MESH_IDX[i];
         res->sponza_model->meshes[mesh_idx]->active = false;
      }
   }

   if (ENABLE_SSR) {
      res->sponza_model->materials[SPONZA_FLOOR_MATERIAL_IDX].
         reflectiveness = SSR_REFLECTION_STRENGTH;
      res->sponza_model->materials[SPONZA_FLOOR_MATERIAL_IDX].
         roughness = SSR_REFLECTION_ROUGHNESS;
   }

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
   vkdf_object_set_do_mesh_collision(obj, true);
   vkdf_scene_add_object(res->scene, "sponza", obj);

   res->sponza_obj = obj;

   /* Add a bunch of invisible walls to simplify collision testing */
   VkdfBox walls[] = {
#if 0
      { glm::vec3( -1.50f,   5.25f,  -5.25f), 19.0f, 50.0f,  0.5f }, // Inner wall (left)
      { glm::vec3( -1.50f,   5.25f,   3.75f), 19.0f, 50.0f,  0.5f }, // Inner wall (right)
#endif
      { glm::vec3(  0.00f,   0.00f,   0.00f), 50.0f,   0.5f, 50.0f }, // Lower floor
      { glm::vec3( 17.50f,   9.00f,  -1.00f),  1.0f,   1.0f,  5.5f }, // Upper inner wall (front)
      { glm::vec3(-20.50f,   9.0f,   -1.00f),  1.0f,   1.0f,  5.5f }, // Upper inner wall (back)
      { glm::vec3( -1.50f,   9.00,   -5.50f),  20.0f,  2.0f,  1.0f }, // Upper inner wall (left)
      { glm::vec3( -1.50f,   9.00f,   4.00f),  20.0f,  2.0f,  1.0f }, // Upper inner wall (right)
      { glm::vec3(-28.00f,   0.00f,   0.00f),   1.0f, 50.0f, 50.0f }, // External wall (back)
      { glm::vec3( 25.50f,   0.00f,   0.00f),   1.0f, 50.0f, 50.0f }, // External wall (front)
      { glm::vec3(  0.00f,   0.00f, -14.00f),  50.0f, 50.0f,  2.0f }, // External wall (left)
      { glm::vec3(  0.00f,   0.00f,  12.50f),  50.0f, 50.0f,  2.0f }, // External wall (right)
      { glm::vec3( 23.00f,   7.50f,   0.00f),   5.0f,  2.0f, 50.0f }, // Upper floor (front)
      { glm::vec3(-25.00f,   7.50f,   0.00f),   5.0f,  2.0f, 50.0f }, // Upper floor (back)
      { glm::vec3(  0.00f,   7.50f, -11.50f),  50.0f,  2.0f,  5.0f }, // Upper floor (left)
      { glm::vec3(  0.00f,   7.50f,  10.00f),  50.0f,  2.0f,  5.0f }, // Upper floor (right)
      { glm::vec3(-20.50f,   5.50f,  -5.50f),   1.5f,  2.0f,  1.5f }, // Wall columns left (0)
      { glm::vec3(-12.50f,   5.50f,  -5.50f),   1.5f, 20.0f,  1.0f }, // Wall columns left (1)
      { glm::vec3( -4.75f,   5.50f,  -5.50f),   1.5f, 20.0f,  1.0f }, // Wall columns left (2)
      { glm::vec3(  2.25f,   5.50f,  -5.50f),   1.5f, 20.0f,  1.0f }, // Wall columns left (3)
      { glm::vec3(  9.25f,   5.50f,  -5.50f),   1.5f, 20.0f,  1.0f }, // Wall columns left (4)
      { glm::vec3( 17.50f,   5.50f,  -5.50f),   1.5f,  2.0f,  1.5f }, // Wall columns left (5)
      { glm::vec3(-20.50f,   5.50f,   4.00f),   1.5f,  2.0f,  1.5f }, // Wall columns right (0)
      { glm::vec3(-12.50f,   5.50f,   4.00f),   1.5f, 20.0f,  1.0f }, // Wall columns right (1)
      { glm::vec3( -4.75f,   5.50f,   4.00f),   1.5f, 20.0f,  1.0f }, // Wall columns right (2)
      { glm::vec3(  2.25f,   5.50f,   4.00f),   1.5f, 20.0f,  1.0f }, // Wall columns right (3)
      { glm::vec3(  9.25f,   5.50f,   4.00f),   1.5f, 20.0f,  1.0f }, // Wall columns right (4)
      { glm::vec3( 17.50f,   5.50f,   4.00f),   1.5f,  2.0f,  1.5f }, // Wall columns right (5)
      { glm::vec3(  0.00f,  15.50f,   0.00f),  50.0f,  1.0f, 50.0f }, // Top
   };

   vkdf_scene_add_invisible_wall_list(res->scene,
                                      sizeof(walls) / sizeof(VkdfBox),
                                      walls);

   /* And enable mesh-level collision testing only for a handful of
    * selected meshes
    */
   const uint32_t num_collision_meshes =
      sizeof(SPONZA_COLLISION_MESH_IDX) / sizeof(uint32_t);
   for (uint32_t i = 0; i < num_collision_meshes; i++) {
      vkdf_model_add_collison_mesh(res->sponza_model,
                                   SPONZA_COLLISION_MESH_IDX[i]);
   }
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
      vkdf_descriptor_set_create(res->ctx,
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
record_debug_tile_cmd_buf(SceneResources *res, VkCommandBuffer cmd_buf)
{
   const VkdfMesh *mesh = res->tile_mesh;

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

   vkCmdBeginRenderPass(cmd_buf,
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
   vkCmdSetViewport(cmd_buf, 0, 1, &viewport);

   VkRect2D scissor;
   scissor.extent.width = width;
   scissor.extent.height = height;
   scissor.offset.x = 0;
   scissor.offset.y = 0;
   vkCmdSetScissor(cmd_buf, 0, 1, &scissor);

   // Pipeline
   vkCmdBindPipeline(cmd_buf,
                     VK_PIPELINE_BIND_POINT_GRAPHICS,
                     res->debug.pipeline.pipeline);

   // Vertex buffer: position, uv
   const VkDeviceSize offsets[1] = { 0 };
   vkCmdBindVertexBuffers(cmd_buf,
                          0,                       // Start Binding
                          1,                       // Binding Count
                          &mesh->vertex_buf.buf,   // Buffers
                          offsets);                // Offsets

   // Descriptors
   vkCmdBindDescriptorSets(cmd_buf,
                           VK_PIPELINE_BIND_POINT_GRAPHICS,
                           res->debug.pipeline.layout,
                           0,                                // First decriptor set
                           1,                                // Descriptor set count
                           &res->debug.pipeline.sampler_set, // Descriptor sets
                           0,                                // Dynamic offset count
                           NULL);                            // Dynamic offsets

   // Draw
   vkCmdDraw(cmd_buf,
             mesh->vertices.size(),                // vertex count
             1,                                    // instance count
             0,                                    // first vertex
             0);                                   // first instance

   vkCmdEndRenderPass(cmd_buf);
}

static VkRenderPass
create_debug_tile_renderpass(SceneResources *res, VkFormat format)
{
   VkAttachmentDescription attachments[1];

   attachments[0].format = format;
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
   res->debug.image = res->scene->lights[0]->shadow.shadow_map;

   VkdfImage *color_image = vkdf_scene_get_color_render_target(res->scene);

   res->debug.renderpass =
      create_debug_tile_renderpass(res, color_image->format);

   res->debug.framebuffer =
      vkdf_create_framebuffer(res->ctx,
                              res->debug.renderpass,
                              color_image->view,
                              res->ctx->width, res->ctx->height,
                              0, NULL);

   create_debug_tile_pipeline(res);
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

   if (AUTO_CAMERA_START_ENABLED)
      auto_camera_enable(res);
}

static void
postprocess_draw(VkdfContext *ctx, VkCommandBuffer cmd_buf, void *data)
{
   SceneResources *res = (SceneResources *) data;
   init_debug_tile_resources(res);
   record_debug_tile_cmd_buf(res, cmd_buf);
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
   vkDestroySampler(res->ctx->device, res->sponza_opacity_sampler, NULL);
   vkDestroySampler(res->ctx->device, res->gbuffer_sampler, NULL);
   vkDestroySampler(res->ctx->device, res->ssao_sampler, NULL);
}

void
cleanup_resources(VkdfContext *ctx, SceneResources *res)
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

   vkdf_camera_free(ctx, res->camera);
}

int
main()
{
   VkdfContext ctx;
   SceneResources resources;

   vkdf_init(&ctx, WIN_WIDTH, WIN_HEIGHT, WIN_FULLSCREEN, false, false);
   vkdf_set_framerate_target(&ctx, FRAMERATE_TARGET);

   init_resources(&ctx, &resources);

   vkdf_scene_event_loop_run(resources.scene);

   cleanup_resources(&ctx, &resources);
   vkdf_cleanup(&ctx);

   return 0;
}
