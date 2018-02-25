#include "vkdf.hpp"

#define JOIN(a,b) (a b)

#define SHADOW_MAP_SHADER_PATH JOIN(VKDF_DATA_DIR, "spirv/shadow-map.vert.spv")

#define SSAO_VS_SHADER_PATH JOIN(VKDF_DATA_DIR, "spirv/ssao.deferred.vert.spv")
#define SSAO_FS_SHADER_PATH JOIN(VKDF_DATA_DIR, "spirv/ssao.deferred.frag.spv")
#define SSAO_BLUR_VS_SHADER_PATH JOIN(VKDF_DATA_DIR, "spirv/ssao-blur.deferred.vert.spv")
#define SSAO_BLUR_FS_SHADER_PATH JOIN(VKDF_DATA_DIR, "spirv/ssao-blur.deferred.frag.spv")

#define FXAA_VS_SHADER_PATH JOIN(VKDF_DATA_DIR, "spirv/fxaa.vert.spv")
#define FXAA_FS_SHADER_PATH JOIN(VKDF_DATA_DIR, "spirv/fxaa.frag.spv")

#define TONE_MAP_VS_SHADER_PATH JOIN(VKDF_DATA_DIR, "spirv/tone-map.vert.spv")
#define TONE_MAP_FS_SHADER_PATH JOIN(VKDF_DATA_DIR, "spirv/tone-map.frag.spv")

/**
 * Input texture bindings for deferred SSAO base pass
 */
enum {
   SSAO_DEPTH_TEX_BINDING    = 0,
   SSAO_NORMAL_TEX_BINDING   = 1,
   SSAO_NOISE_TEX_BINDING    = 2,
};

static const uint32_t MAX_MATERIALS_PER_MODEL =   32;
static const uint32_t MAX_DYNAMIC_OBJECTS     = 1024;
static const uint32_t MAX_DYNAMIC_MODELS      =  128;
static const uint32_t MAX_DYNAMIC_MATERIALS   =  MAX_DYNAMIC_MODELS * MAX_MATERIALS_PER_MODEL;

struct FreeCmdBufInfo {
   uint32_t num_commands;
   VkCommandBuffer cmd_buf[2];
   VkdfSceneTile *tile;
};

static void inline
new_inactive_cmd_buf(VkdfScene *s, uint32_t thread_id, VkCommandBuffer cmd_buf);

static inline uint32_t
tile_index_from_tile_coords(VkdfScene *s, float tx, float ty, float tz)
{
   return ty * s->num_tiles.w * s->num_tiles.d +
          tz * s->num_tiles.w +
          tx;
}

static inline glm::vec3
tile_coord_from_position(VkdfScene *s, glm::vec3 pos)
{
   glm::vec3 tile_id;
   tile_id.x = truncf((pos.x - s->scene_area.origin.x) / s->tile_size[0].w);
   tile_id.y = truncf((pos.y - s->scene_area.origin.y) / s->tile_size[0].h);
   tile_id.z = truncf((pos.z - s->scene_area.origin.z) / s->tile_size[0].d);
   return tile_id;
}

static inline uint32_t
subtile_index_from_position(VkdfScene *s, VkdfSceneTile *t, glm::vec3 pos)
{
   struct _dim subtile_size = s->tile_size[t->level + 1];
   uint32_t x = truncf((pos.x - t->offset.x) / subtile_size.w);
   uint32_t y = truncf((pos.y - t->offset.y) / subtile_size.h);
   uint32_t z = truncf((pos.z - t->offset.z) / subtile_size.d);
   return (y << 2) + (z << 1) + x;
}

static void
init_subtiles(VkdfScene *s, VkdfSceneTile *t)
{
   uint32_t level = t->level + 1;
   if (level >= s->num_tile_levels)
      return;

   t->subtiles = g_new0(VkdfSceneTile, 8);

   struct _dim subtile_size = s->tile_size[level];

   for (uint32_t sty = 0; sty < 2; sty++)
   for (uint32_t stz = 0; stz < 2; stz++)
   for (uint32_t stx = 0; stx < 2; stx++) {
      uint32_t sti = (sty << 2) + (stz << 1) + stx;
      VkdfSceneTile *st = &t->subtiles[sti];
      st->parent = t->index;
      st->index = sti;
      st->level = level;

      st->offset = glm::vec3(t->offset.x + stx * subtile_size.w,
                             t->offset.y + sty * subtile_size.h,
                             t->offset.z + stz * subtile_size.d);

      st->box.center = st->offset + glm::vec3(subtile_size.w / 2.0f,
                                              subtile_size.h / 2.0f,
                                              subtile_size.d / 2.0f);
      st->box.w = 0.0f;
      st->box.h = 0.0f;
      st->box.d = 0.0f;

      st->sets = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

      init_subtiles(s, st);
   }
}

static void
prepare_present_from_image(VkdfScene *s, VkdfImage image)
{
   if (s->cmd_buf.present) {
      for (uint32_t i = 0; i < s->ctx->swap_chain_length; i++)
         new_inactive_cmd_buf(s, 0, s->cmd_buf.present[i]);
   }

   s->rt.output = image;

   s->cmd_buf.present =
      vkdf_command_buffer_create_for_present(s->ctx,
                                             s->cmd_buf.pool[0],
                                             s->rt.output.image,
                                             s->rt.width, s->rt.height,
                                             s->rt.present_filter);
}

static VkdfImage
create_color_framebuffer_image(VkdfScene *s, bool hdr)
{
   VkFormat format = hdr ? VK_FORMAT_R16G16B16A16_SFLOAT :
                           VK_FORMAT_R8G8B8A8_UNORM;

   return vkdf_create_image(s->ctx,
                            s->rt.width,
                            s->rt.height,
                            1,
                            VK_IMAGE_TYPE_2D,
                            format,
                            VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
                              VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT,
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                               VK_IMAGE_USAGE_SAMPLED_BIT |
                               VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                            VK_IMAGE_ASPECT_COLOR_BIT,
                            VK_IMAGE_VIEW_TYPE_2D);
}
static VkdfImage
create_depth_framebuffer_image(VkdfScene *s)
{
      return vkdf_create_image(s->ctx,
                               s->rt.width,
                               s->rt.height,
                               1,
                               VK_IMAGE_TYPE_2D,
                               VK_FORMAT_D32_SFLOAT,
                               VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                  VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT,
                               VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                  VK_IMAGE_USAGE_SAMPLED_BIT,
                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                               VK_IMAGE_ASPECT_DEPTH_BIT,
                               VK_IMAGE_VIEW_TYPE_2D);
}

static void
prepare_render_target(VkdfScene *s)
{
   assert(s->rt.width > 0 && s->rt.height > 0);

   s->rt.depth = create_depth_framebuffer_image(s);
   s->rt.color = create_color_framebuffer_image(s, s->hdr.enabled);
}

static void
create_gbuffer_image(VkdfScene *s, uint32_t idx, VkFormat format)
{
   VkFormatFeatureFlagBits features = (VkFormatFeatureFlagBits)
      (VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
       VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);

   VkImageUsageFlagBits usage = (VkImageUsageFlagBits)
      (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

   s->rt.gbuffer[idx] =
      vkdf_create_image(s->ctx,
                        s->rt.width,
                        s->rt.height,
                        1,
                        VK_IMAGE_TYPE_2D,
                        format,
                        features,
                        usage,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        VK_IMAGE_ASPECT_COLOR_BIT,
                        VK_IMAGE_VIEW_TYPE_2D);
}

void
vkdf_scene_enable_deferred_rendering(VkdfScene *s,
                                     VkdfSceneGbufferMergeCommandsCB merge_cb,
                                     uint32_t num_user_attachments,
                                     ...)
{
   s->rp.do_deferred = true;

   s->callbacks.gbuffer_merge = merge_cb;

   /* Compute GBuffer size as fixed slots plus user enabled slots */
   s->rt.gbuffer_size = GBUFFER_LAST_FIXED_IDX + num_user_attachments;
   assert(s->rt.gbuffer_size <= GBUFFER_MAX_SIZE);

   uint32_t max_attachments =
      s->ctx->phy_device_props.limits.maxFragmentOutputAttachments;
   if (s->rt.gbuffer_size > max_attachments)
      vkdf_fatal("Gbuffer has too many attachments");

   /* Create GBuffer images for fixed slots */
   for (uint32_t i = 0; i < GBUFFER_LAST_FIXED_IDX; i++)
      create_gbuffer_image(s, i, GBUFFER_FIXED_FORMATS[i]);

   /* Create GBuffer images for user slots */
   if (num_user_attachments > 0) {
      va_list ap;
      va_start(ap, num_user_attachments);

      for (uint32_t i = 0; i < num_user_attachments; i++) {
         VkFormat format_i = (VkFormat) va_arg(ap, uint32_t);
         create_gbuffer_image(s, GBUFFER_LAST_FIXED_IDX + i, format_i);
      }

      va_end(ap);
   }
}

void
vkdf_scene_enable_ssao(VkdfScene *s,
                       float downsampling,
                       uint32_t num_samples,
                       float radius,
                       float bias,
                       float intensity,
                       uint32_t blur_size,
                       float blur_threshold)
{
   /* Store the SSAO config and bail. SSAO depends on having depth info
    * available so we postpone creating resources for it until we can
    * verify this requirement. See prepare_scene_ssao().
    */
   s->ssao.enabled = true;

   assert(num_samples > 0);
   if (num_samples > 64) {
      vkdf_info("scene:ssao: clamping num_samples to 64");
      num_samples = 64;
   }
   s->ssao.num_samples = num_samples;

   /* We use a fixed 4x4 noise image (16 samples) */
   s->ssao.num_noise_samples = 16;
   s->ssao.noise_image_dim = 4;

   assert(radius > 0.0f && bias >= 0.0f);
   s->ssao.radius = radius;
   s->ssao.bias = bias;

   assert(intensity > 0.0f);
   s->ssao.intensity = intensity;

   assert(blur_size >= 0.0f);
   s->ssao.blur_size = blur_size;

   assert(blur_threshold >= 0.0f);
   s->ssao.blur_threshold = blur_threshold;

   assert(downsampling >= 1.0f);
   s->ssao.width = s->rt.width / downsampling;
   s->ssao.height = s->rt.height / downsampling;
}

VkdfScene *
vkdf_scene_new(VkdfContext *ctx,
               uint32_t fb_width,
               uint32_t fb_height,
               VkdfCamera *camera,
               glm::vec3 scene_origin,
               glm::vec3 scene_size,
               glm::vec3 tile_size,
               uint32_t num_tile_levels,
               uint32_t cache_size,
               uint32_t num_threads)
{
   VkdfScene *s = g_new0(VkdfScene, 1);

   s->ctx = ctx;

   s->camera = camera;

   assert(tile_size.x > 0.0f);
   assert(tile_size.z > 0.0f);
   assert(tile_size.z > 0.0f);
   assert(tile_size.x <= scene_size.x);
   assert(tile_size.y <= scene_size.y);
   assert(tile_size.z <= scene_size.z);
   assert(num_tile_levels > 0);
   assert(num_threads > 0);

   s->scene_area.origin = scene_origin;
   s->scene_area.w = scene_size.x;
   s->scene_area.h = scene_size.y;
   s->scene_area.d = scene_size.z;

   s->num_tile_levels = num_tile_levels;
   s->tile_size = g_new0(struct _dim, num_tile_levels);
   for (uint32_t i = 0; i < num_tile_levels; i++) {
      float divisor = (i == 0) ? 1.0f : powf(2.0f, i);
      s->tile_size[i].w = tile_size.x / divisor;
      s->tile_size[i].h = tile_size.y / divisor;
      s->tile_size[i].d = tile_size.z / divisor;
   }

   float half_tile_w = 0.5f * s->tile_size[0].w;
   float half_tile_h = 0.5f * s->tile_size[0].h;
   float half_tile_d = 0.5f * s->tile_size[0].d;

   s->num_tiles.w = truncf((s->scene_area.w + half_tile_w) / s->tile_size[0].w);
   s->num_tiles.h = truncf((s->scene_area.h + half_tile_h) / s->tile_size[0].h);
   s->num_tiles.d = truncf((s->scene_area.d + half_tile_d) / s->tile_size[0].d);

   s->num_tiles.total = s->num_tiles.w * s->num_tiles.h * s->num_tiles.d;
   s->tiles = g_new0(VkdfSceneTile, s->num_tiles.total);

   for (uint32_t ty = 0; ty < s->num_tiles.h; ty++)
   for (uint32_t tz = 0; tz < s->num_tiles.d; tz++)
   for (uint32_t tx = 0; tx < s->num_tiles.w; tx++) {
      uint32_t ti = tile_index_from_tile_coords(s, tx, ty, tz);

      VkdfSceneTile *t = &s->tiles[ti];
      t->parent = -1;
      t->level = 0;
      t->index = ti;

      t->offset = glm::vec3(s->scene_area.origin.x + tx * s->tile_size[0].w,
                            s->scene_area.origin.y + ty * s->tile_size[0].h,
                            s->scene_area.origin.z + tz * s->tile_size[0].d);

      t->dirty = false;

      t->box.center =
         t->offset + glm::vec3(half_tile_w, half_tile_h, half_tile_d);
      t->box.w = 0.0f;
      t->box.h = 0.0f;
      t->box.d = 0.0f;

      t->sets = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

      init_subtiles(s, t);
   }

   assert(num_threads <= s->num_tiles.total);

   s->thread.num_threads = num_threads;
   s->thread.work_size =
      (uint32_t) truncf((float) s->num_tiles.total / num_threads);
   if (num_threads > 1)
      s->thread.pool = vkdf_thread_pool_new(num_threads);

   s->cache = (struct _cache *) malloc(sizeof(struct _cache) * num_threads);
   for (uint32_t thread_idx = 0; thread_idx < num_threads; thread_idx++) {
      s->cache[thread_idx].max_size = cache_size;
      s->cache[thread_idx].size = 0;
      s->cache[thread_idx].cached = NULL;
   }

   s->cmd_buf.pool = g_new(VkCommandPool, num_threads);
   s->cmd_buf.active = g_new(GList *, num_threads);
   s->cmd_buf.free = g_new(GList *, num_threads);
   for (uint32_t thread_idx = 0; thread_idx < num_threads; thread_idx++) {
      s->cmd_buf.pool[thread_idx] =
         vkdf_create_gfx_command_pool(s->ctx,
                                      VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
      s->cmd_buf.active[thread_idx] = NULL;
      s->cmd_buf.free[thread_idx] = NULL;
   }
   s->cmd_buf.cur_idx = SCENE_CMD_BUF_LIST_SIZE - 1;

   s->thread.tile_data = g_new0(struct TileThreadData, num_threads);
   for (uint32_t thread_idx = 0; thread_idx < num_threads; thread_idx++) {
      s->thread.tile_data[thread_idx].id = thread_idx;
      s->thread.tile_data[thread_idx].s = s;
      s->thread.tile_data[thread_idx].first_idx =
         thread_idx * s->thread.work_size;
      s->thread.tile_data[thread_idx].last_idx =
         (thread_idx < num_threads - 1) ?
            s->thread.tile_data[thread_idx].first_idx +
               s->thread.work_size - 1 :
            s->num_tiles.total - 1;
   }

   s->sync.update_resources_sem = vkdf_create_semaphore(s->ctx);
   s->sync.shadow_maps_sem = vkdf_create_semaphore(s->ctx);
   s->sync.depth_draw_sem = vkdf_create_semaphore(s->ctx);
   s->sync.depth_draw_static_sem = vkdf_create_semaphore(s->ctx);
   s->sync.draw_sem = vkdf_create_semaphore(s->ctx);
   s->sync.draw_static_sem = vkdf_create_semaphore(s->ctx);
   s->sync.ssao_sem = vkdf_create_semaphore(s->ctx);;
   s->sync.gbuffer_merge_sem = vkdf_create_semaphore(s->ctx);
   s->sync.postprocess_sem = vkdf_create_semaphore(s->ctx);
   s->sync.present_fence = vkdf_create_fence(s->ctx);

   s->ubo.static_pool =
      vkdf_create_descriptor_pool(s->ctx, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 8);

   s->dynamic.sets =
      g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
   s->dynamic.visible =
      g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

   s->sampler.pool =
      vkdf_create_descriptor_pool(s->ctx,
                                  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                  32);

   s->rt.width = fb_width;
   s->rt.height = fb_height;
   s->rt.present_filter = VK_FILTER_NEAREST;

   return s;
}

static void
free_scene_set(VkdfSceneSetInfo *info, bool full_destroy)
{
   if (full_destroy)
      g_list_free_full(info->objs, (GDestroyNotify) vkdf_object_free);
   else
      g_list_free(info->objs);
   g_free(info);
}

static void
destroy_set_full(gpointer key, gpointer value, gpointer data)
{
   VkdfSceneSetInfo *info = (VkdfSceneSetInfo *) value;
   free_scene_set(info, true);
}

static void
destroy_set(gpointer key, gpointer value, gpointer data)
{
   VkdfSceneSetInfo *info = (VkdfSceneSetInfo *) value;
   free_scene_set(info, false);
}

static void
destroy_light_shadow_map(VkdfScene *s, VkdfSceneLight *slight)
{
   if (slight->shadow.shadow_map.image)
      vkdf_destroy_image(s->ctx, &slight->shadow.shadow_map);
   if (slight->shadow.visible)
      g_list_free(slight->shadow.visible);
   if (slight->shadow.framebuffer)
      vkDestroyFramebuffer(s->ctx->device, slight->shadow.framebuffer, NULL);
   if (slight->shadow.sampler)
      vkDestroySampler(s->ctx->device, slight->shadow.sampler, NULL);
}

static void
destroy_light(VkdfScene *s, VkdfSceneLight *slight)
{
   vkdf_light_free(slight->light);
   destroy_light_shadow_map(s, slight);
   g_free(slight);
}

static void
free_tile(VkdfSceneTile *t)
{
   g_hash_table_foreach(t->sets,
                        t->subtiles ? destroy_set : destroy_set_full, NULL);
   g_hash_table_destroy(t->sets);
   t->sets = NULL;

   if (t->subtiles)
   {
      for (uint32_t i = 0; i < 8; i++)
         free_tile(&t->subtiles[i]);
      g_free(t->subtiles);
      t->subtiles = NULL;
   }
}

static void
free_dynamic_objects(VkdfScene *s)
{
   g_hash_table_foreach(s->dynamic.sets, destroy_set_full, NULL);
   g_hash_table_destroy(s->dynamic.sets);
   s->dynamic.sets = NULL;
}

static void
destroy_shadow_map_pipeline(gpointer key, gpointer value, gpointer data)
{
   VkdfScene *s = (VkdfScene *) data;
   VkPipeline pipeline = (VkPipeline) value;
   vkDestroyPipeline(s->ctx->device, pipeline, NULL);
}

static void
destroy_ssao_resources(VkdfScene *s)
{
   assert(s->ssao.enabled);

   /* Pipeline layouts and descriptor sets  */
   vkDestroyPipeline(s->ctx->device, s->ssao.base.pipeline.pipeline, NULL);
   vkDestroyPipelineLayout(s->ctx->device, s->ssao.base.pipeline.layout, NULL);

   vkFreeDescriptorSets(s->ctx->device, s->ubo.static_pool,
                        1, &s->ssao.base.pipeline.samples_set);
   vkDestroyDescriptorSetLayout(s->ctx->device,
                                s->ssao.base.pipeline.samples_set_layout, NULL);

   vkFreeDescriptorSets(s->ctx->device, s->sampler.pool,
                        1, &s->ssao.base.pipeline.textures_set);
   vkDestroyDescriptorSetLayout(s->ctx->device,
                                s->ssao.base.pipeline.textures_set_layout, NULL);

   /* Shaders */
   vkDestroyShaderModule(s->ctx->device, s->ssao.base.pipeline.shader.vs, NULL);
   vkDestroyShaderModule(s->ctx->device, s->ssao.base.pipeline.shader.fs, NULL);

   /* Samples buffer */
   vkdf_destroy_buffer(s->ctx, &s->ssao.samples_buf.buf);

   /* Noise texture and sampler */
   vkDestroySampler(s->ctx->device, s->ssao.noise_sampler, NULL);
   vkdf_destroy_image(s->ctx, &s->ssao.noise_image);

   /* Gbuffer sampler */
   vkDestroySampler(s->ctx->device, s->ssao.base.gbuffer_sampler, NULL);

   /* SSAO render targets */
   vkDestroyRenderPass(s->ctx->device, s->ssao.base.rp.renderpass, NULL);
   vkDestroyFramebuffer(s->ctx->device, s->ssao.base.rp.framebuffer, NULL);
   vkdf_destroy_image(s->ctx, &s->ssao.base.image);

   /* SSAO blur resources  */
   if (s->ssao.blur_size > 0) {
      vkDestroyPipeline(s->ctx->device, s->ssao.blur.pipeline.pipeline, NULL);
      vkDestroyPipelineLayout(s->ctx->device,
                              s->ssao.blur.pipeline.layout, NULL);

      vkDestroyDescriptorSetLayout(s->ctx->device,
                                   s->ssao.blur.pipeline.ssao_tex_set_layout,
                                   NULL);

      vkDestroyShaderModule(s->ctx->device,
                            s->ssao.blur.pipeline.shader.vs, NULL);
      vkDestroyShaderModule(s->ctx->device,
                            s->ssao.blur.pipeline.shader.fs, NULL);

      vkDestroySampler(s->ctx->device, s->ssao.blur.input_sampler, NULL);

      vkDestroyRenderPass(s->ctx->device, s->ssao.blur.rp.renderpass, NULL);
      vkDestroyFramebuffer(s->ctx->device, s->ssao.blur.rp.framebuffer, NULL);
      vkdf_destroy_image(s->ctx, &s->ssao.blur.image);
   }
}

static void
destroy_hdr_resources(VkdfScene *s)
{
   assert(s->hdr.enabled);

   /* Pipeline layouts and descriptor sets  */
   vkDestroyPipeline(s->ctx->device, s->hdr.pipeline.pipeline, NULL);
   vkDestroyPipelineLayout(s->ctx->device, s->hdr.pipeline.layout, NULL);

   vkFreeDescriptorSets(s->ctx->device, s->sampler.pool,
                        1, &s->hdr.pipeline.input_set);
   vkDestroyDescriptorSetLayout(s->ctx->device,
                                s->hdr.pipeline.input_set_layout, NULL);

   /* Source image sampler */
   vkDestroySampler(s->ctx->device, s->hdr.input_sampler, NULL);

   /* Shaders */
   vkDestroyShaderModule(s->ctx->device, s->hdr.pipeline.shader.vs, NULL);
   vkDestroyShaderModule(s->ctx->device, s->hdr.pipeline.shader.fs, NULL);


   /* Render target */
   vkDestroyRenderPass(s->ctx->device, s->hdr.rp.renderpass, NULL);
   vkDestroyFramebuffer(s->ctx->device, s->hdr.rp.framebuffer, NULL);
   vkdf_destroy_image(s->ctx, &s->hdr.output);
}

static void
destroy_fxaa_resources(VkdfScene *s)
{
   assert(s->fxaa.enabled);

   /* Pipeline layouts and descriptor sets  */
   vkDestroyPipeline(s->ctx->device, s->fxaa.pipeline.pipeline, NULL);
   vkDestroyPipelineLayout(s->ctx->device, s->fxaa.pipeline.layout, NULL);

   vkFreeDescriptorSets(s->ctx->device, s->sampler.pool,
                        1, &s->fxaa.pipeline.input_set);
   vkDestroyDescriptorSetLayout(s->ctx->device,
                                s->fxaa.pipeline.input_set_layout, NULL);

   /* Source image sampler */
   vkDestroySampler(s->ctx->device, s->fxaa.input_sampler, NULL);

   /* Shaders */
   vkDestroyShaderModule(s->ctx->device, s->fxaa.pipeline.shader.vs, NULL);
   vkDestroyShaderModule(s->ctx->device, s->fxaa.pipeline.shader.fs, NULL);


   /* Render target */
   vkDestroyRenderPass(s->ctx->device, s->fxaa.rp.renderpass, NULL);
   vkDestroyFramebuffer(s->ctx->device, s->fxaa.rp.framebuffer, NULL);
   vkdf_destroy_image(s->ctx, &s->fxaa.output);
}

void
vkdf_scene_free(VkdfScene *s)
{
   while (s->sync.present_fence_active) {
      VkResult status;
      do {
         status = vkWaitForFences(s->ctx->device,
                                  1, &s->sync.present_fence,
                                  true, 1000ull);
      } while (status == VK_NOT_READY || status == VK_TIMEOUT);
      vkResetFences(s->ctx->device, 1, &s->sync.present_fence);
      s->sync.present_fence_active = false;
   }

   if (s->thread.pool) {
      vkdf_thread_pool_wait(s->thread.pool);
      vkdf_thread_pool_free(s->thread.pool);
   }

   vkdf_destroy_image(s->ctx, &s->rt.depth);
   vkdf_destroy_image(s->ctx, &s->rt.color);
   for (uint32_t i = 0; i < s->rt.gbuffer_size; i++)
      vkdf_destroy_image(s->ctx, &s->rt.gbuffer[i]);

   vkDestroyRenderPass(s->ctx->device, s->rp.static_geom.renderpass, NULL);
   vkDestroyRenderPass(s->ctx->device, s->rp.dynamic_geom.renderpass, NULL);
   if (s->rp.do_deferred)
      vkDestroyRenderPass(s->ctx->device, s->rp.gbuffer_merge.renderpass, NULL);
   if (s->rp.do_depth_prepass) {
      vkDestroyRenderPass(s->ctx->device,
                          s->rp.dpp_static_geom.renderpass, NULL);
      vkDestroyRenderPass(s->ctx->device,
                          s->rp.dpp_dynamic_geom.renderpass, NULL);
   }

   vkDestroyFramebuffer(s->ctx->device, s->rp.static_geom.framebuffer, NULL);
   vkDestroyFramebuffer(s->ctx->device, s->rp.dynamic_geom.framebuffer, NULL);
   if (s->rp.do_deferred)
      vkDestroyFramebuffer(s->ctx->device, s->rp.gbuffer_merge.framebuffer, NULL);
   if (s->rp.do_depth_prepass) {
      vkDestroyFramebuffer(s->ctx->device,
                           s->rp.dpp_static_geom.framebuffer, NULL);
      vkDestroyFramebuffer(s->ctx->device,
                           s->rp.dpp_dynamic_geom.framebuffer, NULL);
   }

   for (uint32_t i = 0; i < s->thread.num_threads; i++)
      g_list_free(s->thread.tile_data[i].visible);
   g_free(s->thread.tile_data);

   g_list_free_full(s->set_ids, g_free);
   s->set_ids = NULL;

   g_list_free(s->models);
   s->models = NULL;

   for (uint32_t i = 0; i < s->num_tiles.total; i++)
      free_tile(&s->tiles[i]);
   g_free(s->tiles);

   free_dynamic_objects(s);
   g_free(s->dynamic.ubo.obj.host_buf);
   g_free(s->dynamic.ubo.material.host_buf);
   g_free(s->dynamic.ubo.shadow_map.host_buf);

   for (uint32_t i = 0; i < s->lights.size(); i++)
      destroy_light(s, s->lights[i]);
   s->lights.clear();
   std::vector<VkdfSceneLight *>(s->lights).swap(s->lights);

   vkDestroySemaphore(s->ctx->device, s->sync.update_resources_sem, NULL);
   vkDestroySemaphore(s->ctx->device, s->sync.shadow_maps_sem, NULL);
   vkDestroySemaphore(s->ctx->device, s->sync.depth_draw_sem, NULL);
   vkDestroySemaphore(s->ctx->device, s->sync.depth_draw_static_sem, NULL);
   vkDestroySemaphore(s->ctx->device, s->sync.draw_sem, NULL);
   vkDestroySemaphore(s->ctx->device, s->sync.draw_static_sem, NULL);
   vkDestroySemaphore(s->ctx->device, s->sync.gbuffer_merge_sem, NULL);
   vkDestroySemaphore(s->ctx->device, s->sync.ssao_sem, NULL);
   vkDestroySemaphore(s->ctx->device, s->sync.postprocess_sem, NULL);
   vkDestroyFence(s->ctx->device, s->sync.present_fence, NULL);

   for (uint32_t i = 0; i < s->thread.num_threads; i++) {
      g_list_free(s->cache[i].cached);
      g_list_free(s->cmd_buf.active[i]);
      g_list_free(s->cmd_buf.free[i]);
      g_list_free(s->cache[i].cached);
      vkDestroyCommandPool(s->ctx->device, s->cmd_buf.pool[i], NULL);
   }
   g_free(s->cache);
   g_free(s->cmd_buf.active);
   g_free(s->cmd_buf.free);
   g_free(s->cmd_buf.pool);
   g_free(s->cmd_buf.present);
   g_free(s->tile_size);

   if (s->shadows.renderpass)
      vkDestroyRenderPass(s->ctx->device, s->shadows.renderpass, NULL);

   if (s->shadows.pipeline.models_set_layout) {
      vkDestroyDescriptorSetLayout(s->ctx->device,
                                   s->shadows.pipeline.models_set_layout,
                                   NULL);
   }

   if (s->shadows.pipeline.layout)
      vkDestroyPipelineLayout(s->ctx->device, s->shadows.pipeline.layout, NULL);

   if (s->shadows.pipeline.pipelines) {
      g_hash_table_foreach(s->shadows.pipeline.pipelines,
                           destroy_shadow_map_pipeline, s);
      g_hash_table_destroy(s->shadows.pipeline.pipelines);
   }

   if (s->shadows.shaders.vs)
     vkDestroyShaderModule(s->ctx->device, s->shadows.shaders.vs, NULL);

   if (s->ssao.enabled)
      destroy_ssao_resources(s);

   if (s->hdr.enabled)
      destroy_hdr_resources(s);

   if (s->fxaa.enabled)
      destroy_fxaa_resources(s);

   // FIXME: have a list of buffers in the scene so that here we can just go
   // through the list and destory all of them without having to add another
   // deleter every time we start using a new buffer.
   if (s->ubo.obj.buf.buf)
      vkdf_destroy_buffer(s->ctx, &s->ubo.obj.buf);

   if (s->dynamic.ubo.obj.buf.buf)
      vkdf_destroy_buffer(s->ctx, &s->dynamic.ubo.obj.buf);

   if (s->ubo.material.buf.buf)
      vkdf_destroy_buffer(s->ctx, &s->ubo.material.buf);

   if (s->dynamic.ubo.material.buf.buf)
      vkdf_destroy_buffer(s->ctx, &s->dynamic.ubo.material.buf);

   if (s->ubo.light.buf.buf)
      vkdf_destroy_buffer(s->ctx, &s->ubo.light.buf);

   if (s->ubo.shadow_map.buf.buf)
      vkdf_destroy_buffer(s->ctx, &s->ubo.shadow_map.buf);

   if (s->dynamic.ubo.shadow_map.buf.buf)
      vkdf_destroy_buffer(s->ctx, &s->dynamic.ubo.shadow_map.buf);

   vkDestroyDescriptorPool(s->ctx->device, s->ubo.static_pool, NULL);
   vkDestroyDescriptorPool(s->ctx->device, s->sampler.pool, NULL);

   g_free(s);
}

static void
update_tile_box_to_fit_box(VkdfSceneTile *t,
                           glm::vec3 min_box, glm::vec3 max_box)
{
   glm::vec3 min_bounds;
   glm::vec3 max_bounds;

   if (t->obj_count == 1) {
      min_bounds = min_box;
      max_bounds = max_box;
   } else {
      min_bounds = t->box.center - glm::vec3(t->box.w, t->box.h, t->box.d);
      max_bounds = t->box.center + glm::vec3(t->box.w, t->box.h, t->box.d);

      if (min_box.x < min_bounds.x)
         min_bounds.x = min_box.x;
      if (max_box.x > max_bounds.x)
         max_bounds.x = max_box.x;

      if (min_box.y < min_bounds.y)
         min_bounds.y = min_box.y;
      if (max_box.y > max_bounds.y)
         max_bounds.y = max_box.y;

      if (min_box.z < min_bounds.z)
         min_bounds.z = min_box.z;
      if (max_box.z > max_bounds.z)
         max_bounds.z = max_box.z;
   }

   t->box.w = (max_bounds.x - min_bounds.x) / 2.0f;
   t->box.h = (max_bounds.y - min_bounds.y) / 2.0f;
   t->box.d = (max_bounds.z - min_bounds.z) / 2.0f;
   t->box.center = glm::vec3(min_bounds.x + t->box.w,
                             min_bounds.y + t->box.h,
                             min_bounds.z + t->box.d);
}

static inline bool
set_id_is_registered(VkdfScene *s, const char *id)
{
   GList *iter = s->set_ids;
   while (iter) {
      if (!strcmp((const char *) iter->data, id))
         return true;
      iter = g_list_next(iter);
   }
   return false;
}

static void
add_static_object(VkdfScene *s, const char *set_id, VkdfObject *obj)
{
   bool is_shadow_caster = vkdf_object_casts_shadows(obj);

   // Find tile this object belongs to
   glm::vec3 tile_coord = tile_coord_from_position(s, obj->pos);

   uint32_t ti =
      tile_index_from_tile_coords(s,
                                  (uint32_t) tile_coord.x,
                                  (uint32_t) tile_coord.y,
                                  (uint32_t) tile_coord.z);
   VkdfSceneTile *tile = &s->tiles[ti];

   tile->obj_count++;
   if (is_shadow_caster)
      tile->shadow_caster_count++;
   tile->dirty = true;

   // Update the tile's box to fit this object
   VkdfBox *box = vkdf_object_get_box(obj);
   glm::vec3 min_box = box->center - glm::vec3(box->w, box->h, box->d);
   glm::vec3 max_box = box->center + glm::vec3(box->w, box->h, box->d);

   update_tile_box_to_fit_box(tile, min_box, max_box);

   // Add the objects to subtiles of its tile
   while (tile->subtiles) {
      uint32_t subtile_idx = subtile_index_from_position(s, tile, obj->pos);
      VkdfSceneTile *subtile = &tile->subtiles[subtile_idx];

      subtile->obj_count++;
      if (is_shadow_caster)
         subtile->shadow_caster_count++;
      subtile->dirty = true;

      update_tile_box_to_fit_box(subtile, min_box, max_box);

      tile = subtile;
   }

   // Only actually put the object in the bottom-most tile of the hierarchy
   // When the user calls vkdf_scene_prepare() we will create the lists
   // for non-leaf tiles in the hierarchy.
   VkdfSceneSetInfo *info =
      (VkdfSceneSetInfo *) g_hash_table_lookup(tile->sets, set_id);
   if (!info) {
      info = g_new0(VkdfSceneSetInfo, 1);
      g_hash_table_replace(tile->sets, g_strdup(set_id), info);
   }
   info->objs = g_list_prepend(info->objs, obj);
   info->count++;
   if (is_shadow_caster)
      info->shadow_caster_count++;

   s->static_obj_count++;
   if (is_shadow_caster)
      s->static_shadow_caster_count++;
}

static void
add_dynamic_object(VkdfScene *s, const char *set_id, VkdfObject *obj)
{
   // FIXME: for dynamic objects a hashtable might not be the best choice...
   VkdfSceneSetInfo *info =
      (VkdfSceneSetInfo *) g_hash_table_lookup(s->dynamic.sets, set_id);
   if (!info) {
      info = g_new0(VkdfSceneSetInfo, 1);
      g_hash_table_replace(s->dynamic.sets, g_strdup(set_id), info);

      // If this is the first time we added this type of dynamic object
      // we will need to update the dynamic materials UBO
      s->dynamic.materials_dirty = true;
   }
   info->objs = g_list_prepend(info->objs, obj);
   info->count++;
   if (vkdf_object_casts_shadows(obj))
      info->shadow_caster_count++;
}

void
vkdf_scene_add_object(VkdfScene *s, const char *set_id, VkdfObject *obj)
{
   assert(obj->model);

   if (!set_id_is_registered(s, set_id)) {
      s->set_ids = g_list_prepend(s->set_ids, g_strdup(set_id));
      s->models = g_list_prepend(s->models, obj->model);
   }

   if (!vkdf_object_is_dynamic(obj))
      add_static_object(s, set_id, obj);
   else
      add_dynamic_object(s, set_id, obj);

   s->obj_count++;
   s->dirty = true;
}

static inline VkdfImage
create_shadow_map_image(VkdfScene *s, uint32_t size)
{
   const VkFormatFeatureFlagBits shadow_map_features =
      (VkFormatFeatureFlagBits)
         (VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT |
          VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);

   const VkImageUsageFlagBits shadow_map_usage =
   (VkImageUsageFlagBits) (VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                           VK_IMAGE_USAGE_SAMPLED_BIT);

   return vkdf_create_image(s->ctx,
                            size,
                            size,
                            1,
                            VK_IMAGE_TYPE_2D,
                            VK_FORMAT_D32_SFLOAT,
                            shadow_map_features,
                            shadow_map_usage,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                            VK_IMAGE_ASPECT_DEPTH_BIT,
                            VK_IMAGE_VIEW_TYPE_2D);
}

static inline glm::vec3
compute_light_space_frustum_vertex(glm::mat4 *view_matrix,
                                   glm::vec3 p,
                                   glm::vec3 dir,
                                   float dist)
{
   vkdf_vec3_normalize(&dir);
   p = p + dir * dist;
   glm::vec4 tmp = (*view_matrix) * glm::vec4(p.x, p.y, p.z, 1.0f);
   return vec3(tmp);
}


static void
compute_directional_light_projection(VkdfSceneLight *sl, VkdfCamera *cam)
{
   const glm::mat4 clip = glm::mat4(1.0f, 0.0f, 0.0f, 0.0f,
                                    0.0f,-1.0f, 0.0f, 0.0f,
                                    0.0f, 0.0f, 0.5f, 0.0f,
                                    0.0f, 0.0f, 0.5f, 1.0f);

   assert(vkdf_light_get_type(sl->light) == VKDF_LIGHT_DIRECTIONAL);
   VkdfSceneShadowSpec *spec = &sl->shadow.spec;

   /* Compute camera's frustum */
   VkdfFrustum f;
   vkdf_frustum_compute(&f, false, false,
                        cam->pos, cam->rot,
                        spec->shadow_map_near, spec->shadow_map_far,
                        cam->proj.fov, cam->proj.aspect_ratio);

   /* Translate frustum to light-space to compute shadow box dimensions */
   const glm::mat4 *view = vkdf_light_get_view_matrix(sl->light);
   for (uint32_t i = 0; i < 8; i++) {
      f.vertices[i] = (*view) * vec4(f.vertices[i], 1.0f);
   }
   vkdf_frustum_compute_box(&f);

   const VkdfBox *box = vkdf_frustum_get_box(&f);
   float w = 2.0f * box->w * spec->directional.scale.x;
   float h = 2.0f * box->h * spec->directional.scale.y;
   float d = 2.0f * box->d * spec->directional.scale.z;

   /* Use the light-space dimensions to compute the orthogonal
    * projection matrix
    */
   glm::mat4 proj(1.0f);
   proj[0][0] =  2.0f / w;
   proj[1][1] =  2.0f / h;
   proj[2][2] = -2.0f / d;
   proj[3][3] =  1.0f;

   sl->shadow.proj = clip * proj;
   sl->shadow.directional.box = *box;

   /* Record the camera parameters used to capture the shadow map */
   sl->shadow.directional.cam_pos = cam->pos;
   sl->shadow.directional.cam_rot = cam->rot;
}

static void
compute_spotlight_projection(VkdfSceneLight *sl)
{
   const glm::mat4 clip = glm::mat4(1.0f, 0.0f, 0.0f, 0.0f,
                                    0.0f,-1.0f, 0.0f, 0.0f,
                                    0.0f, 0.0f, 0.5f, 0.0f,
                                    0.0f, 0.0f, 0.5f, 1.0f);

   assert(vkdf_light_get_type(sl->light) == VKDF_LIGHT_SPOTLIGHT);
   VkdfSceneShadowSpec *spec = &sl->shadow.spec;
   float cutoff_angle = vkdf_light_get_cutoff_angle(sl->light);
   sl->shadow.proj = clip * glm::perspective(2.0f * cutoff_angle,
                                             1.0f,
                                             spec->shadow_map_near,
                                             spec->shadow_map_far);
}

static void
compute_light_projection(VkdfScene *s, VkdfSceneLight *sl)
{
   switch (vkdf_light_get_type(sl->light)) {
   case VKDF_LIGHT_DIRECTIONAL:
      compute_directional_light_projection(sl, s->camera);
      break;
   case VKDF_LIGHT_SPOTLIGHT:
      compute_spotlight_projection(sl);
      break;
   default:
      // FIXME: point lights
      assert(!"unsupported light type");
      break;
   }
}

static inline void
compute_light_view_projection(VkdfScene *s, VkdfSceneLight *sl)
{
   const glm::mat4 *view = vkdf_light_get_view_matrix(sl->light);
   if (vkdf_light_get_type(sl->light) != VKDF_LIGHT_DIRECTIONAL) {
      sl->shadow.viewproj = sl->shadow.proj * (*view);
      return;
   }

   /* The view matrix for directional lights needs to be translated to the
    * center of its shadow box in world-space.
    */
   glm::mat4 final_view;
   const glm::mat4 *view_inv = vkdf_light_get_view_matrix_inv(sl->light);
   glm::vec3 offset =
      vec3((*view_inv) * vec4(sl->shadow.directional.box.center, 1.0f));
   glm::vec3 dir = vkdf_camera_get_viewdir(s->camera);
   offset += dir * sl->shadow.spec.directional.offset;
   final_view = glm::translate((*view), -offset);
   sl->shadow.viewproj = sl->shadow.proj * final_view;
}

static void
scene_light_disable_shadows(VkdfScene *s, VkdfSceneLight *sl)
{
   destroy_light_shadow_map(s, sl);
   vkdf_light_enable_shadows(sl->light, false);
   vkdf_light_set_dirty_shadows(sl->light, false);
}

static void
scene_light_enable_shadows(VkdfScene *s,
                           VkdfSceneLight *sl,
                           VkdfSceneShadowSpec *spec)
{
   assert(spec->pcf_kernel_size >= 1);

   vkdf_light_enable_shadows(sl->light, true);

   sl->shadow.spec = *spec;
   sl->shadow.shadow_map = create_shadow_map_image(s, spec->shadow_map_size);
   sl->shadow.sampler =
      vkdf_create_shadow_sampler(s->ctx,
                                 VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                 VK_FILTER_LINEAR,
                                 VK_SAMPLER_MIPMAP_MODE_NEAREST);

   /* Make sure we compute the shadow map immediately */
   sl->shadow.frame_counter = -1;

   compute_light_projection(s, sl);

   vkdf_light_set_dirty_shadows(sl->light, true);
   s->has_shadow_caster_lights = true;
}

static void
scene_light_update_shadow_spec(VkdfScene *s,
                               VkdfSceneLight *sl,
                               VkdfSceneShadowSpec *spec)
{
   assert(vkdf_light_casts_shadows(sl->light));

   /* We don't support changing the shadow map size dynamically */
   assert(sl->shadow.spec.shadow_map_size == spec->shadow_map_size);
   sl->shadow.spec = *spec;

   compute_light_projection(s, sl);
   vkdf_light_set_dirty_shadows(sl->light, true);
}

void
vkdf_scene_light_update_shadow_spec(VkdfScene *s,
                                    uint32_t index,
                                    VkdfSceneShadowSpec *spec)
{
   assert(index < s->lights.size());
   VkdfSceneLight *sl = s->lights[index];
   VkdfLight *l = sl->light;

   /* If the light already had shadows enabled then disable (if spec is NULL)
    * or update. If it didn't have shadows, then enable them.
    */
   if (vkdf_light_casts_shadows(l)) {
      if (spec)
         scene_light_update_shadow_spec(s, sl, spec);
      else
         scene_light_disable_shadows(s, sl);
   } else {
      if (spec)
         scene_light_enable_shadows(s, sl, spec);
   }
}

void
vkdf_scene_add_light(VkdfScene *s,
                     VkdfLight *light,
                     VkdfSceneShadowSpec *spec)
{
   VkdfSceneLight *slight = g_new0(VkdfSceneLight, 1);
   slight->light = light;

   if (spec)
      scene_light_enable_shadows(s, slight, spec);
   else
      scene_light_disable_shadows(s, slight);

   /* Mark the light dirty so it is included in the lights UBO */
   vkdf_light_set_dirty(light, true);

   slight->dirty_frustum = true;

   s->lights.push_back(slight);
}

static int
compare_distance(const void *a, const void *b,  void *data)
{
   VkdfSceneTile *t1 = (VkdfSceneTile *) a;
   VkdfSceneTile *t2 = (VkdfSceneTile *) b;
   glm::vec3 *cam_pos = (glm::vec3 *) data;

   float d1 = vkdf_vec3_module(t1->box.center - *cam_pos, 1, 1, 1);
   float d2 = vkdf_vec3_module(t2->box.center - *cam_pos, 1, 1, 1);
   return d1 < d2 ? -1 : d1 > d2 ? 1 : 0;
}

static inline GList *
sort_active_tiles_by_distance(VkdfScene *s)
{
   GList *list = NULL;
   for (uint32_t i = 0; i < s->thread.num_threads; i++) {
      GList *iter = s->cmd_buf.active[i];
      while (iter) {
         list = g_list_prepend(list, iter->data);
         iter = g_list_next(iter);
      }
   }

   glm::vec3 cam_pos = vkdf_camera_get_position(s->camera);
   return g_list_sort_with_data(list, compare_distance, &cam_pos);
}

static void inline
new_inactive_cmd_buf(VkdfScene *s, uint32_t thread_id, VkCommandBuffer cmd_buf)
{
   struct FreeCmdBufInfo *info = g_new(struct FreeCmdBufInfo, 1);
   info->num_commands = 1;
   info->cmd_buf[0] = cmd_buf;
   info->cmd_buf[1] = 0;
   info->tile = NULL;
   s->cmd_buf.free[thread_id] =
      g_list_prepend(s->cmd_buf.free[thread_id], info);
}

static void
record_primary_cmd_buf(VkCommandBuffer cmd_buf,
                       VkRenderPassBeginInfo *rp_begin,
                       uint32_t cmd_buf_count,
                       VkCommandBuffer *cmd_bufs)
{
   vkdf_command_buffer_begin(cmd_buf,
                             VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT);

   vkCmdBeginRenderPass(cmd_buf, rp_begin,
                        VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);

   if (cmd_buf_count > 0)
      vkCmdExecuteCommands(cmd_buf, cmd_buf_count, cmd_bufs);

   vkCmdEndRenderPass(cmd_buf);

   vkdf_command_buffer_end(cmd_buf);
}

static void
build_primary_cmd_buf(VkdfScene *s)
{
   s->cmd_buf.cur_idx = (s->cmd_buf.cur_idx + 1) % SCENE_CMD_BUF_LIST_SIZE;

   VkCommandBuffer *primary = &s->cmd_buf.primary[s->cmd_buf.cur_idx];
   VkCommandBuffer *dpp_primary = &s->cmd_buf.dpp_primary[s->cmd_buf.cur_idx];

   if (*primary)
      vkResetCommandBuffer(*primary, 0);
   if (*dpp_primary)
      vkResetCommandBuffer(*dpp_primary, 0);

   VkCommandBuffer cmd_buf[2];
   if (*primary == 0) {
      vkdf_create_command_buffer(s->ctx,
                                 s->cmd_buf.pool[0],
                                 VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                 s->rp.do_depth_prepass ? 2 : 1, cmd_buf);
   } else {
      cmd_buf[0] = *primary;
      cmd_buf[1] = *dpp_primary;
   }

   GList *active = sort_active_tiles_by_distance(s);

   uint32_t cmd_buf_count = g_list_length(active);

   VkCommandBuffer *secondaries = NULL;
   if (cmd_buf_count > 0) {
      uint32_t multiplier = s->rp.do_depth_prepass ? 2 : 1;
      secondaries =
         g_new(VkCommandBuffer,  multiplier * cmd_buf_count);
      uint32_t idx = 0;
      GList *iter = active;
      while (iter) {
         VkdfSceneTile *t = (VkdfSceneTile *) iter->data;
         assert(t->cmd_buf != 0);
         assert(!s->rp.do_depth_prepass || t->depth_cmd_buf != 0);
         secondaries[idx] = t->cmd_buf;
         if (s->rp.do_depth_prepass)
            secondaries[cmd_buf_count + idx] = t->depth_cmd_buf;
         idx++;
         iter = g_list_next(iter);
      }
   }

   uint32_t num_clear_values;
   VkClearValue *clear_values;
   if (s->rp.do_deferred) {
      num_clear_values = 1 + s->rt.gbuffer_size;
      clear_values = s->rp.gbuffer_clear_values;
   } else {
      num_clear_values = 2;
      clear_values = s->rp.clear_values;
   }

   VkRenderPassBeginInfo rp_begin =
      vkdf_renderpass_begin_new(s->rp.static_geom.renderpass,
                                s->rp.static_geom.framebuffer,
                                0, 0, s->rt.width, s->rt.height,
                                num_clear_values, clear_values);

   record_primary_cmd_buf(cmd_buf[0], &rp_begin, cmd_buf_count, secondaries);
   *primary = cmd_buf[0];

   if (s->rp.do_depth_prepass) {
      num_clear_values = 1;
      clear_values = &s->rp.clear_values[1]; /* depth clear value */

      rp_begin =
         vkdf_renderpass_begin_new(s->rp.dpp_static_geom.renderpass,
                                   s->rp.dpp_static_geom.framebuffer,
                                   0, 0, s->rt.width, s->rt.height,
                                   num_clear_values, clear_values);

      record_primary_cmd_buf(cmd_buf[1], &rp_begin,
                             cmd_buf_count, &secondaries[cmd_buf_count]);
      *dpp_primary = cmd_buf[1];
   }

   g_free(secondaries);
   g_list_free(active);
}

static bool
check_fences(VkdfScene *s)
{
   bool new_signaled = false;
   if (s->sync.present_fence_active &&
       vkGetFenceStatus(s->ctx->device, s->sync.present_fence) == VK_SUCCESS) {
      vkResetFences(s->ctx->device, 1, &s->sync.present_fence);
      s->sync.present_fence_active = false;
      new_signaled = true;
   }

   return new_signaled;
}

static void
free_inactive_command_buffers(VkdfScene *s)
{
   for (uint32_t i = 0; i < s->thread.num_threads; i++) {
      GList *iter = s->cmd_buf.free[i];
      while (iter) {
         struct FreeCmdBufInfo *info = (struct FreeCmdBufInfo *) iter->data;
         assert(info->num_commands > 0);
         vkFreeCommandBuffers(s->ctx->device, s->cmd_buf.pool[i],
                              info->num_commands, info->cmd_buf);

         // If this was a tile secondary, mark the tile as not having a command
         if (info->tile &&
             info->tile->cmd_buf == info->cmd_buf[0]) {
            info->tile->cmd_buf = 0;
            info->tile->depth_cmd_buf = 0;
         }

         GList *link = iter;
         iter = g_list_next(iter);
         s->cmd_buf.free[i] = g_list_delete_link(s->cmd_buf.free[i], link);

         g_free(info);
      }
   }
}

static inline void
add_to_cache(struct TileThreadData *data, VkdfSceneTile *t)
{
   VkdfScene *s = data->s;
   uint32_t job_id = data->id;
   assert(job_id < s->thread.num_threads);

   s->cache[job_id].cached = g_list_prepend(s->cache[job_id].cached, t);
   s->cache[job_id].size++;
}

static inline void
remove_from_cache(struct TileThreadData *data, VkdfSceneTile *t)
{
   VkdfScene *s = data->s;
   uint32_t job_id = data->id;
   assert(job_id < s->thread.num_threads);

   assert(s->cache[job_id].size > 0);
   s->cache[job_id].cached = g_list_remove(s->cache[job_id].cached, t);
   s->cache[job_id].size--;
}

static void
record_viewport_and_scissor_commands(VkCommandBuffer cmd_buf,
                                     uint32_t width,
                                     uint32_t height)
{
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
}

static void
new_active_tile(struct TileThreadData *data, VkdfSceneTile *t)
{
   VkdfScene *s = data->s;
   uint32_t job_id = data->id;
   assert(job_id < s->thread.num_threads);

   assert(t->obj_count > 0);

   /* If we don't free secondaries we only need to record them once and we can
    * reuse them whenever we need them again.
    */
   if (!SCENE_FREE_SECONDARIES) {
      if (t->cmd_buf != 0) {
         s->cmd_buf.active[job_id] = g_list_prepend(s->cmd_buf.active[job_id], t);
         return;
      }
   } else {
      /* Otherwise, we may still find it in the cache */
      if (s->cache[job_id].size > 0) {
         GList *found = g_list_find(s->cache[job_id].cached, t);
         if (found) {
            remove_from_cache(data, t);
            s->cmd_buf.active[job_id] =
               g_list_prepend(s->cmd_buf.active[job_id], t);
            return;
         }
      }
   }

   /* If we get here, it means we need to create and record a new one */
   VkCommandBuffer cmd_buf[2];
   vkdf_create_command_buffer(s->ctx,
                              s->cmd_buf.pool[job_id],
                              VK_COMMAND_BUFFER_LEVEL_SECONDARY,
                              s->rp.do_depth_prepass ? 2 : 1, cmd_buf);

   VkCommandBufferUsageFlags flags =
      VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT |
      VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;

   VkCommandBufferInheritanceInfo inheritance_info;
   inheritance_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
   inheritance_info.pNext = NULL;
   inheritance_info.renderPass = s->rp.static_geom.renderpass;
   inheritance_info.subpass = 0;
   inheritance_info.framebuffer = s->rp.static_geom.framebuffer;
   inheritance_info.occlusionQueryEnable = 0;
   inheritance_info.queryFlags = 0;
   inheritance_info.pipelineStatistics = 0;

   vkdf_command_buffer_begin_secondary(cmd_buf[0], flags, &inheritance_info);

   record_viewport_and_scissor_commands(cmd_buf[0], s->rt.width, s->rt.height);

   s->callbacks.record_commands(s->ctx, cmd_buf[0], t->sets, false, false,
                                s->callbacks.data);

   vkdf_command_buffer_end(cmd_buf[0]);

   t->cmd_buf = cmd_buf[0];

   if (s->rp.do_depth_prepass) {
      inheritance_info.renderPass = s->rp.dpp_static_geom.renderpass;
      inheritance_info.framebuffer = s->rp.dpp_static_geom.framebuffer;

      vkdf_command_buffer_begin_secondary(cmd_buf[1],
                                          flags, &inheritance_info);

      record_viewport_and_scissor_commands(cmd_buf[1],
                                           s->rt.width, s->rt.height);

      s->callbacks.record_commands(s->ctx, cmd_buf[1],
                                   t->sets, false, true, s->callbacks.data);

      vkdf_command_buffer_end(cmd_buf[1]);

      t->depth_cmd_buf = cmd_buf[1];
   }

   s->cmd_buf.active[job_id] = g_list_prepend(s->cmd_buf.active[job_id], t);

   t->dirty = false;
}

static void
new_inactive_tile(struct TileThreadData *data, VkdfSceneTile *t)
{
   VkdfScene *s = data->s;
   uint32_t job_id = data->id;
   assert(job_id < s->thread.num_threads);

   s->cmd_buf.active[job_id] =
      g_list_remove(s->cmd_buf.active[job_id], t);

   /* If we're not freeing secondary command buffers, then we are done */
   if (!SCENE_FREE_SECONDARIES)
      return;

   /* Otherwise, put it in the cache if we have one */
   VkdfSceneTile *expired;
   if (s->cache[job_id].max_size <= 0) {
      expired = t;
   } else {
      if (s->cache[job_id].size >= s->cache[job_id].max_size) {
         GList *last = g_list_last(s->cache[job_id].cached);
         expired = (VkdfSceneTile *) last->data;
         remove_from_cache(data, expired);
      } else {
         expired = NULL;
      }

      add_to_cache(data, t);
   }

   if (!expired)
      return;

   /* If we got here, we have a command buffer to free, but we can't do it yet,
    * since it may still be used by the GPU. Put it in a to-free list and
    * free it when it is safe.
    */
   struct FreeCmdBufInfo *info = g_new(struct FreeCmdBufInfo, 1);
   info->cmd_buf[0] = expired->cmd_buf;
   if (s->rp.do_depth_prepass) {
      info->num_commands = 2;
      info->cmd_buf[1] = expired->depth_cmd_buf;
   } else {
      info->num_commands = 1;
   }
   info->tile = expired;
   s->cmd_buf.free[job_id] = g_list_prepend(s->cmd_buf.free[job_id], info);
}

static void
start_recording_resource_updates(VkdfScene *s)
{
   // If the previous frame didn't have any resource updates, we have the
   // resouce update command buffer available for this frame, otherwise
   // we need to create a new one.
   VkCommandBuffer cmd_buf;
   if (s->cmd_buf.update_resources && !s->cmd_buf.have_resource_updates) {
      cmd_buf = s->cmd_buf.update_resources;
   } else {
      if (s->cmd_buf.update_resources)
         new_inactive_cmd_buf(s, 0, s->cmd_buf.update_resources);

      vkdf_create_command_buffer(s->ctx,
                                 s->cmd_buf.pool[0],
                                 VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                 1, &cmd_buf);

      vkdf_command_buffer_begin(cmd_buf,
                                VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
   }

   s->cmd_buf.update_resources = cmd_buf;
}

static inline void
stop_recording_resource_updates(VkdfScene *s)
{
   if (s->cmd_buf.have_resource_updates)
      vkdf_command_buffer_end(s->cmd_buf.update_resources);
}

static inline void
record_client_resource_updates(VkdfScene *s)
{
   s->cmd_buf.have_resource_updates =
      s->callbacks.update_resources(s->ctx,
                                    s->cmd_buf.update_resources,
                                    s->callbacks.data);
}

static void
build_object_lists(VkdfScene *s, VkdfSceneTile *t, const char *set_id)
{
   // Leaf tiles is where we put objects when we add objects to the scene,
   // so their lists are already in place
   if (!t->subtiles)
      return;

   // If this tile doesn't have any objects we are all set
   if (t->obj_count == 0)
      return;

   // Call this recursively for each subtile, for each object key
   // available to build per-key lists for each (sub)tile
   VkdfSceneSetInfo *tile_set_info =
     (VkdfSceneSetInfo *) g_hash_table_lookup(t->sets, set_id);

   for (int32_t i = 0; i < 8; i++) {
      VkdfSceneTile *st = &t->subtiles[i];
      if (st->obj_count > 0) {
         build_object_lists(s, st, set_id);
         VkdfSceneSetInfo *subtile_set_info =
            (VkdfSceneSetInfo *) g_hash_table_lookup(st->sets, set_id);
         GList *st_objs = subtile_set_info->objs;
         while (st_objs) {
            VkdfObject *obj = (VkdfObject *) st_objs->data;
            tile_set_info->objs = g_list_prepend(tile_set_info->objs, obj);
            st_objs = g_list_next(st_objs);
            tile_set_info->count++;
            if (obj->casts_shadows)
               tile_set_info->shadow_caster_count++;
         }
      }
   }

   tile_set_info->objs = g_list_reverse(tile_set_info->objs);
}

static void
compute_tile_start_indices(VkdfScene *s,
                           VkdfSceneTile *t,
                           const char *set_id,
                           uint32_t start_index,
                           uint32_t shadow_caster_start_index,
                           uint32_t *next_start_index,
                           uint32_t *next_shadow_caster_start_index)
{
   VkdfSceneSetInfo *tile_set_info =
      (VkdfSceneSetInfo *) g_hash_table_lookup(t->sets, set_id);
   tile_set_info->start_index = start_index;
   tile_set_info->shadow_caster_start_index = shadow_caster_start_index;

   if (!t->subtiles) {
      *next_start_index = tile_set_info->start_index + tile_set_info->count;
      *next_shadow_caster_start_index =
         tile_set_info->shadow_caster_start_index +
         tile_set_info->shadow_caster_count;
      return;
   }

   for (uint32_t i = 0; i < 8; i++) {
      VkdfSceneTile *st = &t->subtiles[i];
      VkdfSceneSetInfo *subtile_set_info =
         (VkdfSceneSetInfo *) g_hash_table_lookup(st->sets, set_id);

      subtile_set_info->start_index = start_index;
      subtile_set_info->shadow_caster_start_index = shadow_caster_start_index;

      uint32_t unused;
      compute_tile_start_indices(s, st, set_id,
                                 subtile_set_info->start_index,
                                 subtile_set_info->shadow_caster_start_index,
                                 &unused, &unused);

      start_index += subtile_set_info->count;
      shadow_caster_start_index += subtile_set_info->shadow_caster_count;
    }

   *next_start_index = start_index;
   *next_shadow_caster_start_index = shadow_caster_start_index;
}

static void
ensure_set_infos(VkdfSceneTile *t, GList *set_ids)
{
   GList *iter = set_ids;
   while (iter) {
      const char *id = (const char *) iter->data;
      VkdfSceneSetInfo *info =
         (VkdfSceneSetInfo *) g_hash_table_lookup(t->sets, id);
      if (!info) {
         info = g_new0(VkdfSceneSetInfo, 1);
         g_hash_table_replace(t->sets, g_strdup(id), info);
      }

      if (t->subtiles) {
         for (uint32_t i = 0; i < 8; i++)
            ensure_set_infos(&t->subtiles[i], set_ids);
      }

      iter = g_list_next(iter);
   }
}

static inline uint32_t
tile_is_visible(VkdfSceneTile *t,
                const VkdfBox *visible_box,
                const VkdfPlane *fp)
{
   if (t->obj_count == 0)
      return OUTSIDE;
   return vkdf_box_is_in_frustum(&t->box, visible_box, fp);
}

static inline uint32_t
subtile_is_visible(VkdfSceneTile *t, const VkdfPlane *fp)
{
   if (t->obj_count == 0)
      return OUTSIDE;

   // We only check subtiles if the parent tile is inside the camera's box,
   // so no need to check if a subtile is inside it
   return vkdf_box_is_in_frustum(&t->box, NULL, fp);
}

static GList *
find_visible_subtiles(VkdfSceneTile *t,
                      const VkdfPlane *fplanes,
                      GList *visible)
{
   // If the tile can't be subdivided, then take the entire tile as visible
   if (!t->subtiles)
      return g_list_prepend(visible, t);

   // Otherwise, check visibility for each subtile
   uint32_t subtile_visibility[8];
   bool all_subtiles_visible = true;

   for (uint32_t j = 0; j < 8; j++) {
      VkdfSceneTile *st = &t->subtiles[j];
      subtile_visibility[j] = subtile_is_visible(st, fplanes);

      // Only take individualsubtiles if there are invisible subtiles that have
      // objects in them
      if (subtile_visibility[j] == OUTSIDE && st->obj_count > 0)
         all_subtiles_visible = false;
   }

   // If all subtiles are visible, then the parent tile is fully visible,
   // just add the parent tile
   if (all_subtiles_visible)
      return g_list_prepend(visible, t);

   // Otherwise, add only the visible subtiles
   for (uint32_t j = 0; j < 8; j++) {
      if (subtile_visibility[j] == INSIDE) {
         visible = g_list_prepend(visible, &t->subtiles[j]);
      } else if (subtile_visibility[j] == INTERSECT) {
         visible = find_visible_subtiles(&t->subtiles[j], fplanes, visible);
      }
   }

   return visible;
}

static GList *
find_visible_tiles(VkdfScene *s,
                   uint32_t first_tile_idx,
                   uint32_t last_tile_idx,
                   const VkdfBox *visible_box,
                   const VkdfPlane *fplanes)
{
   GList *visible = NULL;
   for (uint32_t i = first_tile_idx; i <= last_tile_idx; i++) {
      VkdfSceneTile *t = &s->tiles[i];
      uint32_t visibility = tile_is_visible(t, visible_box, fplanes);
      if (visibility == INSIDE) {
         visible = g_list_prepend(visible, t);
      } else if (visibility == INTERSECT) {
         visible = find_visible_subtiles(t, fplanes, visible);
      }
   }
   return visible;
}

static void
create_static_object_ubo(VkdfScene *s)
{
   // Per-instance data: model matrix, base material index, model index,
   // receives shadows
   uint32_t num_objects = vkdf_scene_get_static_object_count(s);
   if (num_objects == 0)
      return;

   s->ubo.obj.inst_size = ALIGN(sizeof(glm::mat4) + 3 * sizeof(uint32_t), 16);
   s->ubo.obj.size = s->ubo.obj.inst_size * num_objects;
   s->ubo.obj.buf =
      vkdf_create_buffer(s->ctx, 0,
                         s->ubo.obj.size,
                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

   uint8_t *mem;
   vkdf_memory_map(s->ctx, s->ubo.obj.buf.mem,
                   0, VK_WHOLE_SIZE, (void **) &mem);

   // NOTE: this assumes that each set-id model has a different set of
   // materials. In theory, we could have different set-ids share models
   // though and in that case we would be replicating model data here,
   // but this makes things easier.
   uint32_t model_index = 0;
   GList *set_id_iter = s->set_ids;
   while (set_id_iter) {
      for (uint32_t i = 0; i < s->num_tiles.total; i++) {
         VkdfSceneTile *t = &s->tiles[i];
         if (t->obj_count == 0)
             continue;

         const char *set_id = (const char *) set_id_iter->data;
         VkdfSceneSetInfo *info =
            (VkdfSceneSetInfo *) g_hash_table_lookup(t->sets, set_id);
         if (info && info->count > 0) {
            VkDeviceSize offset = info->start_index * s->ubo.obj.inst_size;
            GList *iter = info->objs;
            while (iter) {
               VkdfObject *obj = (VkdfObject *) iter->data;

               // Model matrix
               glm::mat4 model = vkdf_object_get_model_matrix(obj);
               float *model_data = glm::value_ptr(model);
               memcpy(mem + offset, model_data, sizeof(glm::mat4));
               offset += sizeof(glm::mat4);

               // Base material index
               memcpy(mem + offset, &obj->material_idx_base, sizeof(uint32_t));
               offset += sizeof(uint32_t);

               // Model index
               memcpy(mem + offset, &model_index, sizeof(uint32_t));
               offset += sizeof(uint32_t);

               // Receives shadows
               uint32_t receives_shadows = (uint32_t) obj->receives_shadows;
               memcpy(mem + offset, &receives_shadows, sizeof(uint32_t));
               offset += sizeof(uint32_t);

               offset = ALIGN(offset, 16);

               iter = g_list_next(iter);
            }
         }
      }
      set_id_iter = g_list_next(set_id_iter);
      model_index++;
   }

   vkdf_memory_unmap(s->ctx, s->ubo.obj.buf.mem, s->ubo.obj.buf.mem_props,
                     0, VK_WHOLE_SIZE);
}

static void
create_dynamic_object_ubo(VkdfScene *s)
{
   // Per-instance data: model matrix, base material index,
   // model index, receives shadows
   s->dynamic.ubo.obj.inst_size =
      ALIGN(sizeof(glm::mat4) + 3 * sizeof(uint32_t), 16);

   s->dynamic.ubo.obj.host_buf =
      g_new(uint8_t, MAX_DYNAMIC_OBJECTS * s->dynamic.ubo.obj.inst_size);

   s->dynamic.ubo.obj.size = s->dynamic.ubo.obj.inst_size * MAX_DYNAMIC_OBJECTS;

   s->dynamic.ubo.obj.buf =
      vkdf_create_buffer(s->ctx, 0,
                         s->dynamic.ubo.obj.size,
                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                         VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
}

struct _shadow_map_ubo_data {
   glm::mat4 light_viewproj;
   uint32_t shadow_map_size;
   uint32_t pcf_kernel_size;
   uint32_t padding[2]; // Keep this struct 16-byte aligned
};

static void
create_light_ubo(VkdfScene *s)
{
   uint32_t num_lights = s->lights.size();
   if (num_lights == 0)
      return;

   const uint32_t light_data_size = ALIGN(sizeof(VkdfLight), 16);

   const uint32_t shadow_map_data_size =
      ALIGN(sizeof(struct _shadow_map_ubo_data) , 16);

   /* Since our shadow map data comes after the light data, make sure it
    * starts at a valid offset.
    */
   VkDeviceSize ubo_offset_alignment =
      s->ctx->phy_device_props.limits.minUniformBufferOffsetAlignment;

   s->ubo.light.light_data_size = num_lights * light_data_size;
   s->ubo.light.shadow_map_data_offset =
      ALIGN(s->ubo.light.light_data_size, ubo_offset_alignment);
   s->ubo.light.shadow_map_data_size = num_lights * shadow_map_data_size;
   s->ubo.light.size = s->ubo.light.shadow_map_data_offset +
                       s->ubo.light.shadow_map_data_size;
   s->ubo.light.buf = vkdf_create_buffer(s->ctx, 0,
                                         s->ubo.light.size,
                                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                                         VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
}

/**
 * Creates a UBO with the model matrices for each object that can cast
 * shadows (the ones we need to render to the shadow map).
 *
 * The function also computes the counts of shadow caster objects per tile and
 * in each set as well as the starting index of each set in the UBO so we can
 * draw correct instance counts when we render each set to the the shadow map.
 */
static void
create_static_shadow_map_ubo(VkdfScene *s)
{
   if (s->static_shadow_caster_count == 0)
      return;

   // Build per-instance data for each shadow caster object
   s->ubo.shadow_map.inst_size = ALIGN(sizeof(glm::mat4), 16);
   s->ubo.shadow_map.size =
      s->ubo.shadow_map.inst_size * s->static_shadow_caster_count;
   s->ubo.shadow_map.buf =
      vkdf_create_buffer(s->ctx, 0,
                         s->ubo.shadow_map.size,
                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

   uint8_t *mem;
   vkdf_memory_map(s->ctx, s->ubo.shadow_map.buf.mem,
                   0, VK_WHOLE_SIZE, (void **) &mem);

   VkDeviceSize offset = 0;
   GList *set_id_iter = s->set_ids;
   while (set_id_iter) {
      for (uint32_t i = 0; i < s->num_tiles.total; i++) {
         VkdfSceneTile *t = &s->tiles[i];
         if (t->shadow_caster_count == 0)
             continue;

         const char *set_id = (const char *) set_id_iter->data;
         VkdfSceneSetInfo *info =
            (VkdfSceneSetInfo *) g_hash_table_lookup(t->sets, set_id);
         if (info && info->shadow_caster_count > 0) {
            GList *iter = info->objs;
            while (iter) {
               VkdfObject *obj = (VkdfObject *) iter->data;
               if (vkdf_object_casts_shadows(obj)) {
                  // Model matrix
                  glm::mat4 model = vkdf_object_get_model_matrix(obj);
                  float *model_data = glm::value_ptr(model);
                  memcpy(mem + offset, model_data, sizeof(glm::mat4));
                  offset += sizeof(glm::mat4);

                  offset = ALIGN(offset, 16);
               }
               iter = g_list_next(iter);
            }
         }
      }
      set_id_iter = g_list_next(set_id_iter);
   }

   vkdf_memory_unmap(s->ctx, s->ubo.shadow_map.buf.mem,
                     s->ubo.shadow_map.buf.mem_props, 0, VK_WHOLE_SIZE);
}

static void
create_dynamic_shadow_map_ubo(VkdfScene *s)
{
   s->dynamic.ubo.shadow_map.inst_size = ALIGN(sizeof(glm::mat4), 16);

   VkDeviceSize buf_size =
      s->dynamic.ubo.shadow_map.inst_size * MAX_DYNAMIC_OBJECTS *
         s->lights.size();

   s->dynamic.ubo.shadow_map.host_buf = g_new(uint8_t, buf_size);
   s->dynamic.ubo.shadow_map.size = buf_size;

   s->dynamic.ubo.shadow_map.buf =
      vkdf_create_buffer(s->ctx, 0,
                         s->dynamic.ubo.shadow_map.size,
                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                         VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
}

static void
create_static_material_ubo(VkdfScene *s)
{
   // NOTE: this doesn't consider the case where we have repeated models,
   // which could happen if different set-ids share the same model. It is
   // fine though, since we don't handle the case of shared models when
   // we set up the static object ubo either.
   uint32_t num_models = g_list_length(s->models);
   s->ubo.material.size = num_models * MAX_MATERIALS_PER_MODEL *
                          ALIGN(sizeof(VkdfMaterial), 16);
   s->ubo.material.buf =
      vkdf_create_buffer(s->ctx, 0,
                         s->ubo.material.size,
                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

   uint8_t *mem;
   VkDeviceSize material_size = sizeof(VkdfMaterial);
   vkdf_memory_map(s->ctx, s->ubo.material.buf.mem,
                   0, VK_WHOLE_SIZE, (void **) &mem);

   uint32_t model_idx = 0;
   GList *model_iter = s->models;
   while (model_iter) {
      VkdfModel *model = (VkdfModel *) model_iter->data;
      VkDeviceSize offset =
         model_idx * MAX_MATERIALS_PER_MODEL * ALIGN(sizeof(VkdfMaterial), 16);
      uint32_t num_materials = model->materials.size();
      assert(num_materials <= MAX_MATERIALS_PER_MODEL);
      for (uint32_t mat_idx = 0; mat_idx < num_materials; mat_idx++) {
         VkdfMaterial *m = &model->materials[mat_idx];
         memcpy(mem + offset, m, material_size);
         offset += ALIGN(material_size, 16);
      }
      model_iter = g_list_next(model_iter);
      model_idx++;
   }

   vkdf_memory_unmap(s->ctx, s->ubo.material.buf.mem,
                     s->ubo.material.buf.mem_props, 0, VK_WHOLE_SIZE);
}

static void
create_dynamic_material_ubo(VkdfScene *s)
{
   s->dynamic.ubo.material.inst_size = ALIGN(sizeof(VkdfMaterial), 16);

   VkDeviceSize buf_size =
      MAX_DYNAMIC_MATERIALS * s->dynamic.ubo.material.inst_size;

   s->dynamic.ubo.material.host_buf = g_new(uint8_t, buf_size);
   s->dynamic.ubo.material.size = buf_size;

   s->dynamic.ubo.material.buf =
      vkdf_create_buffer(s->ctx, 0,
                         s->dynamic.ubo.material.size,
                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                         VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
}

/**
 * - Builds object lists for non-leaf (sub)tiles (making sure object
 *   order is correct)
 * - Computes (sub)tile starting indices
 * - Creates static UBO data for scene objects (model matrix, materials, etc)
 */
static void
prepare_scene_objects(VkdfScene *s)
{
   if (!s->dirty)
      return;

   s->set_ids = g_list_reverse(s->set_ids);
   s->models = g_list_reverse(s->models);

   for (uint32_t i = 0; i < s->num_tiles.total; i++) {
      VkdfSceneTile *t = &s->tiles[i];
      ensure_set_infos(t, s->set_ids);

      GList *iter = s->set_ids;
      while (iter) {
         const char *set_id = (const char *) iter->data;
         build_object_lists(s, t, set_id);
         iter = g_list_next(iter);
      }
   }

   uint32_t start_index = 0;
   uint32_t shadow_caster_start_index = 0;
   GList *iter = s->set_ids;
   while (iter) {
      const char *set_id = (const char *) iter->data;
      for (uint32_t i = 0; i < s->num_tiles.total; i++) {
         VkdfSceneTile *t = &s->tiles[i];
         uint32_t next_start_index, next_shadow_caster_start_index;
         compute_tile_start_indices(s, t, set_id,
                                    start_index,
                                    shadow_caster_start_index,
                                    &next_start_index,
                                    &next_shadow_caster_start_index);
         start_index = next_start_index;
         shadow_caster_start_index = next_shadow_caster_start_index;
      }
      iter = g_list_next(iter);
   }

   create_static_object_ubo(s);
   create_static_material_ubo(s);

   create_dynamic_object_ubo(s);
   create_dynamic_material_ubo(s);

   create_light_ubo(s);
   if (s->has_shadow_caster_lights) {
      create_static_shadow_map_ubo(s);
      create_dynamic_shadow_map_ubo(s);
   }

   s->dirty = false;
}

static VkRenderPass
create_depth_renderpass(VkdfScene *s,
                        VkAttachmentLoadOp load_op,
                        bool needs_sampling)
{
   VkAttachmentDescription attachments[2];

   // Single depth attachment
   attachments[0].format = VK_FORMAT_D32_SFLOAT;
   attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
   attachments[0].loadOp = load_op;
   attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
   attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
   attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
   attachments[0].initialLayout =
      load_op == VK_ATTACHMENT_LOAD_OP_CLEAR ?
         VK_IMAGE_LAYOUT_UNDEFINED :
         VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
   attachments[0].finalLayout = needs_sampling ?
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL :
      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
   attachments[0].flags = 0;

   // Attachment references from subpasses
   VkAttachmentReference depth_ref;
   depth_ref.attachment = 0;
   depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

   // Single subpass
   VkSubpassDescription subpass[1];
   subpass[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
   subpass[0].flags = 0;
   subpass[0].inputAttachmentCount = 0;
   subpass[0].pInputAttachments = NULL;
   subpass[0].colorAttachmentCount = 0;
   subpass[0].pColorAttachments = NULL;
   subpass[0].pResolveAttachments = NULL;
   subpass[0].pDepthStencilAttachment = &depth_ref;
   subpass[0].preserveAttachmentCount = 0;
   subpass[0].pPreserveAttachments = NULL;

   // Create render pass
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
   VK_CHECK(vkCreateRenderPass(s->ctx->device, &rp_info, NULL, &renderpass));

   return renderpass;
}

static inline void
create_shadow_map_renderpass(VkdfScene *s)
{
   s->shadows.renderpass =
      create_depth_renderpass(s, VK_ATTACHMENT_LOAD_OP_CLEAR, true);
}

struct _shadow_map_pcb {
   glm::mat4 viewproj;
};

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

static inline uint32_t
hash_shadow_map_pipeline_spec(uint32_t vertex_data_stride,
                              VkPrimitiveTopology primitive)
{
   assert((vertex_data_stride & 0x00ffffff) == vertex_data_stride);
   return primitive << 24 | vertex_data_stride;
}

static void
create_shadow_map_pipeline_for_mesh(VkdfScene *s, VkdfMesh *mesh)
{
   uint32_t vertex_data_stride = vkdf_mesh_get_vertex_data_stride(mesh);
   VkPrimitiveTopology primitive = vkdf_mesh_get_primitive(mesh);
   void *hash = GINT_TO_POINTER(
      hash_shadow_map_pipeline_spec(vertex_data_stride, primitive));
   if (g_hash_table_lookup(s->shadows.pipeline.pipelines, hash) != NULL)
      return;

   VkPipelineInputAssemblyStateCreateInfo ia;
   ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
   ia.pNext = NULL;
   ia.flags = 0;
   ia.primitiveRestartEnable = VK_FALSE;
   ia.topology = primitive;

   VkPipelineViewportStateCreateInfo vp;
   vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
   vp.pNext = NULL;
   vp.flags = 0;
   vp.viewportCount = 1;
   vp.scissorCount = 1;
   vp.pScissors = NULL;
   vp.pViewports = NULL;

   VkPipelineMultisampleStateCreateInfo ms;
   ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
   ms.pNext = NULL;
   ms.flags = 0;
   ms.pSampleMask = NULL;
   ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
   ms.sampleShadingEnable = VK_FALSE;
   ms.alphaToCoverageEnable = VK_FALSE;
   ms.alphaToOneEnable = VK_FALSE;
   ms.minSampleShading = 0.0;

   VkPipelineDepthStencilStateCreateInfo ds;
   ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
   ds.pNext = NULL;
   ds.flags = 0;
   ds.depthTestEnable = VK_TRUE;
   ds.depthWriteEnable = VK_TRUE;
   ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
   ds.depthBoundsTestEnable = VK_FALSE;
   ds.minDepthBounds = 0;
   ds.maxDepthBounds = 0;
   ds.stencilTestEnable = VK_FALSE;
   ds.back.failOp = VK_STENCIL_OP_KEEP;
   ds.back.passOp = VK_STENCIL_OP_KEEP;
   ds.back.compareOp = VK_COMPARE_OP_ALWAYS;
   ds.back.compareMask = 0;
   ds.back.reference = 0;
   ds.back.depthFailOp = VK_STENCIL_OP_KEEP;
   ds.back.writeMask = 0;
   ds.front = ds.back;

   VkPipelineColorBlendStateCreateInfo cb;
   VkPipelineColorBlendAttachmentState att_state[1];
   att_state[0].colorWriteMask = 0xf;
   att_state[0].blendEnable = VK_FALSE;
   att_state[0].alphaBlendOp = VK_BLEND_OP_ADD;
   att_state[0].colorBlendOp = VK_BLEND_OP_ADD;
   att_state[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
   att_state[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
   att_state[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
   att_state[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
   cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
   cb.flags = 0;
   cb.pNext = NULL;
   cb.attachmentCount = 0;
   cb.pAttachments = att_state;
   cb.logicOpEnable = VK_FALSE;
   cb.logicOp = VK_LOGIC_OP_COPY;
   cb.blendConstants[0] = 1.0f;
   cb.blendConstants[1] = 1.0f;
   cb.blendConstants[2] = 1.0f;
   cb.blendConstants[3] = 1.0f;

   VkPipelineDynamicStateCreateInfo dsi;
   VkDynamicState ds_enables[VK_DYNAMIC_STATE_RANGE_SIZE];
   uint32_t ds_count = 0;
   memset(ds_enables, 0, sizeof(ds_enables));
   ds_enables[ds_count++] = VK_DYNAMIC_STATE_SCISSOR;
   ds_enables[ds_count++] = VK_DYNAMIC_STATE_VIEWPORT;
   ds_enables[ds_count++] = VK_DYNAMIC_STATE_DEPTH_BIAS;
   dsi.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
   dsi.pNext = NULL;
   dsi.flags = 0;
   dsi.pDynamicStates = ds_enables;
   dsi.dynamicStateCount = ds_count;

   // Depth bias state is dynamic so we can use different settings per light
   VkPipelineRasterizationStateCreateInfo rs;
   rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
   rs.pNext = NULL;
   rs.flags = 0;
   rs.polygonMode = VK_POLYGON_MODE_FILL;
   rs.cullMode = VK_CULL_MODE_BACK_BIT;
   rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
   rs.depthClampEnable = VK_FALSE;
   rs.rasterizerDiscardEnable = VK_FALSE;
   rs.lineWidth = 1.0f;
   rs.depthBiasEnable = VK_TRUE;

   VkVertexInputBindingDescription vi_binding[1];
   VkVertexInputAttributeDescription vi_attribs[1];
   vkdf_vertex_binding_set(&vi_binding[0],
                           0, VK_VERTEX_INPUT_RATE_VERTEX, vertex_data_stride);
   vkdf_vertex_attrib_set(&vi_attribs[0], 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0);

   VkPipelineVertexInputStateCreateInfo vi;
   vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
   vi.pNext = NULL;
   vi.flags = 0;
   vi.vertexBindingDescriptionCount = 1;
   vi.pVertexBindingDescriptions = vi_binding;
   vi.vertexAttributeDescriptionCount = 1;
   vi.pVertexAttributeDescriptions = vi_attribs;

   if (!s->shadows.shaders.vs) {
      s->shadows.shaders.vs =
         vkdf_create_shader_module(s->ctx, SHADOW_MAP_SHADER_PATH);
   }
   VkPipelineShaderStageCreateInfo shader_stages[1];
   vkdf_pipeline_fill_shader_stage_info(&shader_stages[0],
                                        VK_SHADER_STAGE_VERTEX_BIT,
                                        s->shadows.shaders.vs);

   VkGraphicsPipelineCreateInfo pipeline_info;
   pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
   pipeline_info.pNext = NULL;
   pipeline_info.layout = s->shadows.pipeline.layout;
   pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
   pipeline_info.basePipelineIndex = 0;
   pipeline_info.flags = 0;
   pipeline_info.pVertexInputState = &vi;
   pipeline_info.pInputAssemblyState = &ia;
   pipeline_info.pTessellationState = NULL;
   pipeline_info.pViewportState = &vp;
   pipeline_info.pRasterizationState = &rs;
   pipeline_info.pMultisampleState = &ms;
   pipeline_info.pDepthStencilState = &ds;
   pipeline_info.pColorBlendState = &cb;
   pipeline_info.pDynamicState = &dsi;
   pipeline_info.pStages = shader_stages;
   pipeline_info.stageCount = 1;
   pipeline_info.renderPass = s->shadows.renderpass;
   pipeline_info.subpass = 0;

   VkPipeline pipeline;
   VK_CHECK(vkCreateGraphicsPipelines(s->ctx->device,
                                      NULL,
                                      1,
                                      &pipeline_info,
                                      NULL,
                                      &pipeline));

   g_hash_table_insert(s->shadows.pipeline.pipelines, hash, pipeline);
}

/**
 * Creates a pipeline to render each mesh in the scene to the
 * shadow map.
 */
static void
create_shadow_map_pipelines(VkdfScene *s)
{
   // Set layout with a single binding for the model matrices of
   // scene objects
   s->shadows.pipeline.models_set_layout =
      vkdf_create_ubo_descriptor_set_layout(s->ctx, 0, 1,
                                            VK_SHADER_STAGE_VERTEX_BIT, false);

   if (s->static_shadow_caster_count > 0) {
      s->shadows.pipeline.models_set =
         create_descriptor_set(s->ctx, s->ubo.static_pool,
                               s->shadows.pipeline.models_set_layout);

      VkDeviceSize ubo_offset = 0;
      VkDeviceSize ubo_size = s->ubo.shadow_map.size;
      vkdf_descriptor_set_buffer_update(s->ctx,
                                        s->shadows.pipeline.models_set,
                                        s->ubo.shadow_map.buf.buf,
                                        0, 1, &ubo_offset, &ubo_size,
                                        false, true);
   }

   s->shadows.pipeline.dyn_models_set =
      create_descriptor_set(s->ctx, s->ubo.static_pool,
                            s->shadows.pipeline.models_set_layout);


   VkDeviceSize ubo_offset = 0;
   VkDeviceSize ubo_size = s->dynamic.ubo.shadow_map.size;
   vkdf_descriptor_set_buffer_update(s->ctx,
                                     s->shadows.pipeline.dyn_models_set,
                                     s->dynamic.ubo.shadow_map.buf.buf,
                                     0, 1, &ubo_offset, &ubo_size,
                                     false, true);

   // Pipeline layout: 2 push constant ranges and 1 set layout
   VkPushConstantRange pcb_ranges[1];
   pcb_ranges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
   pcb_ranges[0].offset = 0;
   pcb_ranges[0].size = sizeof(struct _shadow_map_pcb);

   VkDescriptorSetLayout set_layouts[1] = {
      s->shadows.pipeline.models_set_layout,
   };

   VkPipelineLayoutCreateInfo pipeline_layout_info;
   pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
   pipeline_layout_info.pNext = NULL;
   pipeline_layout_info.pushConstantRangeCount =
      sizeof(pcb_ranges) / sizeof(VkPushConstantRange);
   pipeline_layout_info.pPushConstantRanges = pcb_ranges;
   pipeline_layout_info.setLayoutCount =
      sizeof(set_layouts) / sizeof(VkDescriptorSetLayout);
   pipeline_layout_info.pSetLayouts = set_layouts;
   pipeline_layout_info.flags = 0;

   VK_CHECK(vkCreatePipelineLayout(s->ctx->device,
                                   &pipeline_layout_info,
                                   NULL,
                                   &s->shadows.pipeline.layout));

   // Create a pipeline instance for each mesh spec in the scene
   //
   // Different meshes may require slightly different pipelines to be rendered
   // to the shadow map to account for varying vertex data strides in the
   // meshes's vertex buffers and different primitive topologies.
   s->shadows.pipeline.pipelines =
      g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, NULL);

   GList *iter = s->models;
   while(iter) {
      VkdfModel *model = (VkdfModel *) iter->data;
      for (uint32_t mesh_idx = 0; mesh_idx < model->meshes.size(); mesh_idx++) {
         VkdfMesh *mesh = model->meshes[mesh_idx];
         create_shadow_map_pipeline_for_mesh(s, mesh);
      }
      iter = g_list_next(iter);
   }
}

static VkFramebuffer
create_depth_framebuffer(VkdfScene *s,
                         uint32_t width,
                         uint32_t height,
                         VkRenderPass renderpass,
                         VkImageView view)
{
   VkFramebufferCreateInfo fb_info;
   fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
   fb_info.pNext = NULL;
   fb_info.renderPass = renderpass;
   fb_info.attachmentCount = 1;
   fb_info.pAttachments = &view;
   fb_info.width = width;
   fb_info.height = height;
   fb_info.layers = 1;
   fb_info.flags = 0;

   VkFramebuffer framebuffer;
   VK_CHECK(vkCreateFramebuffer(s->ctx->device, &fb_info, NULL, &framebuffer));

   return framebuffer;
}

static inline void
create_shadow_map_framebuffer(VkdfScene *s, VkdfSceneLight *sl)
{
   sl->shadow.framebuffer =
      create_depth_framebuffer(s,
                               sl->shadow.spec.shadow_map_size,
                               sl->shadow.spec.shadow_map_size,
                               s->shadows.renderpass,
                               sl->shadow.shadow_map.view);
}

static const VkdfFrustum *
scene_light_get_frustum(VkdfScene *s, VkdfSceneLight *sl)
{
   // FIXME: support point lights
   assert(vkdf_light_get_type(sl->light) != VKDF_LIGHT_POINT);

   if (!sl->dirty_frustum)
      return &sl->frustum;

   if (vkdf_light_get_type(sl->light) == VKDF_LIGHT_SPOTLIGHT) {
      float aperture_angle =
         RAD_TO_DEG(vkdf_light_get_aperture_angle(sl->light));
      vkdf_frustum_compute(&sl->frustum, true, true,
                           vkdf_light_get_position(sl->light),
                           vkdf_light_get_rotation(sl->light),
                           sl->shadow.spec.shadow_map_near,
                           sl->shadow.spec.shadow_map_far,
                           aperture_angle,
                           1.0f);
   } else if (vkdf_light_get_type(sl->light) == VKDF_LIGHT_DIRECTIONAL) {
      vkdf_frustum_compute(&sl->frustum, true, true,
                           vkdf_camera_get_position(s->camera),
                           vkdf_camera_get_rotation(s->camera),
                           sl->shadow.spec.shadow_map_near,
                           sl->shadow.spec.shadow_map_far,
                           s->camera->proj.fov,
                           s->camera->proj.aspect_ratio);
   }

   sl->dirty_frustum = false;
   return &sl->frustum;
}

static void
compute_visible_tiles_for_light(VkdfScene *s, VkdfSceneLight *sl)
{
   // The Light must be a shadow caster and we should've a shadow map image
   assert(vkdf_light_casts_shadows(sl->light));
   assert(sl->shadow.shadow_map.image);

   // FIXME: support point lights
   assert(vkdf_light_get_type(sl->light) != VKDF_LIGHT_POINT);

   // Compute light frustum bounds for clipping
   const VkdfFrustum *f = scene_light_get_frustum(s, sl);
   const VkdfBox *frustum_box = vkdf_frustum_get_box(f);
   const VkdfPlane *frustum_planes = vkdf_frustum_get_planes(f);

   // Find the list of tiles visible to this light
   // FIXME: thread this?
   sl->shadow.visible = find_visible_tiles(s, 0, s->num_tiles.total - 1,
                                           frustum_box, frustum_planes);

#if 0
   // Trim the list of visible tiles further by testing the tiles that
   // passed the tests against the cone of the spotlight
   // FIXME: seems to work fine, but due to CPU/GPU precission differences
   // the vkdf_box_box_is_in_cone() function requires some error margin
   // that reduces its effectiviness, so disable it for now.
   GList *iter = sl->shadow.visible;
   while (iter) {
      VkdfSceneTile *t = (VkdfSceneTile *) iter->data;
      if (!vkdf_box_is_in_cone(&t->box,
                               vkdf_light_get_position(sl->light),
                               vec3(vkdf_light_get_direction(sl->light)),
                               vkdf_light_get_cutoff_factor(sl->light))) {
         GList *tmp = iter;
         iter = g_list_next(iter);
         sl->shadow.visible = g_list_delete_link(sl->shadow.visible, tmp);
         // vkdf_info("scene: spotlight cone culling success.\n");
      } else {
         iter = g_list_next(iter);
      }
   }
#endif
}

static inline void
start_recording_shadow_maps_cmd_buf(VkdfScene *s)
{
   if (s->cmd_buf.shadow_maps)
      new_inactive_cmd_buf(s, 0, s->cmd_buf.shadow_maps);

   vkdf_create_command_buffer(s->ctx,
                              s->cmd_buf.pool[0],
                              VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                              1, &s->cmd_buf.shadow_maps);

   vkdf_command_buffer_begin(s->cmd_buf.shadow_maps,
                             VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
}

static inline void
stop_recording_shadow_maps_cmd_buf(VkdfScene *s)
{
   vkdf_command_buffer_end(s->cmd_buf.shadow_maps);
}

static void
record_shadow_map_commands(VkdfScene *s,
                           VkdfSceneLight *sl,
                           GHashTable *dyn_sets)
{
   assert(sl->shadow.shadow_map.image);

   // FIXME: support point lights
   assert(vkdf_light_get_type(sl->light) != VKDF_LIGHT_POINT);

   VkClearValue clear_values[1];
   vkdf_depth_stencil_clear_set(clear_values, 1.0, 0);

   uint32_t shadow_map_size = sl->shadow.spec.shadow_map_size;

   VkRenderPassBeginInfo rp_begin;
   rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
   rp_begin.pNext = NULL;
   rp_begin.renderPass = s->shadows.renderpass;
   rp_begin.framebuffer = sl->shadow.framebuffer;
   rp_begin.renderArea.offset.x = 0;
   rp_begin.renderArea.offset.y = 0;
   rp_begin.renderArea.extent.width = shadow_map_size;
   rp_begin.renderArea.extent.height = shadow_map_size;
   rp_begin.clearValueCount = 1;
   rp_begin.pClearValues = clear_values;

   vkCmdBeginRenderPass(s->cmd_buf.shadow_maps,
                        &rp_begin,
                        VK_SUBPASS_CONTENTS_INLINE);

   // Dynamic viewport / scissor / depth bias
   record_viewport_and_scissor_commands(s->cmd_buf.shadow_maps,
                                        shadow_map_size,
                                        shadow_map_size);

   vkCmdSetDepthBias(s->cmd_buf.shadow_maps,
                     sl->shadow.spec.depth_bias_const_factor,
                     0.0f,
                     sl->shadow.spec.depth_bias_slope_factor);

   // Push constants (Light View/projection)
   vkCmdPushConstants(s->cmd_buf.shadow_maps,
                      s->shadows.pipeline.layout,
                      VK_SHADER_STAGE_VERTEX_BIT,
                      0, sizeof(_shadow_map_pcb), &sl->shadow.viewproj[0][0]);

   VkPipeline current_pipeline = 0;

   // Render static objects
   if (s->static_shadow_caster_count > 0) {
      // Descriptor sets (UBO with object model matrices)
      vkCmdBindDescriptorSets(s->cmd_buf.shadow_maps,
                              VK_PIPELINE_BIND_POINT_GRAPHICS,
                              s->shadows.pipeline.layout,
                              0,                               // First decriptor set
                              1,                               // Descriptor set count
                              &s->shadows.pipeline.models_set, // Descriptor sets
                              0,                               // Dynamic offset count
                              NULL);                           // Dynamic offsets

      // For each tile visible from this light source...
      GList *tile_iter = sl->shadow.visible;
      while (tile_iter) {
         VkdfSceneTile *tile = (VkdfSceneTile *) tile_iter->data;
         assert(tile);

         // For each object type in this tile...
         GList *set_iter = s->set_ids;
         while (set_iter) {
            const char *set_id = (const char *) set_iter->data;
            VkdfSceneSetInfo *set_info =
               (VkdfSceneSetInfo *) g_hash_table_lookup(tile->sets, set_id);

            // If there are shadow caster objects of this type...
            if (set_info->shadow_caster_count > 0) {
               // Grab the model (it is shared across all objects in the same type)
               VkdfObject *obj = (VkdfObject *) set_info->objs->data;
               VkdfModel *model = obj->model;
               assert(model);

               // For each mesh in this model...
               for (uint32_t i = 0; i < model->meshes.size(); i++) {
                  VkdfMesh *mesh = model->meshes[i];

                  if (mesh->active == false)
                     continue;

                  // Bind pipeline
                  // FIXME: can we do without a hashtable lookup here?
                  uint32_t vertex_data_stride =
                     vkdf_mesh_get_vertex_data_stride(mesh);
                  VkPrimitiveTopology primitive = vkdf_mesh_get_primitive(mesh);
                  void *hash = GINT_TO_POINTER(
                     hash_shadow_map_pipeline_spec(vertex_data_stride, primitive));
                  VkPipeline pipeline = (VkPipeline)
                     g_hash_table_lookup(s->shadows.pipeline.pipelines, hash);
                  assert(pipeline);

                  if (pipeline != current_pipeline) {
                     vkCmdBindPipeline(s->cmd_buf.shadow_maps,
                                       VK_PIPELINE_BIND_POINT_GRAPHICS,
                                       pipeline);
                     current_pipeline = pipeline;
                  }

                  // FIXME: should we make this a callback to the app so it can
                  // have better control of what and how gets rendered to the
                  // shadow map?

                  // Draw all instances
                  const VkDeviceSize offsets[1] = { 0 };
                  vkCmdBindVertexBuffers(s->cmd_buf.shadow_maps,
                                         0,                       // Start Binding
                                         1,                       // Binding Count
                                         &mesh->vertex_buf.buf,   // Buffers
                                         offsets);                // Offsets

                  vkdf_mesh_draw(mesh,
                                 s->cmd_buf.shadow_maps,
                                 set_info->shadow_caster_count,
                                 set_info->shadow_caster_start_index);
               }
            }
            set_iter = g_list_next(set_iter);
         }
         tile_iter = g_list_next(tile_iter);
      }
   }

   // Render dynamic objects
   vkCmdBindDescriptorSets(s->cmd_buf.shadow_maps,
                           VK_PIPELINE_BIND_POINT_GRAPHICS,
                           s->shadows.pipeline.layout,
                           0,                                   // First decriptor set
                           1,                                   // Descriptor set count
                           &s->shadows.pipeline.dyn_models_set, // Descriptor sets
                           0,                                   // Dynamic offset count
                           NULL);                               // Dynamic offsets

   char *set_id;
   VkdfSceneSetInfo *set_info;
   GHashTableIter set_iter;
   g_hash_table_iter_init(&set_iter, dyn_sets);
   while (g_hash_table_iter_next(&set_iter, (void **)&set_id, (void **)&set_info)) {
      if (!set_info || set_info->shadow_caster_count == 0)
         continue;

      // Grab the model (it is shared across all objects in the same type)
      VkdfObject *obj = (VkdfObject *) set_info->objs->data;
      VkdfModel *model = obj->model;
      assert(model);

      // For each mesh in this model...
      for (uint32_t i = 0; i < model->meshes.size(); i++) {
         VkdfMesh *mesh = model->meshes[i];

         if (mesh->active == false)
            continue;

         // Bind pipeline
         // FIXME: can we do without a hashtable lookup here?
         uint32_t vertex_data_stride =
            vkdf_mesh_get_vertex_data_stride(mesh);
         VkPrimitiveTopology primitive = vkdf_mesh_get_primitive(mesh);
         void *hash = GINT_TO_POINTER(
            hash_shadow_map_pipeline_spec(vertex_data_stride, primitive));
         VkPipeline pipeline = (VkPipeline)
            g_hash_table_lookup(s->shadows.pipeline.pipelines, hash);
         assert(pipeline);

         if (pipeline != current_pipeline) {
            vkCmdBindPipeline(s->cmd_buf.shadow_maps,
                              VK_PIPELINE_BIND_POINT_GRAPHICS,
                              pipeline);
            current_pipeline = pipeline;
         }

         // Draw all instances
         const VkDeviceSize offsets[1] = { 0 };
         vkCmdBindVertexBuffers(s->cmd_buf.shadow_maps,
                                0,                       // Start Binding
                                1,                       // Binding Count
                                &mesh->vertex_buf.buf,   // Buffers
                                offsets);                // Offsets

         vkdf_mesh_draw(mesh,
                        s->cmd_buf.shadow_maps,
                        set_info->shadow_caster_count,
                        set_info->shadow_caster_start_index);
      }
   }

   vkCmdEndRenderPass(s->cmd_buf.shadow_maps);
}

static bool
skip_shadow_map_frame(VkdfSceneLight *sl)
{
   /* If frame_counter < 0 it means that the shadow map has never
    * been recorded yet, so we can't skip
    */
   if (sl->shadow.frame_counter < 0)
      return false;

   /* If skip_frames < 0 it means we never want to update the shadow map */
   if (sl->shadow.spec.skip_frames < 0)
      return true;

   /* Otherwise, update only if we have skipped the requested frames */
   if (sl->shadow.frame_counter < sl->shadow.spec.skip_frames)
      return true;

   return false;
}

static bool
vkdf_scene_light_has_dirty_shadows(VkdfSceneLight *sl)
{
   if (!vkdf_light_has_dirty_shadows(sl->light))
      return false;

   return !skip_shadow_map_frame(sl);
}

static void
record_dirty_light_resource_updates(VkdfScene *s)
{
   assert(s->lights_dirty);

   uint32_t num_lights = s->lights.size();

   // FIXME: maybe a single update of the entire buffer is faster if we have
   // too many dirty lights
   VkDeviceSize light_inst_size = ALIGN(sizeof(VkdfLight), 16);
   for (uint32_t i = 0; i < num_lights; i++) {
      VkdfSceneLight *sl = s->lights[i];
      if (!vkdf_light_is_dirty(sl->light))
         continue;

      assert(light_inst_size < 64 * 1024);
      vkCmdUpdateBuffer(s->cmd_buf.update_resources,
                        s->ubo.light.buf.buf,
                        i * light_inst_size, light_inst_size,
                        sl->light);
   }

   s->cmd_buf.have_resource_updates = true;
}

static void
record_dirty_shadow_map_resource_updates(VkdfScene *s)
{
   assert(s->shadow_maps_dirty);

   uint32_t num_lights = s->lights.size();

   VkDeviceSize base_offset = s->ubo.light.shadow_map_data_offset;
   VkDeviceSize shadow_map_inst_size =
      ALIGN(sizeof(struct _shadow_map_ubo_data), 16);
   for (uint32_t i = 0; i < num_lights; i++) {
      VkdfSceneLight *sl = s->lights[i];
      if (!vkdf_light_casts_shadows(sl->light))
         continue;
      if (!vkdf_scene_light_has_dirty_shadows(sl))
         continue;

      struct _shadow_map_ubo_data data;
      memcpy(&data.light_viewproj[0][0],
             &sl->shadow.viewproj[0][0], sizeof(glm::mat4));
      memcpy(&data.shadow_map_size,
             &sl->shadow.spec.shadow_map_size, sizeof(uint32_t));
      memcpy(&data.pcf_kernel_size,
             &sl->shadow.spec.pcf_kernel_size, sizeof(uint32_t));

      assert(shadow_map_inst_size < 64 * 1024);
      vkCmdUpdateBuffer(s->cmd_buf.update_resources,
                        s->ubo.light.buf.buf,
                        base_offset + i * shadow_map_inst_size,
                        shadow_map_inst_size,
                        &data);
   }

   s->cmd_buf.have_resource_updates = true;
}

static GHashTable *
find_dynamic_objects_for_light(VkdfScene *s,
                               VkdfSceneLight *sl,
                               bool *has_dirty_objects)
{
   // If a dynamic objects is not dirty it doesn't invalidate an existing
   // shadow map. If no dynamic object invalidates it we can skip its update.
   *has_dirty_objects = false;

   GHashTable *dyn_sets =
      g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

   // Go through the list of dynamic objects and check if any of them is
   // inside in any of the visible tiles for this light
   GHashTableIter iter;
   char *id;
   VkdfSceneSetInfo *info;

   // Notice that in order to test if a dynamic objects is visible to a light
   // we can't rely on the know list of vible tiles for the light. This is
   // because tile boxes are shrunk to fit the objects in it, so it could be
   // that a dynamic object is inside the tile but not inside its box, or even
   // that the object is inside a tile that is visible to the light but that is
   // not in its list of visible tiles because it doesn't have any static
   // objects or it doesn't have any visible to the light. Therefore,
   // we need to test for visibility by doing frustum testing for each object.

   // FIXME: Support point lights
   assert(vkdf_light_get_type(sl->light) != VKDF_LIGHT_POINT);

   const VkdfFrustum *f = scene_light_get_frustum(s, sl);
   const VkdfBox *light_box = vkdf_frustum_get_box(f);
   const VkdfPlane *light_planes = vkdf_frustum_get_planes(f);

   uint32_t start_index = 0;
   g_hash_table_iter_init(&iter, s->dynamic.sets);
   while (g_hash_table_iter_next(&iter, (void **)&id, (void **)&info)) {
      if (!info || info->count == 0)
         continue;

      VkdfSceneSetInfo *dyn_info = g_new0(VkdfSceneSetInfo, 1);
      g_hash_table_replace(dyn_sets, g_strdup(id), dyn_info);
      dyn_info->shadow_caster_start_index = start_index;

      GList *obj_iter = info->objs;
      while (obj_iter) {
         VkdfObject *obj = (VkdfObject *) obj_iter->data;
         if (vkdf_object_casts_shadows(obj)) {
            VkdfBox *obj_box = vkdf_object_get_box(obj);
            if (vkdf_box_is_in_frustum(obj_box, light_box, light_planes) != OUTSIDE) {
               dyn_info->objs = g_list_prepend(dyn_info->objs, obj);
               dyn_info->shadow_caster_count++;
               start_index++;

               if (vkdf_object_is_dirty(obj))
                  *has_dirty_objects = true;
            }
         }
         obj_iter = g_list_next(obj_iter);
      }
   }

   return dyn_sets;
}

static bool
record_scene_dynamic_shadow_map_resource_updates(VkdfScene *s,
                                                 GList *dirty_shadow_maps)
{
   if (!dirty_shadow_maps)
      return false;

   // Generate host buffer with data
   //
   // We store visible objects to each light contiguously so we can use
   // instanced rendering. Because the same object can be seen by multiple
   // lights, we may have to replicate object data for each light.
   uint8_t *mem = (uint8_t *) s->dynamic.ubo.shadow_map.host_buf;
   VkDeviceSize offset = 0;

   GList *sm_iter = dirty_shadow_maps;
   while (sm_iter) {
      struct _DirtyShadowMapInfo *ds
         = (struct _DirtyShadowMapInfo *) sm_iter->data;
      uint32_t count = 0;

      char *id;
      VkdfSceneSetInfo *info;
      GHashTableIter set_iter;
      g_hash_table_iter_init(&set_iter, ds->dyn_sets);
      while (g_hash_table_iter_next(&set_iter, (void **)&id, (void **)&info)) {
         if (!info || info->shadow_caster_count == 0)
            continue;

         // Sanity check
         assert(count == info->shadow_caster_start_index);

         GList *obj_iter = info->objs;
         while (obj_iter) {
            VkdfObject *obj = (VkdfObject *) obj_iter->data;

            // Model matrix
            glm::mat4 model = vkdf_object_get_model_matrix(obj);
            memcpy(mem + offset,
                   &model[0][0], sizeof(glm::mat4));
            offset += sizeof(glm::mat4);

            offset = ALIGN(offset, 16);

            count++;
            obj_iter = g_list_next(obj_iter);
         }
      }

      sm_iter = g_list_next(sm_iter);
   }

   // If offset > 0 then we have at least one dynamic object that needs
   // to be updated
   if (offset > 0) {
      assert(offset < 64 * 1024);
      vkCmdUpdateBuffer(s->cmd_buf.update_resources,
                        s->dynamic.ubo.shadow_map.buf.buf,
                        0, offset, mem);

      s->cmd_buf.have_resource_updates = true;
   }

   return s->cmd_buf.have_resource_updates;
}

static inline void
free_dirty_shadow_map_info_list(GList **dirty_shadow_map_list)
{
   assert(*dirty_shadow_map_list);

   GList *iter = *dirty_shadow_map_list;
   do {
      struct _DirtyShadowMapInfo *ds =
         (struct _DirtyShadowMapInfo *) iter->data;
      g_hash_table_foreach(ds->dyn_sets, destroy_set, NULL);
      g_hash_table_destroy(ds->dyn_sets);
      /* warning: ds points to stack-allocated data so don't free it here */
      iter = g_list_next(iter);
   } while (iter);
   g_list_free(*dirty_shadow_map_list);
   *dirty_shadow_map_list = NULL;
}

static void
thread_shadow_map_update(uint32_t thread_id, void *arg)
{
   struct LightThreadData *data = (struct LightThreadData *) arg;

   VkdfScene *s = data->s;
   VkdfSceneLight *sl = data->sl;
   VkdfLight *l = sl->light;

   // FIXME: for spolights, if neither the spotlight nor its area of
   // influence are visible to the camera, then we can skip shadow map
   // updates. This requires frustum vs frustum testing or maybe a
   // cone vs frustum collision test. For point lights we could probably
   // use a similar check.

   // If the light has dirty shadows it means that its area of influence
   // has changed and we need to recompute its list of visible tiles.
   if (vkdf_scene_light_has_dirty_shadows(sl)) {
      data->has_dirty_shadow_map = true;
      compute_light_view_projection(s, sl);
      compute_visible_tiles_for_light(s, sl);
   }

   // Whether the area of influence has changed or not, we need to check if
   // we need to regen shadow maps due to dynamic objects anyway. If the
   // light has dynamic objects in its area of influence then we also need
   // an updated list of objects so we can render them to the shadow map
   //
   // We need to update the shadow maps in this case even if we are skipping
   // shadow map frames, since otherwise we get self-shadowing on dynamic
   // objects
   bool has_dirty_objects;
   GHashTable *dyn_sets =
      find_dynamic_objects_for_light(s, sl, &has_dirty_objects);
   data->has_dirty_shadow_map = data->has_dirty_shadow_map || has_dirty_objects;

   if (data->has_dirty_shadow_map) {
      data->shadow_map_info.sl = sl;
      data->shadow_map_info.dyn_sets = dyn_sets;
   }
}

static bool
directional_light_has_dirty_shadow_map(VkdfScene *s, VkdfSceneLight *sl)
{
   VkdfCamera *cam = vkdf_scene_get_camera(s);

   if (vkdf_light_has_dirty_shadows(sl->light))
      return true;

   glm::vec3 cam_pos = vkdf_camera_get_position(cam);
   if (cam->pos != sl->shadow.directional.cam_pos)
      return true;

   glm::vec3 cam_rot = vkdf_camera_get_rotation(cam);
   if (cam_rot != sl->shadow.directional.cam_rot)
      return true;

   return false;
}

static void
update_dirty_lights(VkdfScene *s)
{
   s->lights_dirty = false;
   s->shadow_maps_dirty = false;

   uint32_t num_lights = s->lights.size();
   if (num_lights == 0)
      return;

   // Go through the list of lights and check if they are dirty and if they
   // require new shadow maps. If they require new shadow maps, record
   // the command buffers for them. We thread the shadow map checks per light.

   // If all lights are shadow casters then we can have as much that many
   // dirty shadow maps
   std::vector<struct LightThreadData> data;
   data.resize(num_lights);
   uint32_t data_count = 0;

   bool has_thread_jobs = false;
   for (uint32_t i = 0; i < num_lights; i++) {
      VkdfSceneLight *sl = s->lights[i];
      VkdfLight *l = sl->light;

      // Directional ligthts are special because the shadow box that defines
      // the shadow map changes as the camera moves around.
      if (vkdf_light_get_type(l) == VKDF_LIGHT_DIRECTIONAL &&
          vkdf_light_casts_shadows(l) &&
          directional_light_has_dirty_shadow_map(s, sl)) {
         compute_light_projection(s, sl);
         vkdf_light_set_dirty_shadows(l, true);
      }

      if (vkdf_light_is_dirty(l))
         s->lights_dirty = true;

      if (vkdf_scene_light_has_dirty_shadows(sl))
         sl->dirty_frustum = true;

      if (!vkdf_light_casts_shadows(l))
         continue;

      data[data_count].id = i;
      data[data_count].s = s;
      data[data_count].sl = sl;

      if (s->thread.pool) {
         has_thread_jobs = true;
         vkdf_thread_pool_add_job(s->thread.pool,
                                  thread_shadow_map_update,
                                  &data[data_count]);
      } else {
         thread_shadow_map_update(0, &data[data_count]);
      }

      data_count++;
   }

   // Wait for all threads to finish
   if (has_thread_jobs)
      vkdf_thread_pool_wait(s->thread.pool);

   // Check if we have at least one shadow map that we need to update.
   uint32_t first_dirty_shadow_map = 0;
   for (; first_dirty_shadow_map < data_count; first_dirty_shadow_map++) {
      if (data[first_dirty_shadow_map].has_dirty_shadow_map) {
         s->shadow_maps_dirty = true;
         break;
      }
   }

   /* Record the commands to update scene light resources for rendering, this
    * includes:
    *
    * 1. Dirty light descriptions
    * 2. Dirty shadow map descriptions
    * 3. Dynamic objects that need to be rendered into each shadow map
    */
   if (s->lights_dirty)
      record_dirty_light_resource_updates(s);

   GList *dirty_shadow_map_list = NULL;
   if (s->shadow_maps_dirty) {
      record_dirty_shadow_map_resource_updates(s);

      for (int i = first_dirty_shadow_map; i < data_count; i++) {
         if (!data[i].has_dirty_shadow_map)
            continue;
         struct _DirtyShadowMapInfo *ds = &data[i].shadow_map_info;
         dirty_shadow_map_list = g_list_prepend(dirty_shadow_map_list, ds);
      }
      record_scene_dynamic_shadow_map_resource_updates(s, dirty_shadow_map_list);
   }

   // Record command buffer for rendering dirty shadow maps
   if (s->shadow_maps_dirty) {
      start_recording_shadow_maps_cmd_buf(s);
      for (int i = first_dirty_shadow_map; i < data_count; i++) {
         if (!data[i].has_dirty_shadow_map)
            continue;
         struct _DirtyShadowMapInfo *ds = &data[i].shadow_map_info;
         record_shadow_map_commands(s, ds->sl, ds->dyn_sets);
      }
      stop_recording_shadow_maps_cmd_buf(s);
   }

   if (dirty_shadow_map_list)
      free_dirty_shadow_map_info_list(&dirty_shadow_map_list);

   // Clean-up dirty bits on the lights now
   for (uint32_t i = 0; i < num_lights; i++) {
      VkdfSceneLight *sl = s->lights[i];

      if (vkdf_scene_light_has_dirty_shadows(sl)) {
         vkdf_light_set_dirty_shadows(sl->light, false);
         sl->shadow.frame_counter = 0;
      } else {
         sl->shadow.frame_counter++;
      }

      bitfield_unset(&sl->light->dirty,
                     VKDF_LIGHT_DIRTY | VKDF_LIGHT_DIRTY_VIEW);
   }
}

/**
 * Prepares state and resources required by light sources:
 * - Prepares rendering resources for shadow maps
 */
static void
prepare_scene_lights(VkdfScene *s)
{
   // Create shared rendering resources for shadow maps
   create_shadow_map_renderpass(s);
   create_shadow_map_pipelines(s);

   // Create per-light resources
   for (uint32_t i = 0; i < s->lights.size(); i++) {
      VkdfSceneLight *sl = s->lights[i];
      if (vkdf_light_casts_shadows(s->lights[i]->light))
         create_shadow_map_framebuffer(s, sl);
   }
}

void
vkdf_scene_set_clear_values(VkdfScene *s,
                            VkClearValue *color,
                            VkClearValue *depth)
{
   // Color clear is optional, depth is mandatory
   assert(depth != NULL);
   s->rp.do_color_clear = color != NULL;

   if (color) {
      s->rp.clear_values[0] = *color;
   } else {
      vkdf_color_clear_set(&s->rp.clear_values[0],
                           glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
   }

   s->rp.clear_values[1] = *depth;
}

static void
prepare_forward_render_passes(VkdfScene *s)
{
   s->rp.static_geom.renderpass =
      vkdf_renderpass_simple_new(s->ctx,
                                 s->rt.color.format,
                                 s->rp.do_color_clear ?
                                    VK_ATTACHMENT_LOAD_OP_CLEAR :
                                    VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                 VK_ATTACHMENT_STORE_OP_STORE,
                                 VK_IMAGE_LAYOUT_UNDEFINED,
                                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                 s->rt.depth.format,
                                 s->rp.do_depth_prepass ?
                                    VK_ATTACHMENT_LOAD_OP_LOAD :
                                    VK_ATTACHMENT_LOAD_OP_CLEAR,
                                 VK_ATTACHMENT_STORE_OP_STORE,
                                 s->rp.do_depth_prepass ?
                                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL :
                                    VK_IMAGE_LAYOUT_UNDEFINED,
                                 VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

   s->rp.static_geom.framebuffer =
      vkdf_create_framebuffer(s->ctx,
                              s->rp.static_geom.renderpass,
                              s->rt.color.view,
                              s->rt.width, s->rt.height,
                              1, &s->rt.depth);

   s->rp.dynamic_geom.renderpass =
      vkdf_renderpass_simple_new(s->ctx,
                                 s->rt.color.format,
                                 VK_ATTACHMENT_LOAD_OP_LOAD,
                                 VK_ATTACHMENT_STORE_OP_STORE,
                                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                 s->rt.depth.format,
                                 VK_ATTACHMENT_LOAD_OP_LOAD,
                                 VK_ATTACHMENT_STORE_OP_STORE,
                                 VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                 VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

   s->rp.dynamic_geom.framebuffer =
      vkdf_create_framebuffer(s->ctx,
                              s->rp.dynamic_geom.renderpass,
                              s->rt.color.view,
                              s->rt.width, s->rt.height,
                              1, &s->rt.depth);
}

static VkRenderPass
create_gbuffer_render_pass(VkdfScene *s, bool for_dynamic)
{
   // Attachments: Depth + Gbuffer
   VkAttachmentDescription atts[1 + GBUFFER_MAX_SIZE];

   uint32_t idx = 0;
   int32_t depth_idx;
   int32_t gbuffer_idx;

   // Attachent 0: Depth
   bool load_depth = for_dynamic || s->rp.do_depth_prepass;

   assert(s->rt.depth.format != VK_FORMAT_UNDEFINED);
   atts[idx].format = s->rt.depth.format;
   atts[idx].samples = VK_SAMPLE_COUNT_1_BIT;
   atts[idx].loadOp = load_depth ? VK_ATTACHMENT_LOAD_OP_LOAD :
                                   VK_ATTACHMENT_LOAD_OP_CLEAR;
   atts[idx].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
   atts[idx].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
   atts[idx].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
   atts[idx].initialLayout =
      load_depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL :
                   VK_IMAGE_LAYOUT_UNDEFINED;
   atts[idx].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
   atts[idx].flags = 0;
   depth_idx = idx++;

   // Attachments 1..N: Gbuffer
   gbuffer_idx = idx;
   for (uint32_t i = 0; i < s->rt.gbuffer_size; i++) {
      atts[idx].format = s->rt.gbuffer[i].format;
      atts[idx].samples = VK_SAMPLE_COUNT_1_BIT;
      atts[idx].loadOp =  for_dynamic ? VK_ATTACHMENT_LOAD_OP_LOAD :
                                        VK_ATTACHMENT_LOAD_OP_CLEAR;
      atts[idx].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      atts[idx].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      atts[idx].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
      atts[idx].initialLayout =
         for_dynamic ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL :
                       VK_IMAGE_LAYOUT_UNDEFINED;
      atts[idx].finalLayout =
         for_dynamic ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL :
                       VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      atts[idx].flags = 0;
      idx++;
   }

   // Attachment references from subpasses
   VkAttachmentReference depth_ref;
   depth_ref.attachment = depth_idx;
   depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

   VkAttachmentReference gbuffer_ref[GBUFFER_MAX_SIZE];
   for (uint32_t i = 0; i < s->rt.gbuffer_size; i++) {
      gbuffer_ref[i].attachment = gbuffer_idx + i;
      gbuffer_ref[i].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
   }

   // Single subpass
   VkSubpassDescription subpass[1];
   subpass[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
   subpass[0].flags = 0;
   subpass[0].inputAttachmentCount = 0;
   subpass[0].pInputAttachments = NULL;
   subpass[0].colorAttachmentCount = s->rt.gbuffer_size;
   subpass[0].pColorAttachments = gbuffer_ref;
   subpass[0].pResolveAttachments = NULL;
   subpass[0].pDepthStencilAttachment = &depth_ref;
   subpass[0].preserveAttachmentCount = 0;
   subpass[0].pPreserveAttachments = NULL;

   // Create render pass
   VkRenderPassCreateInfo rp_info;
   rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
   rp_info.pNext = NULL;
   rp_info.attachmentCount = s->rt.gbuffer_size + 1;
   rp_info.pAttachments = atts;
   rp_info.subpassCount = 1;
   rp_info.pSubpasses = subpass;
   rp_info.dependencyCount = 0;
   rp_info.pDependencies = NULL;
   rp_info.flags = 0;

   VkRenderPass render_pass;
   VK_CHECK(vkCreateRenderPass(s->ctx->device, &rp_info, NULL, &render_pass));

   return render_pass;
}

static inline VkRenderPass
create_gbuffer_merge_render_pass(VkdfScene *s)
{
   // The gbuffer merge shader can output in the clear color for pixels not
   // rendered in the gbuffer pass. This gives apps the opportunity to skip
   // the color clear in this pass.
   return  vkdf_renderpass_simple_new(
               s->ctx,
               s->rt.color.format,
               s->rp.do_color_clear ?
                  VK_ATTACHMENT_LOAD_OP_CLEAR :
                  VK_ATTACHMENT_LOAD_OP_DONT_CARE,
               VK_ATTACHMENT_STORE_OP_STORE,
               VK_IMAGE_LAYOUT_UNDEFINED,
               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
               VK_FORMAT_UNDEFINED,
               VK_ATTACHMENT_LOAD_OP_DONT_CARE,
               VK_ATTACHMENT_STORE_OP_DONT_CARE,
               VK_IMAGE_LAYOUT_UNDEFINED,
               VK_IMAGE_LAYOUT_UNDEFINED);
}

static void
prepare_deferred_render_passes(VkdfScene *s)
{
   /* Setup depth and gbuffer color clear values */
   vkdf_depth_stencil_clear_set(&s->rp.gbuffer_clear_values[0], 1.0f, 0);

   for (uint32_t i = 0; i < s->rt.gbuffer_size; i++) {
      vkdf_color_clear_set(&s->rp.gbuffer_clear_values[i + 1],
                           glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
   }

   /* Depth + GBuffer render passes */
   s->rp.static_geom.renderpass = create_gbuffer_render_pass(s, false);

   s->rp.static_geom.framebuffer =
      vkdf_create_framebuffer(s->ctx,
                              s->rp.static_geom.renderpass,
                              s->rt.depth.view,
                              s->rt.width, s->rt.height,
                              s->rt.gbuffer_size, s->rt.gbuffer);

   s->rp.dynamic_geom.renderpass = create_gbuffer_render_pass(s, true);

   s->rp.dynamic_geom.framebuffer =
      vkdf_create_framebuffer(s->ctx,
                              s->rp.dynamic_geom.renderpass,
                              s->rt.depth.view,
                              s->rt.width, s->rt.height,
                              s->rt.gbuffer_size, s->rt.gbuffer);

   /* Merge render pass */
   s->rp.gbuffer_merge.renderpass = create_gbuffer_merge_render_pass(s);

   s->rp.gbuffer_merge.framebuffer =
      vkdf_create_framebuffer(s->ctx,
                              s->rp.gbuffer_merge.renderpass,
                              s->rt.color.view,
                              s->rt.width, s->rt.height,
                              0, NULL);
}

static void
prepare_depth_prepass_render_passes(VkdfScene *s)
{
   s->rp.dpp_static_geom.renderpass =
      create_depth_renderpass(s, VK_ATTACHMENT_LOAD_OP_CLEAR, false);

   s->rp.dpp_static_geom.framebuffer =
      create_depth_framebuffer(s,
                               s->rt.width,
                               s->rt.height,
                               s->rp.dpp_static_geom.renderpass,
                               s->rt.depth.view);

   s->rp.dpp_dynamic_geom.renderpass =
      create_depth_renderpass(s, VK_ATTACHMENT_LOAD_OP_LOAD, false);

   s->rp.dpp_dynamic_geom.framebuffer =
      create_depth_framebuffer(s,
                               s->rt.width,
                               s->rt.height,
                               s->rp.dpp_dynamic_geom.renderpass,
                               s->rt.depth.view);
}

struct SsaoPCB {
   glm::mat4 proj;
   glm::vec2 noise_scale;
   float radius;
   float bias;
   float intensity;
   float aspect_ratio;
   float tan_half_fov;
};

struct SsaoBlurPCB {
   float threshold;
   float near_plane;
   float far_plane;
};

static VkCommandBuffer
record_ssao_cmd_buf(VkdfScene *s)
{
   VkCommandBuffer cmd_buf;

   vkdf_create_command_buffer(s->ctx,
                              s->cmd_buf.pool[0],
                              VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                              1, &cmd_buf);

   vkdf_command_buffer_begin(cmd_buf,
                             VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT);

   /* Base pass */
   VkRenderPassBeginInfo rp_begin =
      vkdf_renderpass_begin_new(s->ssao.base.rp.renderpass,
                                s->ssao.base.rp.framebuffer,
                                0, 0, s->ssao.width, s->ssao.height,
                                0, NULL);

   vkCmdBeginRenderPass(cmd_buf,
                        &rp_begin,
                        VK_SUBPASS_CONTENTS_INLINE);

   record_viewport_and_scissor_commands(cmd_buf, s->ssao.width, s->ssao.height);

   vkCmdBindPipeline(cmd_buf,
                     VK_PIPELINE_BIND_POINT_GRAPHICS,
                     s->ssao.base.pipeline.pipeline);

   struct SsaoPCB pcb;
   const glm::mat4 *proj_ptr = vkdf_camera_get_projection_ptr(s->camera);
   memcpy(&pcb.proj[0][0], proj_ptr, sizeof(glm::mat4));
   pcb.noise_scale = s->ssao.noise_scale;
   pcb.radius = s->ssao.radius;
   pcb.bias = s->ssao.bias;
   pcb.intensity = s->ssao.intensity;
   pcb.aspect_ratio = s->camera->proj.aspect_ratio;
   pcb.tan_half_fov = tanf(glm::radians(s->camera->proj.fov / 2.0f));

   vkCmdPushConstants(cmd_buf,
                      s->ssao.base.pipeline.layout,
                      VK_SHADER_STAGE_VERTEX_BIT |
                        VK_SHADER_STAGE_FRAGMENT_BIT,
                      0, sizeof(struct SsaoPCB), &pcb);

   VkDescriptorSet descriptor_sets[] = {
      s->ssao.base.pipeline.samples_set,
      s->ssao.base.pipeline.textures_set,
   };

   vkCmdBindDescriptorSets(cmd_buf,
                           VK_PIPELINE_BIND_POINT_GRAPHICS,
                           s->ssao.base.pipeline.layout,
                           0, 2, descriptor_sets,  // First, count, sets
                           0, NULL);               // Dynamic offsets

   vkCmdDraw(cmd_buf, 4, 1, 0, 0);

   vkCmdEndRenderPass(cmd_buf);

   /* Blur pass */
   if (s->ssao.blur_size == 0) {
      /* No blur */
      vkdf_command_buffer_end(cmd_buf);
      return cmd_buf;
   }

   rp_begin =
      vkdf_renderpass_begin_new(s->ssao.blur.rp.renderpass,
                                s->ssao.blur.rp.framebuffer,
                                0, 0, s->ssao.width, s->ssao.height,
                                0, NULL);

   vkCmdBeginRenderPass(cmd_buf,
                        &rp_begin,
                        VK_SUBPASS_CONTENTS_INLINE);

   record_viewport_and_scissor_commands(cmd_buf, s->ssao.width, s->ssao.height);

   vkCmdBindPipeline(cmd_buf,
                     VK_PIPELINE_BIND_POINT_GRAPHICS,
                     s->ssao.blur.pipeline.pipeline);

   struct SsaoBlurPCB pcb_blur;
   pcb_blur.threshold = s->ssao.blur_threshold;
   pcb_blur.near_plane = s->camera->proj.near_plane;
   pcb_blur.far_plane = s->camera->proj.far_plane;

   vkCmdPushConstants(cmd_buf,
                      s->ssao.blur.pipeline.layout,
                      VK_SHADER_STAGE_FRAGMENT_BIT,
                      0, sizeof(SsaoBlurPCB), &pcb_blur);

   VkDescriptorSet blur_descriptor_sets[] = {
      s->ssao.blur.pipeline.ssao_tex_set,
   };

   vkCmdBindDescriptorSets(cmd_buf,
                           VK_PIPELINE_BIND_POINT_GRAPHICS,
                           s->ssao.blur.pipeline.layout,
                           0, 1, blur_descriptor_sets,  // First, count, sets
                           0, NULL);                    // Dynamic offsets

   vkCmdDraw(cmd_buf, 4, 1, 0, 0);

   vkCmdEndRenderPass(cmd_buf);

   vkdf_command_buffer_end(cmd_buf);

   return cmd_buf;
}

static void
prepare_ssao_rendering(VkdfScene *s)
{
   /* SSAO render target output */
   s->ssao.base.image =
      vkdf_create_image(s->ctx,
                        s->ssao.width,
                        s->ssao.height,
                        1,
                        VK_IMAGE_TYPE_2D,
                        VK_FORMAT_R8_UNORM,
                        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
                           VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT,
                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                           VK_IMAGE_USAGE_SAMPLED_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        VK_IMAGE_ASPECT_COLOR_BIT,
                        VK_IMAGE_VIEW_TYPE_2D);

   /* Render passes */
   s->ssao.base.rp.renderpass =
      vkdf_renderpass_simple_new(s->ctx,
                                 s->ssao.base.image.format,
                                 VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                 VK_ATTACHMENT_STORE_OP_STORE,
                                 VK_IMAGE_LAYOUT_UNDEFINED,
                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                 VK_FORMAT_UNDEFINED,
                                 VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                 VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                 VK_IMAGE_LAYOUT_UNDEFINED,
                                 VK_IMAGE_LAYOUT_UNDEFINED);

   /* Render targets */
   s->ssao.base.rp.framebuffer =
      vkdf_create_framebuffer(s->ctx,
                              s->ssao.base.rp.renderpass,
                              s->ssao.base.image.view,
                              s->ssao.width, s->ssao.height,
                              0, NULL);

   /* Base SSAO pipeline */
   VkPushConstantRange pcb_range;
   pcb_range.stageFlags =
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
   pcb_range.offset = 0;
   pcb_range.size = sizeof(struct SsaoPCB);

   VkPushConstantRange pcb_ranges[] = {
      pcb_range,
   };

   s->ssao.base.pipeline.samples_set_layout =
      vkdf_create_ubo_descriptor_set_layout(s->ctx, 0, 1,
                                            VK_SHADER_STAGE_FRAGMENT_BIT,
                                            false);

   s->ssao.base.pipeline.textures_set_layout =
      vkdf_create_sampler_descriptor_set_layout(s->ctx, 0, 3,
                                                VK_SHADER_STAGE_FRAGMENT_BIT);

   VkDescriptorSetLayout layouts[] = {
      s->ssao.base.pipeline.samples_set_layout,
      s->ssao.base.pipeline.textures_set_layout
   };

   VkPipelineLayoutCreateInfo info;
   info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
   info.pNext = NULL;
   info.pushConstantRangeCount = 1;
   info.pPushConstantRanges = pcb_ranges;
   info.setLayoutCount = 2;
   info.pSetLayouts = layouts;
   info.flags = 0;

   VK_CHECK(vkCreatePipelineLayout(s->ctx->device, &info, NULL,
                                   &s->ssao.base.pipeline.layout));

   s->ssao.base.pipeline.shader.vs =
      vkdf_create_shader_module(s->ctx, SSAO_VS_SHADER_PATH);

   VkPipelineShaderStageCreateInfo vs_info;
   vkdf_pipeline_fill_shader_stage_info(&vs_info,
                                        VK_SHADER_STAGE_VERTEX_BIT,
                                        s->ssao.base.pipeline.shader.vs);

   s->ssao.base.pipeline.shader.fs =
      vkdf_create_shader_module(s->ctx, SSAO_FS_SHADER_PATH);

   VkPipelineShaderStageCreateInfo fs_info;
   VkSpecializationMapEntry entry = { 0, 0, sizeof(uint32_t) };
   VkSpecializationInfo fs_spec_info = {
      1,
      &entry,
      sizeof(uint32_t),
      &s->ssao.num_samples
   };
   vkdf_pipeline_fill_shader_stage_info(&fs_info,
                                        VK_SHADER_STAGE_FRAGMENT_BIT,
                                        s->ssao.base.pipeline.shader.fs,
                                        &fs_spec_info);

   s->ssao.base.pipeline.pipeline =
      vkdf_create_gfx_pipeline(s->ctx,
                               NULL,
                               0,
                               NULL,
                               0,
                               NULL,
                               false,
                               VK_COMPARE_OP_ALWAYS,
                               s->ssao.base.rp.renderpass,
                               s->ssao.base.pipeline.layout,
                               VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
                               VK_CULL_MODE_BACK_BIT,
                               1,
                               &vs_info, &fs_info);

   /* Base SSAO descriptor sets */
   s->ssao.base.pipeline.samples_set =
      create_descriptor_set(s->ctx,
                            s->ubo.static_pool,
                            s->ssao.base.pipeline.samples_set_layout);

   VkDeviceSize buf_offset = 0;
   VkDeviceSize buf_size = s->ssao.samples_buf.size;
   vkdf_descriptor_set_buffer_update(s->ctx,
                                     s->ssao.base.pipeline.samples_set,
                                     s->ssao.samples_buf.buf.buf,
                                     0, 1, &buf_offset, &buf_size, false, true);

   s->ssao.base.gbuffer_sampler = vkdf_ssao_create_gbuffer_sampler(s->ctx);

   s->ssao.base.pipeline.textures_set =
      create_descriptor_set(s->ctx,
                            s->sampler.pool,
                            s->ssao.base.pipeline.textures_set_layout);

   vkdf_descriptor_set_sampler_update(s->ctx,
                                      s->ssao.base.pipeline.textures_set,
                                      s->ssao.base.gbuffer_sampler,
                                      s->rt.depth.view,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                      SSAO_DEPTH_TEX_BINDING, 1);

   vkdf_descriptor_set_sampler_update(s->ctx,
                                      s->ssao.base.pipeline.textures_set,
                                      s->ssao.base.gbuffer_sampler,
                                      s->rt.gbuffer[GBUFFER_EYE_NORMAL_IDX].view,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                      SSAO_NORMAL_TEX_BINDING, 1);

   vkdf_descriptor_set_sampler_update(s->ctx,
                                      s->ssao.base.pipeline.textures_set,
                                      s->ssao.noise_sampler,
                                      s->ssao.noise_image.view,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                      SSAO_NOISE_TEX_BINDING, 1);

   if (s->ssao.blur_size > 0) {
      /* Blur render target output */
      if (s->ssao.blur_size > 0) {
         s->ssao.blur.image =
            vkdf_create_image(s->ctx,
                              s->ssao.width,
                              s->ssao.height,
                              1,
                              VK_IMAGE_TYPE_2D,
                              VK_FORMAT_R8_UNORM,
                              VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
                                 VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT,
                              VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                 VK_IMAGE_USAGE_SAMPLED_BIT,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                              VK_IMAGE_ASPECT_COLOR_BIT,
                              VK_IMAGE_VIEW_TYPE_2D);
      }

      /* Blur render pass */
      s->ssao.blur.rp.renderpass =
         vkdf_renderpass_simple_new(s->ctx,
                                    s->ssao.blur.image.format,
                                    VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                    VK_ATTACHMENT_STORE_OP_STORE,
                                    VK_IMAGE_LAYOUT_UNDEFINED,
                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                    VK_FORMAT_UNDEFINED,
                                    VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                    VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                    VK_IMAGE_LAYOUT_UNDEFINED,
                                    VK_IMAGE_LAYOUT_UNDEFINED);

      /* Blur framebuffer */
      s->ssao.blur.rp.framebuffer =
         vkdf_create_framebuffer(s->ctx,
                                 s->ssao.blur.rp.renderpass,
                                 s->ssao.blur.image.view,
                                 s->ssao.width, s->ssao.height,
                                 0, NULL);

      /* Blur SSAO pipeline */
      VkPushConstantRange pcb_blur_range;
      pcb_blur_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
      pcb_blur_range.offset = 0;
      pcb_blur_range.size = sizeof(SsaoBlurPCB);

      VkPushConstantRange pcb_blur_ranges[] = {
         pcb_blur_range,
      };

      s->ssao.blur.pipeline.ssao_tex_set_layout =
         vkdf_create_sampler_descriptor_set_layout(s->ctx, 0, 2,
                                                   VK_SHADER_STAGE_FRAGMENT_BIT);

      info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
      info.pNext = NULL;
      info.pushConstantRangeCount = 1;
      info.pPushConstantRanges = pcb_blur_ranges;
      info.setLayoutCount = 1;
      info.pSetLayouts = &s->ssao.blur.pipeline.ssao_tex_set_layout;
      info.flags = 0;

      VK_CHECK(vkCreatePipelineLayout(s->ctx->device, &info, NULL,
                                      &s->ssao.blur.pipeline.layout));

      s->ssao.blur.pipeline.shader.vs =
         vkdf_create_shader_module(s->ctx, SSAO_BLUR_VS_SHADER_PATH);

      VkPipelineShaderStageCreateInfo vs_info;
      vkdf_pipeline_fill_shader_stage_info(&vs_info,
                                           VK_SHADER_STAGE_VERTEX_BIT,
                                           s->ssao.blur.pipeline.shader.vs,
                                           NULL);

      s->ssao.blur.pipeline.shader.fs =
         vkdf_create_shader_module(s->ctx, SSAO_BLUR_FS_SHADER_PATH);

      VkPipelineShaderStageCreateInfo fs_info;
      VkSpecializationMapEntry entry = { 0, 0, sizeof(uint32_t) };
      VkSpecializationInfo fs_spec_info = {
         1,
         &entry,
         sizeof(uint32_t),
         &s->ssao.blur_size
      };
      vkdf_pipeline_fill_shader_stage_info(&fs_info,
                                           VK_SHADER_STAGE_FRAGMENT_BIT,
                                           s->ssao.blur.pipeline.shader.fs,
                                           &fs_spec_info);

      s->ssao.blur.pipeline.pipeline =
         vkdf_create_gfx_pipeline(s->ctx,
                                  NULL,
                                  0,
                                  NULL,
                                  0,
                                  NULL,
                                  false,
                                  VK_COMPARE_OP_ALWAYS,
                                  s->ssao.blur.rp.renderpass,
                                  s->ssao.blur.pipeline.layout,
                                  VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
                                  VK_CULL_MODE_BACK_BIT,
                                  1,
                                  &vs_info, &fs_info);

      /* Blur descriptor sets */
      s->ssao.blur.input_sampler =
         vkdf_ssao_create_ssao_sampler(s->ctx, VK_FILTER_NEAREST);

      s->ssao.blur.pipeline.ssao_tex_set =
         create_descriptor_set(s->ctx,
                               s->sampler.pool,
                               s->ssao.blur.pipeline.ssao_tex_set_layout);

      vkdf_descriptor_set_sampler_update(s->ctx,
                                         s->ssao.blur.pipeline.ssao_tex_set,
                                         s->ssao.blur.input_sampler,
                                         s->ssao.base.image.view,
                                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                         0, 1);

      vkdf_descriptor_set_sampler_update(s->ctx,
                                         s->ssao.blur.pipeline.ssao_tex_set,
                                         s->ssao.blur.input_sampler,
                                         s->rt.depth.view,
                                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                         1, 1);
   }

   /* Command buffer */
   s->ssao.cmd_buf = record_ssao_cmd_buf(s);
}

static void
prepare_scene_ssao(VkdfScene *s)
{
   assert(s->ssao.enabled);

   /* FIXME: We only support deferred for now */
   if (!s->rp.do_deferred) {
      vkdf_error("scene: SSAO is not supported in forward mode yet.");
      s->ssao.enabled = false;
      return;
   }

   /* SSAO requires depth information, which we assume is there if we
    * are in deferred mode, but in forward it requires depth-prepass to be
    * explicitly enabled.
    *
    * FIXME: Our SSAO implementation assumes that we have a Position texture
    *        in the Gbuffer from which we can retriev linear Z values. In the
    *        future we might want to make this more flexible and be able to
    *        reconstruct linear Z without demanding a Position texture at all.
    */
   if (!s->rp.do_deferred && !s->rp.do_depth_prepass) {
      vkdf_error("scene: cannot enable SSAO. Forward SSAO needs depth-prepass "
                 "which has not been enabled.");
      s->ssao.enabled = false;
      return;
   }

   /* SSAO samples UBO */
   vkdf_ssao_gen_tangent_samples(s->ssao.num_samples, &s->ssao.samples);

   s->ssao.samples_buf.size =
      ALIGN(sizeof(glm::vec3), 16) * s->ssao.num_samples;
   s->ssao.samples_buf.buf =
      vkdf_create_buffer(s->ctx, 0,
                         s->ssao.samples_buf.size,
                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

   uint8_t *mem;
   vkdf_memory_map(s->ctx, s->ssao.samples_buf.buf.mem,
                   0, VK_WHOLE_SIZE, (void **) &mem);

   const uint32_t sample_size = sizeof(s->ssao.samples[0]);
   for (uint32_t i = 0; i < s->ssao.num_samples; i++) {
      memcpy(mem, &s->ssao.samples[i], sample_size);
      mem += ALIGN(sample_size, 16);
   }

   vkdf_memory_unmap(s->ctx,
                     s->ssao.samples_buf.buf.mem,
                     s->ssao.samples_buf.buf.mem_props,
                     0, VK_WHOLE_SIZE);

   /* SSAO noise texture & sampler */
   vkdf_ssao_gen_noise_samples(s->ssao.num_noise_samples, &s->ssao.noise);

   vkdf_ssao_gen_noise_image(s->ctx,
                             s->cmd_buf.pool[0],
                             s->ssao.noise_image_dim,
                             s->ssao.noise_image_dim,
                             &s->ssao.noise,
                             &s->ssao.noise_image);

   s->ssao.noise_sampler = vkdf_ssao_create_noise_sampler(s->ctx);

   s->ssao.noise_scale = glm::vec2(s->ssao.width / s->ssao.noise_image_dim,
                                   s->ssao.height / s->ssao.noise_image_dim);

   /* Setup render passes, pipelines and command buffers */
   prepare_ssao_rendering(s);
}

struct HdrPCB {
   float exposure;
};

static void
record_hdr_cmd_buf(VkdfScene *s, VkCommandBuffer cmd_buf)
{
   VkImageSubresourceRange subresource_range =
      vkdf_create_image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT,
                                          0, 1, 0, 1);

   vkdf_image_set_layout(s->ctx,
                         cmd_buf,
                         s->hdr.input.image,
                         subresource_range,
                         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

   VkRenderPassBeginInfo rp_begin =
      vkdf_renderpass_begin_new(s->hdr.rp.renderpass,
                                s->hdr.rp.framebuffer,
                                0, 0, s->rt.width, s->rt.height,
                                0, NULL);

   vkCmdBeginRenderPass(cmd_buf,
                        &rp_begin,
                        VK_SUBPASS_CONTENTS_INLINE);

   record_viewport_and_scissor_commands(cmd_buf, s->rt.width, s->rt.height);

   vkCmdBindPipeline(cmd_buf,
                     VK_PIPELINE_BIND_POINT_GRAPHICS,
                     s->hdr.pipeline.pipeline);


   struct HdrPCB pcb;
   pcb.exposure = s->hdr.exposure;

   vkCmdPushConstants(cmd_buf,
                      s->hdr.pipeline.layout,
                      VK_SHADER_STAGE_FRAGMENT_BIT,
                      0, sizeof(struct HdrPCB), &pcb);

   VkDescriptorSet descriptor_sets[] = {
      s->hdr.pipeline.input_set,
   };

   vkCmdBindDescriptorSets(cmd_buf,
                           VK_PIPELINE_BIND_POINT_GRAPHICS,
                           s->hdr.pipeline.layout,
                           0, 1, descriptor_sets,  // First, count, sets
                           0, NULL);               // Dynamic offsets

   vkCmdDraw(cmd_buf, 4, 1, 0, 0);

   vkCmdEndRenderPass(cmd_buf);
}

static VkdfImage
prepare_hdr(VkdfScene *s, VkCommandBuffer cmd_buf, const VkdfImage *input)
{
   assert(s->hdr.enabled);

   /* Output image (tone mapping output) */
   s->hdr.output = create_color_framebuffer_image(s, false);

   /* Render pass */
   s->hdr.rp.renderpass =
      vkdf_renderpass_simple_new(s->ctx,
                                 s->hdr.output.format,
                                 VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                 VK_ATTACHMENT_STORE_OP_STORE,
                                 VK_IMAGE_LAYOUT_UNDEFINED,
                                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                 VK_FORMAT_UNDEFINED,
                                 VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                 VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                 VK_IMAGE_LAYOUT_UNDEFINED,
                                 VK_IMAGE_LAYOUT_UNDEFINED);

   /* Framebuffer */
   s->hdr.rp.framebuffer =
      vkdf_create_framebuffer(s->ctx,
                              s->hdr.rp.renderpass,
                              s->hdr.output.view,
                              s->rt.width, s->rt.height,
                              0, NULL);


   /* Pipeline */
   VkPushConstantRange pcb_range;
   pcb_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
   pcb_range.offset = 0;
   pcb_range.size = sizeof(struct HdrPCB);

   VkPushConstantRange pcb_ranges[] = {
      pcb_range,
   };

   s->hdr.pipeline.input_set_layout =
      vkdf_create_sampler_descriptor_set_layout(s->ctx, 0, 1,
                                                VK_SHADER_STAGE_FRAGMENT_BIT);

   VkDescriptorSetLayout layouts[] = {
      s->hdr.pipeline.input_set_layout,
   };

   VkPipelineLayoutCreateInfo info;
   info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
   info.pNext = NULL;
   info.pushConstantRangeCount = 1;
   info.pPushConstantRanges = pcb_ranges;
   info.setLayoutCount = 1;
   info.pSetLayouts = layouts;
   info.flags = 0;

   VK_CHECK(vkCreatePipelineLayout(s->ctx->device, &info, NULL,
                                   &s->hdr.pipeline.layout));

   s->hdr.pipeline.shader.vs =
      vkdf_create_shader_module(s->ctx, TONE_MAP_VS_SHADER_PATH);

   s->hdr.pipeline.shader.fs =
      vkdf_create_shader_module(s->ctx, TONE_MAP_FS_SHADER_PATH);

   s->hdr.pipeline.pipeline =
      vkdf_create_gfx_pipeline(s->ctx,
                               NULL,
                               0,
                               NULL,
                               0,
                               NULL,
                               false,
                               VK_COMPARE_OP_ALWAYS,
                               s->hdr.rp.renderpass,
                               s->hdr.pipeline.layout,
                               VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
                               VK_CULL_MODE_BACK_BIT,
                               1,
                               s->hdr.pipeline.shader.vs,
                               s->hdr.pipeline.shader.fs);

   /* Descriptor sets */
   s->hdr.input_sampler =
         vkdf_create_sampler(s->ctx,
                             VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                             VK_FILTER_NEAREST,
                             VK_SAMPLER_MIPMAP_MODE_NEAREST,
                             0.0f);

   s->hdr.pipeline.input_set =
      create_descriptor_set(s->ctx,
                            s->sampler.pool,
                            s->hdr.pipeline.input_set_layout);

   s->hdr.input = *input;
   vkdf_descriptor_set_sampler_update(s->ctx,
                                      s->hdr.pipeline.input_set,
                                      s->hdr.input_sampler,
                                      s->hdr.input.view,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                      0, 1);

   /* Command buffer */
   record_hdr_cmd_buf(s, cmd_buf);

   return s->hdr.output;
}

struct FxaaPCB {
   float luma_min;
   float luma_range_min;
   float subpx_aa;
};

static void
record_fxaa_cmd_buf(VkdfScene *s, VkCommandBuffer cmd_buf)
{
   VkImageSubresourceRange subresource_range =
      vkdf_create_image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT,
                                          0, 1, 0, 1);

   vkdf_image_set_layout(s->ctx,
                         cmd_buf,
                         s->fxaa.input.image,
                         subresource_range,
                         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

   VkRenderPassBeginInfo rp_begin =
      vkdf_renderpass_begin_new(s->fxaa.rp.renderpass,
                                s->fxaa.rp.framebuffer,
                                0, 0, s->rt.width, s->rt.height,
                                0, NULL);

   vkCmdBeginRenderPass(cmd_buf,
                        &rp_begin,
                        VK_SUBPASS_CONTENTS_INLINE);

   record_viewport_and_scissor_commands(cmd_buf, s->rt.width, s->rt.height);

   vkCmdBindPipeline(cmd_buf,
                     VK_PIPELINE_BIND_POINT_GRAPHICS,
                     s->fxaa.pipeline.pipeline);


   struct FxaaPCB pcb;
   pcb.luma_min = s->fxaa.luma_min;
   pcb.luma_range_min = s->fxaa.luma_range_min;
   pcb.subpx_aa = s->fxaa.subpx_aa;

   vkCmdPushConstants(cmd_buf,
                      s->fxaa.pipeline.layout,
                      VK_SHADER_STAGE_FRAGMENT_BIT,
                      0, sizeof(struct FxaaPCB), &pcb);

   VkDescriptorSet descriptor_sets[] = {
      s->fxaa.pipeline.input_set,
   };

   vkCmdBindDescriptorSets(cmd_buf,
                           VK_PIPELINE_BIND_POINT_GRAPHICS,
                           s->fxaa.pipeline.layout,
                           0, 1, descriptor_sets,  // First, count, sets
                           0, NULL);               // Dynamic offsets

   vkCmdDraw(cmd_buf, 4, 1, 0, 0);

   vkCmdEndRenderPass(cmd_buf);
}

static VkdfImage
prepare_fxaa(VkdfScene *s, VkCommandBuffer cmd_buf, const VkdfImage *input)
{
   assert(s->fxaa.enabled);

   /* Output image */
   s->fxaa.output = create_color_framebuffer_image(s, false);

   /* Render pass */
   s->fxaa.rp.renderpass =
      vkdf_renderpass_simple_new(s->ctx,
                                 s->fxaa.output.format,
                                 VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                 VK_ATTACHMENT_STORE_OP_STORE,
                                 VK_IMAGE_LAYOUT_UNDEFINED,
                                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                 VK_FORMAT_UNDEFINED,
                                 VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                 VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                 VK_IMAGE_LAYOUT_UNDEFINED,
                                 VK_IMAGE_LAYOUT_UNDEFINED);

   /* Framebuffer */
   s->fxaa.rp.framebuffer =
      vkdf_create_framebuffer(s->ctx,
                              s->fxaa.rp.renderpass,
                              s->fxaa.output.view,
                              s->rt.width, s->rt.height,
                              0, NULL);

   /* Pipeline */
   VkPushConstantRange pcb_range;
   pcb_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
   pcb_range.offset = 0;
   pcb_range.size = sizeof(struct FxaaPCB);

   VkPushConstantRange pcb_ranges[] = {
      pcb_range,
   };

   s->fxaa.pipeline.input_set_layout =
      vkdf_create_sampler_descriptor_set_layout(s->ctx, 0, 1,
                                                VK_SHADER_STAGE_FRAGMENT_BIT);

   VkDescriptorSetLayout layouts[] = {
      s->fxaa.pipeline.input_set_layout,
   };

   VkPipelineLayoutCreateInfo info;
   info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
   info.pNext = NULL;
   info.pushConstantRangeCount = 1;
   info.pPushConstantRanges = pcb_ranges;
   info.setLayoutCount = 1;
   info.pSetLayouts = layouts;
   info.flags = 0;

   VK_CHECK(vkCreatePipelineLayout(s->ctx->device, &info, NULL,
                                   &s->fxaa.pipeline.layout));

   s->fxaa.pipeline.shader.vs =
      vkdf_create_shader_module(s->ctx, FXAA_VS_SHADER_PATH);

   s->fxaa.pipeline.shader.fs =
      vkdf_create_shader_module(s->ctx, FXAA_FS_SHADER_PATH);

   s->fxaa.pipeline.pipeline =
      vkdf_create_gfx_pipeline(s->ctx,
                               NULL,
                               0,
                               NULL,
                               0,
                               NULL,
                               false,
                               VK_COMPARE_OP_ALWAYS,
                               s->fxaa.rp.renderpass,
                               s->fxaa.pipeline.layout,
                               VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
                               VK_CULL_MODE_BACK_BIT,
                               1,
                               s->fxaa.pipeline.shader.vs,
                               s->fxaa.pipeline.shader.fs);

   /* Descriptor sets */
   s->fxaa.input_sampler =
         vkdf_create_sampler(s->ctx,
                             VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                             VK_FILTER_LINEAR,
                             VK_SAMPLER_MIPMAP_MODE_NEAREST,
                             0.0f);

   s->fxaa.pipeline.input_set =
      create_descriptor_set(s->ctx,
                            s->sampler.pool,
                            s->fxaa.pipeline.input_set_layout);

   s->fxaa.input = *input;
   vkdf_descriptor_set_sampler_update(s->ctx,
                                      s->fxaa.pipeline.input_set,
                                      s->fxaa.input_sampler,
                                      s->fxaa.input.view,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                      0, 1);

   /* Command buffer */
   record_fxaa_cmd_buf(s, cmd_buf);

   return s->fxaa.output;
}

static void
prepare_post_processing_render_passes(VkdfScene *s)
{
   /* We record all the post-processing commands into a single
    * command buffer
    */
   VkCommandBuffer cmd_buf;

   vkdf_create_command_buffer(s->ctx,
                              s->cmd_buf.pool[0],
                              VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                              1, &cmd_buf);

   vkdf_command_buffer_begin(cmd_buf,
                             VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT);


   /* NOTE: Keep post-processing passes sorted in rendering order to keep
    * track of input and output images for each stage.
    */
   VkdfImage output = s->rt.color;

   bool has_post_processing = false;

   if (s->callbacks.postprocess) {
      has_post_processing = true;
      s->callbacks.postprocess(s->ctx, cmd_buf, s->callbacks.data);
      if (s->callbacks.postprocess_output)
         output = *s->callbacks.postprocess_output;
   }

   if (s->hdr.enabled) {
      has_post_processing = true;
      output = prepare_hdr(s, cmd_buf, &output);
   }

   if (s->fxaa.enabled) {
      has_post_processing = true;
      output = prepare_fxaa(s, cmd_buf, &output);
   }

   vkdf_command_buffer_end(cmd_buf);

   /* If we haven't recorded any post-processing passes into the
    * command buffer, free it
    */
   if (has_post_processing) {
      s->cmd_buf.postprocess = cmd_buf;
   } else {
      vkFreeCommandBuffers(s->ctx->device, s->cmd_buf.pool[0], 1, &cmd_buf);
   }

   /* We present from the output of the last post-processing pass */
   prepare_present_from_image(s, output);
}

static void
prepare_scene_render_passes(VkdfScene *s)
{
   if (s->rp.do_depth_prepass) {
      prepare_depth_prepass_render_passes(s);
   }

   if (s->ssao.enabled) {
      prepare_scene_ssao(s);
   }

   if (!s->rp.do_deferred) {
      prepare_forward_render_passes(s);
   } else {
      prepare_deferred_render_passes(s);
   }

   prepare_post_processing_render_passes(s);
}

static void
prepare_scene_gbuffer_merge_command_buffer(VkdfScene *s)
{
   assert(!s->cmd_buf.gbuffer_merge);

   VkCommandBuffer cmd_buf;

   vkdf_create_command_buffer(s->ctx,
                              s->cmd_buf.pool[0],
                              VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                              1, &cmd_buf);

   vkdf_command_buffer_begin(cmd_buf,
                             VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT);

   uint32_t num_clear_values;
   VkClearValue *clear_values;
   if (s->rp.do_color_clear) {
      num_clear_values = 1;
      clear_values = s->rp.clear_values;
   } else {
      num_clear_values = 0;
      clear_values = NULL;
   }

   VkRenderPassBeginInfo rp_begin =
      vkdf_renderpass_begin_new(s->rp.gbuffer_merge.renderpass,
                                s->rp.gbuffer_merge.framebuffer,
                                0, 0, s->rt.width, s->rt.height,
                                num_clear_values, clear_values);

   vkCmdBeginRenderPass(cmd_buf,
                        &rp_begin,
                        VK_SUBPASS_CONTENTS_INLINE);

   record_viewport_and_scissor_commands(cmd_buf, s->rt.width, s->rt.height);

   s->callbacks.gbuffer_merge(s->ctx, cmd_buf, s->callbacks.data);

   vkCmdEndRenderPass(cmd_buf);

   vkdf_command_buffer_end(cmd_buf);

   s->cmd_buf.gbuffer_merge = cmd_buf;
}

/**
 * Processess scene contents and sets things up for optimal rendering
 */
void
vkdf_scene_prepare(VkdfScene *s)
{
   prepare_render_target(s);
   prepare_scene_objects(s);
   prepare_scene_lights(s);
   prepare_scene_render_passes(s);
}

static void
record_dynamic_objects_command_buffer(VkdfScene *s,
                                      VkCommandBuffer cmd_buf,
                                      VkRenderPassBeginInfo *rp_begin)
{
   vkdf_command_buffer_begin(cmd_buf,
                             VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

   vkCmdBeginRenderPass(cmd_buf, rp_begin, VK_SUBPASS_CONTENTS_INLINE);

   record_viewport_and_scissor_commands(cmd_buf, s->rt.width, s->rt.height);

   const bool is_depth_prepass =
      rp_begin->renderPass == s->rp.dpp_dynamic_geom.renderpass;
   s->callbacks.record_commands(s->ctx, cmd_buf, s->dynamic.visible,
                                true, is_depth_prepass, s->callbacks.data);

   vkCmdEndRenderPass(cmd_buf);

   vkdf_command_buffer_end(cmd_buf);
}

static void
update_dirty_objects(VkdfScene *s)
{
   // Only need to do anything if we have dynamic objects
   if (s->obj_count == s->static_obj_count)
      return;

   const VkdfBox *cam_box = vkdf_camera_get_frustum_box(s->camera);
   const VkdfPlane *cam_planes = vkdf_camera_get_frustum_planes(s->camera);

   // Keep track of the number of visible dynamic objects in the scene so we
   // can compute start indices for each visible set in the UBO with the
   // dynamic object data
   s->dynamic.visible_obj_count = 0;
   s->dynamic.visible_shadow_caster_count = 0;

   // Go through all dynamic objects in the scene and update visible sets
   // and their material data
   uint8_t *obj_mem = (uint8_t *) s->dynamic.ubo.obj.host_buf;
   uint8_t *mat_mem = (uint8_t *) s->dynamic.ubo.material.host_buf;
   VkDeviceSize obj_offset = 0;
   VkDeviceSize mat_offset;

   uint32_t model_index = 0;
   char *id;
   VkdfSceneSetInfo *info;
   GHashTableIter set_iter;
   g_hash_table_iter_init(&set_iter, s->dynamic.sets);
   while (g_hash_table_iter_next(&set_iter, (void **)&id, (void **)&info)) {
      if (!info || info->count == 0)
         continue;

      // Reset visible information for this set
      VkdfSceneSetInfo *vis_info =
         (VkdfSceneSetInfo *) g_hash_table_lookup(s->dynamic.visible, id);
      if (!vis_info) {
         vis_info = g_new0(VkdfSceneSetInfo, 1);
         g_hash_table_replace(s->dynamic.visible, g_strdup(id), vis_info);
      } else if (vis_info->objs) {
         g_list_free(vis_info->objs);
         memset(vis_info, 0, sizeof(VkdfSceneSetInfo));
      }

      // Update visible objects for this set
      vis_info->start_index = s->dynamic.visible_obj_count;
      vis_info->shadow_caster_start_index =
         s->dynamic.visible_shadow_caster_count;

      GList *obj_iter = info->objs;
      while (obj_iter) {
         VkdfObject *obj = (VkdfObject *) obj_iter->data;

         // FIXME: Maybe we want to wrap objects into sceneobjects so we
         // can keep track of whether they are visible to the camera and the
         // lights and their slots in the UBOs. Then here and in other
         // similar updates, if the object is known to already be in the UBO
         // and in the same slot as we would put it now, we can skip
         // the memcpy's with the purpose of having the update command
         // start at an offset > 0.
         //
         // FIXME: The above would enable another optimization: we could
         // skip the frustum testing if we know that the object is not dirty
         // (or maybe more procisely, it has not moved) and the
         // camera is not dirty and the object was visible in the previous
         // frame.
         VkdfBox *obj_box = vkdf_object_get_box(obj);
         if (vkdf_box_is_in_frustum(obj_box, cam_box, cam_planes) != OUTSIDE) {
            // Update host buffer for UBO upload
            glm::mat4 model_matrix = vkdf_object_get_model_matrix(obj);

            // Model matrix
            memcpy(obj_mem + obj_offset,
                   &model_matrix[0][0], sizeof(glm::mat4));
            obj_offset += sizeof(glm::mat4);

            // Base material index
            memcpy(obj_mem + obj_offset,
                   &obj->material_idx_base, sizeof(uint32_t));
            obj_offset += sizeof(uint32_t);

            // Model index
            memcpy(obj_mem + obj_offset,
                   &model_index, sizeof(uint32_t));
            obj_offset += sizeof(uint32_t);

            // Receives shadows
            uint32_t receives_shadows = (uint32_t) obj->receives_shadows;
            memcpy(obj_mem + obj_offset,
                   &receives_shadows, sizeof(uint32_t));
            obj_offset += sizeof(uint32_t);

            obj_offset = ALIGN(obj_offset, 16);

            // Add the object to the viisble list and update visibility counters
            vis_info->objs = g_list_prepend(vis_info->objs, obj);
            vis_info->count++;
            if (vkdf_object_casts_shadows) {
               vis_info->shadow_caster_count++;
               s->dynamic.visible_shadow_caster_count++;
            }
            s->dynamic.visible_obj_count++;

            // This object is no longer dirty. Notice that we skip processing
            // updates for dirty objects that are not visible.
            vkdf_object_set_dirty(obj, false);
         }

         obj_iter = g_list_next(obj_iter);
      }

      // Update material data for this dynamic object set. We only need to
      // upload material data for dynamic objects once unless we have added
      // new set-ids or the materials have been updated (we don't really
      // support that for now)
      //
      // FIXME: support dirty materials for existing set-ids
      if (s->dynamic.materials_dirty) {
         VkdfModel *model = ((VkdfObject *) info->objs->data)->model;
         uint32_t material_size = ALIGN(sizeof(VkdfMaterial), 16);
         mat_offset = model_index * MAX_MATERIALS_PER_MODEL * material_size;
         uint32_t num_materials = model->materials.size();
         assert(num_materials <= MAX_MATERIALS_PER_MODEL);
         for (uint32_t mat_idx = 0; mat_idx < num_materials; mat_idx++) {
            VkdfMaterial *m = &model->materials[mat_idx];
            memcpy(mat_mem + mat_offset, m, material_size);
            mat_offset += material_size;
         }
      }

      model_index++;
   }

   // Record dynamic resource update command buffer for dynamic objects and
   // materials
   //
   // FIXME: Maybe we can skip this if we have an efficient way to know that
   // it has not changed from the previous frame ahead. For now,
   // we update every frame.
   if (s->dynamic.visible_obj_count > 0) {
      s->cmd_buf.have_resource_updates = true;

      /* We can only use VkCmdUpdateBuffer for small updates, but that should
       * be okay assuming that we won't have too many dynamic objects in a
       * scene (as in many hundreds of them).
       *
       * FIXME: vkCmdUpdateBuffer is not the most efficient thing to do, but
       * it has the advantage that the update won't happen until the command
       * buffer executes and we ensure it won't until it is dafe to update
       * the UBO. If we want to implement an alternative we will need to use
       * a ring a UBOs and command buffers so that we do buffer updates against
       * buffers that are not being accessed by commands in execution.
       */
      assert(obj_offset < 64 * 1024);
      vkCmdUpdateBuffer(s->cmd_buf.update_resources,
                        s->dynamic.ubo.obj.buf.buf,
                        0, obj_offset,
                        s->dynamic.ubo.obj.host_buf);

      if (s->dynamic.materials_dirty) {
         assert(mat_offset < 64 * 1024);
         vkCmdUpdateBuffer(s->cmd_buf.update_resources,
                           s->dynamic.ubo.material.buf.buf,
                           0, mat_offset,
                           s->dynamic.ubo.material.host_buf);
      }
   }

   // We have processed all new materials by now
   s->dynamic.materials_dirty = false;

   // Record dynamic object rendering command buffer
   if (s->cmd_buf.dynamic)
      new_inactive_cmd_buf(s, 0, s->cmd_buf.dynamic);
   if (s->cmd_buf.dpp_dynamic)
      new_inactive_cmd_buf(s, 0, s->cmd_buf.dpp_dynamic);

   if (s->dynamic.visible_obj_count > 0) {
      VkCommandBuffer cmd_buf[2];
      vkdf_create_command_buffer(s->ctx,
                                 s->cmd_buf.pool[0],
                                 VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                 s->rp.do_depth_prepass ? 2 : 1, cmd_buf);

      VkRenderPassBeginInfo rp_begin =
         vkdf_renderpass_begin_new(s->rp.dynamic_geom.renderpass,
                                   s->rp.dynamic_geom.framebuffer,
                                   0, 0, s->rt.width, s->rt.height,
                                   0, NULL);

      record_dynamic_objects_command_buffer(s, cmd_buf[0], &rp_begin);

      s->cmd_buf.dynamic = cmd_buf[0];

      if (s->rp.do_depth_prepass) {
         rp_begin =
            vkdf_renderpass_begin_new(s->rp.dpp_dynamic_geom.renderpass,
                                      s->rp.dpp_dynamic_geom.framebuffer,
                                      0, 0, s->rt.width, s->rt.height,
                                      0, NULL);

         record_dynamic_objects_command_buffer(s, cmd_buf[1], &rp_begin);

         s->cmd_buf.dpp_dynamic = cmd_buf[1];
      }
   } else {
      s->cmd_buf.dynamic = 0;
      s->cmd_buf.dpp_dynamic = 0;
   }
}

static void
thread_update_cmd_bufs(uint32_t thread_id, void *arg)
{
   struct TileThreadData *data = (struct TileThreadData *) arg;

   VkdfScene *s = data->s;

   const VkdfBox *visible_box = data->visible_box;
   const VkdfPlane *fplanes = data->fplanes;

   uint32_t first_idx = data->first_idx;
   uint32_t last_idx = data->last_idx;

   // Find visible tiles
   GList *prev_visible = data->visible;
   GList *cur_visible =
      find_visible_tiles(s, first_idx, last_idx, visible_box, fplanes);

   // Identify new invisible tiles
   data->cmd_buf_changes = false;
   GList *iter = prev_visible;
   while (iter) {
      VkdfSceneTile *t = (VkdfSceneTile *) iter->data;
      if (!g_list_find(cur_visible, t)) {
         new_inactive_tile(data, t);
         data->cmd_buf_changes = true;
      }
      iter = g_list_next(iter);
   }

   // Identify new visible tiles
   iter = cur_visible;
   while (iter) {
      VkdfSceneTile *t = (VkdfSceneTile *) iter->data;
      if (t->obj_count > 0 && !g_list_find(prev_visible, t)) {
         new_active_tile(data, t);
         data->cmd_buf_changes = true;
      }
      iter = g_list_next(iter);
   }

   // Attach the new list of visible tiles
   g_list_free(data->visible);
   data->visible = cur_visible;
}

static bool
update_cmd_bufs(VkdfScene *s)
{
   const VkdfBox *cam_box = vkdf_camera_get_frustum_box(s->camera);
   const VkdfPlane *cam_planes = vkdf_camera_get_frustum_planes(s->camera);

   for (uint32_t thread_idx = 0;
        thread_idx < s->thread.num_threads;
        thread_idx++) {
      s->thread.tile_data[thread_idx].visible_box = cam_box;
      s->thread.tile_data[thread_idx].fplanes = cam_planes;
      s->thread.tile_data[thread_idx].cmd_buf_changes = false;
   }

   if (s->thread.pool) {
      for (uint32_t thread_idx = 0;
           thread_idx < s->thread.num_threads;
           thread_idx++) {
         vkdf_thread_pool_add_job(s->thread.pool,
                                  thread_update_cmd_bufs,
                                  &s->thread.tile_data[thread_idx]);
      }
      vkdf_thread_pool_wait(s->thread.pool);
   } else {
      thread_update_cmd_bufs(0, &s->thread.tile_data[0]);
   }

   bool cmd_buf_changes = s->thread.tile_data[0].cmd_buf_changes;
   for (uint32_t thread_idx = 1;
        cmd_buf_changes == false && thread_idx < s->thread.num_threads;
        thread_idx++) {
      cmd_buf_changes = cmd_buf_changes ||
                        s->thread.tile_data[thread_idx].cmd_buf_changes;
   }

   return cmd_buf_changes;
}

static void
scene_update(VkdfScene *s)
{
   // Let the application update its state first
   if (s->callbacks.update_state)
      s->callbacks.update_state(s->callbacks.data);

   // Record the gbuffer merge command if needed
   if (s->rp.do_deferred && !s->cmd_buf.gbuffer_merge)
      prepare_scene_gbuffer_merge_command_buffer(s);

   // Check if any fences have been signaled and if so free any disposable
   // command buffers that were pending execution on signaled fences
   if (check_fences(s))
      free_inactive_command_buffers(s);

   // Start recording command buffer with resource updates for this frame
   start_recording_resource_updates(s);

   // Record resource updates from the application
   record_client_resource_updates(s);

   // Process scene element changes (this may also record resource updates)
   // We want to update dirty lights first so we can know if any dirty objects
   // are visible to them (since that means their shadow maps are dirty).
   update_dirty_lights(s);
   update_dirty_objects(s);

   // At this point we are done recording resource updates
   stop_recording_resource_updates(s);

   // If the camera didn't change, then our active tiles remain the same and
   // we don't need to re-record secondaries for them
   if (vkdf_camera_is_dirty(s->camera)) {
      bool cmd_buf_changes = update_cmd_bufs(s);

      if (!s->cmd_buf.primary[s->cmd_buf.cur_idx] || cmd_buf_changes) {
         build_primary_cmd_buf(s);
      }

      vkdf_camera_reset_dirty_state(s->camera);
   }
}

static void
scene_draw(VkdfScene *s)
{
   VkPipelineStageFlags wait_stage;
   uint32_t wait_sem_count;
   VkSemaphore *wait_sem;

   /* ========== Submit resource updates for the current frame ========== */

   // Since we always have to wait for the rendering to the render target
   // to finish before we submit the presentation job, we are certain that by
   // the time we get here, rendering to the render target for the previous
   // frame is completed and presentation for the previous frame might still
   // be ongoing. This means that we can safely submit command buffers that
   // do not render to the render target, such us any resource update.

   // If we have resource update commands, execute them first
   if (s->cmd_buf.have_resource_updates) {
      VkPipelineStageFlags resources_wait_stage = 0;
      vkdf_command_buffer_execute(s->ctx,
                                  s->cmd_buf.update_resources,
                                  &resources_wait_stage,
                                  0, NULL,
                                  1, &s->sync.update_resources_sem);

      wait_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
      wait_sem_count = 1;
      wait_sem = &s->sync.update_resources_sem;
   } else {
      wait_stage = 0;
      wait_sem_count = 0;
      wait_sem = NULL;
   }

   // If we have dirty shadow maps, update them.
   if (s->shadow_maps_dirty) {
      vkdf_command_buffer_execute(s->ctx,
                                  s->cmd_buf.shadow_maps,
                                  &wait_stage,
                                  wait_sem_count, wait_sem,
                                  1, &s->sync.shadow_maps_sem);

      wait_stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
      wait_sem_count = 1;
      wait_sem = &s->sync.shadow_maps_sem;
   }

   // Execute rendering command for the depth-prepass
   if (s->rp.do_depth_prepass) {
      if (!s->cmd_buf.dpp_dynamic) {
         vkdf_command_buffer_execute(s->ctx,
                                     s->cmd_buf.dpp_primary[s->cmd_buf.cur_idx],
                                     &wait_stage,
                                     wait_sem_count, wait_sem,
                                     1, &s->sync.depth_draw_sem);
      } else {
         vkdf_command_buffer_execute(s->ctx,
                                     s->cmd_buf.dpp_primary[s->cmd_buf.cur_idx],
                                     &wait_stage,
                                     wait_sem_count, wait_sem,
                                     1, &s->sync.depth_draw_static_sem);

         wait_stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
         wait_sem_count = 1;
         wait_sem = &s->sync.depth_draw_static_sem;

         vkdf_command_buffer_execute(s->ctx,
                                     s->cmd_buf.dpp_dynamic,
                                     &wait_stage,
                                     wait_sem_count, wait_sem,
                                     1, &s->sync.depth_draw_sem);
      }

      wait_stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
      wait_sem_count = 1;
      wait_sem = &s->sync.depth_draw_sem;
   }

   /* ========== Submit rendering jobs for the current frame ========== */

   // If we are still presenting the previous frame (actually, copying the
   // previous frame to the swapchain) we have to wait for that to finish
   // before rendering the new one. Otherwise we would probably corrupt the
   // copy of the previous frame to the swapchain.
   while (s->sync.present_fence_active) {
      VkResult status;
      do {
         status = vkWaitForFences(s->ctx->device,
                                  1, &s->sync.present_fence,
                                  true, 1000ull);
#if ENABLE_DEBUG && 0
         if (status == VK_NOT_READY || status == VK_TIMEOUT) {
            vkdf_info("debug: perf: scene: warning: "
                      "gpu busy, cpu stall before draw\n");
         }
#endif
      } while (status == VK_NOT_READY || status == VK_TIMEOUT);
      vkResetFences(s->ctx->device, 1, &s->sync.present_fence);
      s->sync.present_fence_active = false;
   }

   // Execute rendering commands for static and dynamic geometry
   if (!s->cmd_buf.dynamic) {
      vkdf_command_buffer_execute(s->ctx,
                                  s->cmd_buf.primary[s->cmd_buf.cur_idx],
                                  &wait_stage,
                                  wait_sem_count, wait_sem,
                                  1, &s->sync.draw_sem);
   } else {
      vkdf_command_buffer_execute(s->ctx,
                                  s->cmd_buf.primary[s->cmd_buf.cur_idx],
                                  &wait_stage,
                                  wait_sem_count, wait_sem,
                                  1, &s->sync.draw_static_sem);

      wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      wait_sem_count = 1;
      wait_sem = &s->sync.draw_static_sem;

      vkdf_command_buffer_execute(s->ctx,
                                  s->cmd_buf.dynamic,
                                  &wait_stage,
                                  wait_sem_count, wait_sem,
                                  1, &s->sync.draw_sem);
   }

   wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
   wait_sem_count = 1;
   wait_sem = &s->sync.draw_sem;

   if (s->rp.do_deferred) {
      // SSAO
      if (s->ssao.enabled) {
         vkdf_command_buffer_execute(s->ctx,
                                     s->ssao.cmd_buf,
                                     &wait_stage,
                                     wait_sem_count, wait_sem,
                                     1, &s->sync.ssao_sem);

         wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
         wait_sem_count = 1;
         wait_sem = &s->sync.ssao_sem;
      }

      // Deferred merge pass
      vkdf_command_buffer_execute(s->ctx,
                                  s->cmd_buf.gbuffer_merge,
                                  &wait_stage,
                                  wait_sem_count, wait_sem,
                                  1, &s->sync.gbuffer_merge_sem);

      wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      wait_sem_count = 1;
      wait_sem = &s->sync.gbuffer_merge_sem;
   }

   // Execute post-processing chain command buffer
   if (s->cmd_buf.postprocess) {
      vkdf_command_buffer_execute(s->ctx,
                                  s->cmd_buf.postprocess,
                                  &wait_stage,
                                  wait_sem_count, wait_sem,
                                  1, &s->sync.postprocess_sem);

      wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      wait_sem_count = 1;
      wait_sem = &s->sync.postprocess_sem;
   }

   /* ========== Copy rendering result to swapchain ========== */

   assert(wait_sem_count == 1);

   vkdf_copy_to_swapchain(s->ctx,
                          s->cmd_buf.present,
                          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                          *wait_sem,
                          s->sync.present_fence);

   s->sync.present_fence_active = true;
   free_inactive_command_buffers(s);
}

static inline void
event_loop_update(VkdfContext *ctx, void *data)
{
   VkdfScene *s = (VkdfScene *) data;
   scene_update(s);
}

static inline void
event_loop_render(VkdfContext *ctx, void *data)
{
   VkdfScene *s = (VkdfScene *) data;
   scene_draw(s);
}

void
vkdf_scene_event_loop_run(VkdfScene *s)
{
  vkdf_event_loop_run(s->ctx,
                      event_loop_update,
                      event_loop_render,
                      s);
}
