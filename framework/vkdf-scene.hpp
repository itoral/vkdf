#ifndef __VKDF_SCENE_H__
#define __VKDF_SCENE_H__

#define SCENE_MAX_GBUFFER_SIZE 8

static const uint32_t SCENE_CMD_BUF_LIST_SIZE = 2;
static const bool SCENE_FREE_SECONDARIES = false;

typedef struct {
   uint32_t shadow_map_size;
   float shadow_map_near;
   float shadow_map_far;
   float depth_bias_const_factor;
   float depth_bias_slope_factor;

   // PFC kernel_size: valid values start at 1 (no PFC, 1 sample) to
   // N (2*(N-1)+1)^2 samples).
   uint32_t pcf_kernel_size;
} VkdfSceneShadowSpec;

typedef struct {
   VkdfLight *light;
   struct {
      VkdfSceneShadowSpec spec;
      glm::mat4 proj;
      glm::mat4 viewproj;
      VkdfImage shadow_map;
      VkFramebuffer framebuffer;
      VkSampler sampler;
      GList *visible;
   } shadow;
   VkdfFrustum frustum;
   bool dirty_frustum;
} VkdfSceneLight;

typedef struct _VkdfSceneTile VkdfSceneTile;
typedef struct _VkdfScene VkdfScene;

struct TileThreadData {
   uint32_t id;
   VkdfScene *s;
   uint32_t first_idx;
   uint32_t last_idx;
   const VkdfBox *visible_box;
   const VkdfPlane *fplanes;
   GList *visible;
   bool cmd_buf_changes;
};

struct _DirtyShadowMapInfo {
   VkdfSceneLight *sl;
   GHashTable *dyn_sets;
};

struct LightThreadData {
   uint32_t id;
   VkdfScene *s;
   VkdfSceneLight *sl;
   VkdfBox *visible_box;
   VkdfPlane *fplanes;
   bool has_dirty_shadow_map;
   struct _DirtyShadowMapInfo shadow_map_info;
};

typedef struct {
   GList *objs;                        // Set list
   uint32_t start_index;               // The global scene set index of the first object in this set
   uint32_t count;                     // Number of objects in the set
   uint32_t shadow_caster_start_index; // The shadow map scene set index of the first shadow caster object in this set
   uint32_t shadow_caster_count;       // Number of objects in the set that cast shadows
} VkdfSceneSetInfo;

struct _VkdfSceneTile {
   int32_t parent;
   uint32_t level;                 // Level of the tile
   uint32_t index;                 // Index of the tile in the level
   glm::vec3 offset;               // world-space offset of the tile
   bool dirty;                     // Whether new objects have been added
   VkdfBox box;                    // Bounding box of the ojects in the tile
   GHashTable *sets;               // Objects in the tile
   uint32_t obj_count;             // Number of objects in the tile
   uint32_t shadow_caster_count;   // Number of objects in the tile that can cast shadows
   VkCommandBuffer cmd_buf;        // Secondary command buffer for this tile
   VkCommandBuffer depth_cmd_buf;  // Secondary command buffer for this tile (depth-prepass)
   VkdfSceneTile *subtiles;        // Subtiles within this tile
};

typedef void (*VkdfSceneUpdateStateCB)(void *);
typedef bool (*VkdfSceneUpdateResourcesCB)(VkdfContext *, VkCommandBuffer, void *);
typedef void (*VkdfSceneCommandsCB)(VkdfContext *, VkCommandBuffer, GHashTable *, bool, bool, void *);
typedef void (*VkdfScenePostprocessCB)(VkdfContext *, VkSemaphore, VkSemaphore, void *);
typedef void (*VkdfSceneGbufferMergeCommandsCB)(VkdfContext *, VkCommandBuffer, void *);

struct _dim {
   float w;
   float h;
   float d;
};

struct _cache {
   GList *cached;
   uint32_t size;
   uint32_t max_size;
};

struct _VkdfScene {
   VkdfContext *ctx;

   // Scene resources
   VkdfCamera *camera;
   std::vector<VkdfSceneLight *> lights;
   GList *set_ids;
   GList *models;

   bool deferred;

   // Render target
   struct {
      uint32_t width;
      uint32_t height;
      VkdfImage color;
      VkdfImage depth;

      uint32_t gbuffer_size;
      VkdfImage gbuffer[SCENE_MAX_GBUFFER_SIZE];
   } rt;

   // Render passes
   struct {
      VkClearValue clear_values[2];
      VkClearValue gbuffer_clear_values[SCENE_MAX_GBUFFER_SIZE + 1];
      bool do_color_clear;
      bool do_deferred;
      bool do_depth_prepass;

      // Depth pre-pass render passes for static and dynamic geometry
      struct {
         VkRenderPass renderpass;
         VkFramebuffer framebuffer;
      } dpp_static_geom;

      struct {
         VkRenderPass renderpass;
         VkFramebuffer framebuffer;
      } dpp_dynamic_geom;

      // Forward or Deferred passes for static and dynamic geometry
      struct {
         VkRenderPass renderpass;
         VkFramebuffer framebuffer;
      } static_geom;

      struct {
         VkRenderPass renderpass;
         VkFramebuffer framebuffer;
      } dynamic_geom;

      // Deferred Gbuffer merge pass
      struct {
         VkRenderPass renderpass;
         VkFramebuffer framebuffer;
      } gbuffer_merge;
   } rp;

   // SSAO resources
   struct {
      bool enabled;

      /* Size of the SSAO texture */
      uint32_t width;
      uint32_t height;

      /* Samples */
      uint32_t num_samples;
      std::vector<glm::vec3> samples;
      struct {
         VkdfBuffer buf;
         VkDeviceSize size;
      } samples_buf;

      /* Noise */
      uint32_t num_noise_samples;
      std::vector<glm::vec3> noise;
      uint32_t noise_image_dim;
      VkdfImage noise_image;
      VkSampler noise_sampler;
      glm::vec2 noise_scale;

      /* Shader parameters */
      float radius;
      float bias;
      float intensity;
      int32_t blur_size;

      /* SSAO renderpasses */
      struct {
         struct {
            VkPipeline pipeline;
            VkPipelineLayout layout;
            VkDescriptorSetLayout samples_set_layout;
            VkDescriptorSet samples_set;
            VkDescriptorSetLayout textures_set_layout;
            VkDescriptorSet textures_set;
            struct {
               VkShaderModule vs;
               VkShaderModule fs;
            } shader;
         } pipeline;

         VkdfImage image;
         struct {
            VkRenderPass renderpass;
            VkFramebuffer framebuffer;
         } rp;

         VkSampler gbuffer_sampler; // To sample from GBuffer (deferred only)
      } base;

      struct {
         struct {
            VkPipeline pipeline;
            VkPipelineLayout layout;
            VkDescriptorSetLayout ssao_tex_set_layout;
            VkDescriptorSet ssao_tex_set;
            struct {
               // VS shader is shared with the base pass
               VkShaderModule fs;
            } shader;
         } pipeline;

         VkdfImage image;
         struct {
            VkRenderPass renderpass;
            VkFramebuffer framebuffer;
         } rp;

         VkSampler input_sampler;  // To sample input SSAO texture
      } blur;

      VkCommandBuffer cmd_buf;
   } ssao;

   VkFramebuffer framebuffer;
   uint32_t fb_width;
   uint32_t fb_height;

   // Tiling
   struct {
      glm::vec3 origin;

      float w;
      float h;
      float d;
   } scene_area;

   struct _dim *tile_size;

   uint32_t num_tile_levels;

   struct {
      uint32_t w;
      uint32_t h;
      uint32_t d;
      uint32_t total;
   } num_tiles;

   VkdfSceneTile *tiles;

   struct _cache *cache;

   bool dirty;                          // Dirty static objects (initialization)
   bool objs_dirty;                     // Dirty dynamic objects
   bool lights_dirty;                   // Dirty light sources
   bool shadow_maps_dirty;              // Dirty shadow maps
   uint32_t obj_count;                  // Total object count (static + dynamic)
   uint32_t static_obj_count;           // Number of static (tiled) objects
   uint32_t static_shadow_caster_count; // Number of static objects that are shadow casters
   bool has_shadow_caster_lights;       // If we have any static objects that can cast shadows

   /** 
    * active    : list of secondary command buffers that are active (that is,
    *             they are associated with a currently visible tile).
    *             [one list per thread]
    *
    * free      : list of obsolete (inactive) secondary command buffers that
    *             are still pending execution (in a previous frame). These
    *             commands will be freed when the corresponding fence is
    *             signaled. [one list per thread]
    *
    * primary   : The current primary command buffer for the visible tiles.
    *
    * resources : A command buffer recorded every frame to update
    *             resources used for rendering the current frame.
    */
   struct {
      VkCommandPool *pool;
      GList **active;
      GList **free;
      uint32_t cur_idx;                                        // Index of the current command (for command buffer lists)
      VkCommandBuffer dpp_primary[SCENE_CMD_BUF_LIST_SIZE];    // Command buffer for depth-prepass static objs
      VkCommandBuffer dpp_dynamic;                             // Command buffer for depth-prepass dynamic objs
      VkCommandBuffer primary[SCENE_CMD_BUF_LIST_SIZE];        // Command buffer for rendering static objs
      VkCommandBuffer dynamic;                                 // Command buffer for rendering dynamic objs
      VkCommandBuffer update_resources;                        // Command buffer for resource updates
      bool have_resource_updates;
      VkCommandBuffer shadow_maps;       // Command buffer for shadow map updates
      VkCommandBuffer *present;          // Command buffer rt -> swapchin copies
      VkCommandBuffer gbuffer_merge;     // Command buffer for deferred gbuffer merge
   } cmd_buf;

   struct {
      VkSemaphore update_resources_sem;
      VkSemaphore shadow_maps_sem;
      VkSemaphore depth_draw_static_sem;
      VkSemaphore depth_draw_sem;
      VkSemaphore draw_static_sem;
      VkSemaphore draw_sem;
      VkSemaphore ssao_sem;
      VkSemaphore gbuffer_merge_sem;
      VkSemaphore postprocess_sem;
      VkFence present_fence;
      bool present_fence_active;
   } sync;

   struct {
      VkdfSceneUpdateStateCB update_state;            // Updates application state, camera, etc
      VkdfSceneUpdateResourcesCB update_resources;    // Updates rendering resources used by command buffers
      VkdfSceneCommandsCB record_commands;            // Records command buffers
      VkdfScenePostprocessCB postprocess;             // Executes post-processing command buffers
      VkdfSceneGbufferMergeCommandsCB gbuffer_merge;  // Records Gbuffer merge command buffer (deferred only)
      void *data;
   } callbacks;

   struct {
      VkdfThreadPool *pool;
      uint32_t num_threads;
      uint32_t work_size;
      struct TileThreadData *tile_data;
   } thread;

   struct {
      VkRenderPass renderpass;
      struct {
         VkDescriptorSetLayout models_set_layout;
         VkDescriptorSet models_set;
         VkDescriptorSet dyn_models_set;
         VkPipelineLayout layout;
         GHashTable *pipelines;
      } pipeline;
      struct {
         VkShaderModule vs;
      } shaders;
   } shadows;

   struct {
      VkDescriptorPool static_pool;
      struct {
         VkdfBuffer buf;
         VkDeviceSize inst_size;
         VkDeviceSize size;
      } obj;
      struct {
         VkdfBuffer buf;
         VkDeviceSize size;
      } material;
      struct {
         VkdfBuffer buf;
         VkDeviceSize light_data_size;
         VkDeviceSize shadow_map_data_offset;
         VkDeviceSize shadow_map_data_size;
         VkDeviceSize size;
      } light;
      struct {
         VkdfBuffer buf;
         VkDeviceSize inst_size;
         VkDeviceSize size;
      } shadow_map;
   } ubo;

   struct {
      VkDescriptorPool pool;
   } sampler;

   struct {
      uint32_t visible_obj_count;            // Number of dynamic objects that are visible
      uint32_t visible_shadow_caster_count;  // Number of visible dynamic objects that can cast shadows
      GHashTable *sets;                      // Dynamic objects, these are not tiled
      GHashTable *visible;                   // Dynamic objects that are visible
      bool materials_dirty;
      struct {
         // UBO for dynamic object updates
         struct {
            VkdfBuffer buf;
            VkDeviceSize inst_size;
            VkDeviceSize size;
            void *host_buf;
         } obj;
         // UBO for dynamic material updates
         struct {
            VkdfBuffer buf;
            VkDeviceSize inst_size;
            VkDeviceSize size;
            void *host_buf;
         } material;
         // UBO for dynamic shadow map object updates
         struct {
            VkdfBuffer buf;
            VkDeviceSize inst_size;
            VkDeviceSize size;
            void *host_buf;
         } shadow_map;
      } ubo;
   } dynamic;
};

VkdfScene *
vkdf_scene_new(VkdfContext *ctx,
               VkdfCamera *camera,
               glm::vec3 scene_origin,
               glm::vec3 scene_size,
               glm::vec3 tile_size,
               uint32_t num_tile_levels,
               uint32_t cache_size,
               uint32_t num_threads);

void
vkdf_scene_free(VkdfScene *s);

inline VkdfCamera *
vkdf_scene_get_camera(VkdfScene *scene)
{
   return scene->camera;
}

void
vkdf_scene_add_object(VkdfScene *scene, const char *set_id, VkdfObject *obj);

void
vkdf_scene_set_clear_values(VkdfScene *scene,
                            VkClearValue *color,
                            VkClearValue *depth);

void
vkdf_scene_prepare(VkdfScene *scene);

inline VkRenderPass
vkdf_scene_get_depth_prepass_static_render_pass(VkdfScene *s)
{
   return s->rp.dpp_static_geom.renderpass;
}

inline VkRenderPass
vkdf_scene_get_depth_prepass_dynamic_render_pass(VkdfScene *s)
{
   return s->rp.dpp_dynamic_geom.renderpass;
}

inline VkRenderPass
vkdf_scene_get_static_render_pass(VkdfScene *s)
{
   return s->rp.static_geom.renderpass;
}

inline VkRenderPass
vkdf_scene_get_dynamic_render_pass(VkdfScene *s)
{
   return s->rp.dynamic_geom.renderpass;
}

inline VkdfImage *
vkdf_scene_get_color_render_target(VkdfScene *s)
{
   return &s->rt.color;
}

inline VkdfImage *
vkdf_scene_get_depth_render_target(VkdfScene *s)
{
   return &s->rt.depth;
}

inline VkdfBuffer *
vkdf_scene_get_object_ubo(VkdfScene *s)
{
   return &s->ubo.obj.buf;
}

inline VkDeviceSize
vkdf_scene_get_object_ubo_size(VkdfScene *s)
{
   return s->ubo.obj.size;
}

inline VkdfBuffer *
vkdf_scene_get_dynamic_object_ubo(VkdfScene *s)
{
   return &s->dynamic.ubo.obj.buf;
}

inline VkDeviceSize
vkdf_scene_get_dynamic_object_ubo_size(VkdfScene *s)
{
   return s->dynamic.ubo.obj.size;
}

inline VkdfBuffer *
vkdf_scene_get_material_ubo(VkdfScene *s)
{
   return &s->ubo.material.buf;
}

inline VkDeviceSize
vkdf_scene_get_material_ubo_size(VkdfScene *s)
{
   return s->ubo.material.size;
}

inline VkdfBuffer *
vkdf_scene_get_dynamic_material_ubo(VkdfScene *s)
{
   return &s->dynamic.ubo.material.buf;
}

inline VkDeviceSize
vkdf_scene_get_dynamic_material_ubo_size(VkdfScene *s)
{
   return s->dynamic.ubo.material.size;
}

inline uint32_t
vkdf_scene_get_num_lights(VkdfScene *s)
{
   return s->lights.size();
}

inline VkdfBuffer *
vkdf_scene_get_light_ubo(VkdfScene *s)
{
   return &s->ubo.light.buf;
}

inline VkDeviceSize
vkdf_scene_get_light_ubo_size(VkdfScene *s)
{
   return s->ubo.light.size;
}

inline void
vkdf_scene_get_light_ubo_range(VkdfScene *s,
                               VkDeviceSize *offset, VkDeviceSize *size)
{
   *offset = 0;
   *size = s->ubo.light.light_data_size;
}

inline void
vkdf_scene_get_shadow_map_ubo_range(VkdfScene *s,
                                    VkDeviceSize *offset, VkDeviceSize *size)
{
   *offset = s->ubo.light.shadow_map_data_offset;
   *size = s->ubo.light.shadow_map_data_size;
}

inline VkSampler
vkdf_scene_light_get_shadow_map_sampler(VkdfScene *s, uint32_t index)
{
   assert(index < s->lights.size());
   return s->lights[index]->shadow.sampler;
}

inline VkdfImage *
vkdf_scene_light_get_shadow_map_image(VkdfScene *s, uint32_t index)
{
   assert(index < s->lights.size());
   return &s->lights[index]->shadow.shadow_map;
}

inline uint32_t
vkdf_scene_get_static_object_count(VkdfScene *scene)
{
   return scene->static_obj_count;
}

inline uint32_t
vkdf_scene_get_object_count(VkdfScene *scene)
{
   return scene->obj_count;
}

inline VkdfSceneSetInfo *
vkdf_scene_get_dynamic_object_set(VkdfScene *s, const char *set_id)
{
   return (VkdfSceneSetInfo *) g_hash_table_lookup(s->dynamic.sets, set_id);
}

inline uint32_t
vkdf_scene_get_num_tiles(VkdfScene *s)
{
   return s->num_tiles.total;
}

void
vkdf_scene_add_light(VkdfScene *s,
                     VkdfLight *light,
                     VkdfSceneShadowSpec *shadow_spec);

inline void
vkdf_scene_set_scene_callbacks(VkdfScene *s,
                               VkdfSceneUpdateStateCB us_cb,
                               VkdfSceneUpdateResourcesCB ur_cb,
                               VkdfSceneCommandsCB cmd_cb,
                               VkdfScenePostprocessCB postprocess_cb,
                               void *data)
{
   s->callbacks.update_state = us_cb;
   s->callbacks.update_resources = ur_cb;
   s->callbacks.record_commands = cmd_cb;
   s->callbacks.postprocess = postprocess_cb;
   s->callbacks.data = data;
}

void
vkdf_scene_optimize_clip_boxes(VkdfScene *s);

void
vkdf_scene_enable_deferred_rendering(VkdfScene *s,
                                     VkdfSceneGbufferMergeCommandsCB merge_cb,
                                     uint32_t gbuffer_size,
                                     VkFormat format0,
                                     ...);

inline void
vkdf_scene_enable_depth_prepass(VkdfScene *s)
{
   s->rp.do_depth_prepass = true;
}

void
vkdf_scene_enable_ssao(VkdfScene *s,
                       float downsampling,
                       uint32_t num_samples,
                       float radius,
                       float bias,
                       float intensity,
                       uint32_t blur_size);

inline VkRenderPass
vkdf_scene_get_gbuffer_merge_render_pass(VkdfScene *s)
{
   return s->rp.gbuffer_merge.renderpass;
}

inline VkdfImage *
vkdf_scene_get_gbuffer_image(VkdfScene *s, uint32_t index)
{
   assert(index < s->rt.gbuffer_size);
   return &s->rt.gbuffer[index];
}

inline VkdfImage *
vkdf_scene_get_ssao_image(VkdfScene *s)
{
   if (s->ssao.blur_size > 0)
      return &s->ssao.blur.image;
   else
      return &s->ssao.base.image; /* No blur */
}

void
vkdf_scene_event_loop_run(VkdfScene *s);

inline void
vkdf_scene_shadow_spec_set(VkdfSceneShadowSpec *spec,
                           uint32_t shadow_map_size,
                           float near_plane,
                           float far_plane,
                           float depth_bias_const_factor,
                           float depth_bias_slope_factor,
                           uint32_t pcf_kernel_size)
{
   spec->shadow_map_size = shadow_map_size;
   spec->shadow_map_near = near_plane;
   spec->shadow_map_far = far_plane;
   spec->depth_bias_const_factor = depth_bias_const_factor;
   spec->depth_bias_slope_factor = depth_bias_slope_factor;
   spec->pcf_kernel_size = pcf_kernel_size;
}

#endif
