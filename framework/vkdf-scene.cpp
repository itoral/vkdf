#include "vkdf.hpp"

#define JOIN(a,b) (a b)
#define SHADOW_MAP_SHADER_PATH JOIN(VKDF_DATA_DIR, "spirv/shadow-map.vert.spv")

static const uint32_t MAX_MATERIALS_PER_MODEL =   32;
static const uint32_t MAX_DYNAMIC_OBJECTS     = 1024;
static const uint32_t MAX_DYNAMIC_MODELS      =  128;
static const uint32_t MAX_DYNAMIC_MATERIALS   =  MAX_DYNAMIC_MODELS * MAX_MATERIALS_PER_MODEL;

struct FreeCmdBufInfo {
   VkCommandBuffer cmd_buf;
   VkdfSceneTile *tile;
};

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
create_render_target(VkdfScene *s,
                     uint32_t width,
                     uint32_t height,
                     VkFormat color_format)
{
   s->rt.width = width;
   s->rt.height = height;

   s->rt.color =
      vkdf_create_image(s->ctx,
                        s->rt.width,
                        s->rt.height,
                        1,
                        VK_IMAGE_TYPE_2D,
                        color_format,
                        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT,
                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                           VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        VK_IMAGE_ASPECT_COLOR_BIT,
                        VK_IMAGE_VIEW_TYPE_2D);

   s->rt.depth =
      vkdf_create_image(s->ctx,
                        s->rt.width,
                        s->rt.height,
                        1,
                        VK_IMAGE_TYPE_2D,
                        VK_FORMAT_D32_SFLOAT,
                        0,
                        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        VK_IMAGE_ASPECT_DEPTH_BIT,
                        VK_IMAGE_VIEW_TYPE_2D);
}

VkdfScene *
vkdf_scene_new(VkdfContext *ctx,
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
      s->cmd_buf.pool[thread_idx] = vkdf_create_gfx_command_pool(s->ctx, 0);
      s->cmd_buf.active[thread_idx] = NULL;
      s->cmd_buf.free[thread_idx] = NULL;
   }

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
   s->sync.draw_sem = vkdf_create_semaphore(s->ctx);
   s->sync.draw_static_sem = vkdf_create_semaphore(s->ctx);
   s->sync.postprocess_sem = vkdf_create_semaphore(s->ctx);
   s->sync.present_fence = vkdf_create_fence(s->ctx);

   s->ubo.static_pool =
      vkdf_create_descriptor_pool(s->ctx, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 8);

   s->dynamic.sets =
      g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
   s->dynamic.visible =
      g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

   create_render_target(s, s->ctx->width, s->ctx->height, s->ctx->surface_format);

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
destroy_light(VkdfScene *s, VkdfSceneLight *slight)
{
   vkdf_light_free(slight->light);
   if (slight->shadow.shadow_map.image)
      vkdf_destroy_image(s->ctx, &slight->shadow.shadow_map);
   if (slight->shadow.visible)
      g_list_free(slight->shadow.visible);
   if (slight->shadow.cmd_buf)
      vkFreeCommandBuffers(s->ctx->device,
                           s->cmd_buf.pool[slight->shadow.thread_id],
                           1, &slight->shadow.cmd_buf);
   if (slight->shadow.framebuffer)
      vkDestroyFramebuffer(s->ctx->device, slight->shadow.framebuffer, NULL);
   if (slight->shadow.sampler)
      vkDestroySampler(s->ctx->device, slight->shadow.sampler, NULL);
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

   vkdf_destroy_image(s->ctx, &s->rt.color);
   vkdf_destroy_image(s->ctx, &s->rt.depth);

   vkDestroyRenderPass(s->ctx->device, s->rp.static_geom.renderpass, NULL);
   vkDestroyRenderPass(s->ctx->device, s->rp.dynamic_geom.renderpass, NULL);

   vkDestroyFramebuffer(s->ctx->device, s->rp.static_geom.framebuffer, NULL);
   vkDestroyFramebuffer(s->ctx->device, s->rp.dynamic_geom.framebuffer, NULL);

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
   vkDestroySemaphore(s->ctx->device, s->sync.draw_sem, NULL);
   vkDestroySemaphore(s->ctx->device, s->sync.draw_static_sem, NULL);
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

   // FIXME: have a list of buffers in the scene so that here we can just go
   // through the list and destory all of them without having to add another
   // deleter every time we start using a new buffer.
   if (s->ubo.obj.buf.buf) {
      vkDestroyBuffer(s->ctx->device, s->ubo.obj.buf.buf, NULL);
      vkFreeMemory(s->ctx->device, s->ubo.obj.buf.mem, NULL);
   }

   if (s->dynamic.ubo.obj.buf.buf) {
      vkDestroyBuffer(s->ctx->device, s->dynamic.ubo.obj.buf.buf, NULL);
      vkFreeMemory(s->ctx->device, s->dynamic.ubo.obj.buf.mem, NULL);
   }

   if (s->ubo.material.buf.buf) {
      vkDestroyBuffer(s->ctx->device, s->ubo.material.buf.buf, NULL);
      vkFreeMemory(s->ctx->device, s->ubo.material.buf.mem, NULL);
   }

   if (s->dynamic.ubo.material.buf.buf) {
      vkDestroyBuffer(s->ctx->device, s->dynamic.ubo.material.buf.buf, NULL);
      vkFreeMemory(s->ctx->device, s->dynamic.ubo.material.buf.mem, NULL);
   }

   if (s->ubo.light.buf.buf) {
      vkDestroyBuffer(s->ctx->device, s->ubo.light.buf.buf, NULL);
      vkFreeMemory(s->ctx->device, s->ubo.light.buf.mem, NULL);
   }

   if (s->ubo.shadow_map.buf.buf) {
      vkDestroyBuffer(s->ctx->device, s->ubo.shadow_map.buf.buf, NULL);
      vkFreeMemory(s->ctx->device, s->ubo.shadow_map.buf.mem, NULL);
   }

   if (s->dynamic.ubo.shadow_map.buf.buf) {
      vkDestroyBuffer(s->ctx->device, s->dynamic.ubo.shadow_map.buf.buf, NULL);
      vkFreeMemory(s->ctx->device, s->dynamic.ubo.shadow_map.buf.mem, NULL);
   }

   vkDestroyDescriptorPool(s->ctx->device, s->ubo.static_pool, NULL);

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
   s->dirty = true;
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
}

static inline VkdfImage
create_shadow_map_image(VkdfScene *s, uint32_t size)
{
   const VkImageUsageFlagBits shadow_map_usage =
   (VkImageUsageFlagBits) (VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                           VK_IMAGE_USAGE_SAMPLED_BIT);

   return vkdf_create_image(s->ctx,
                            size,
                            size,
                            1,
                            VK_IMAGE_TYPE_2D,
                            VK_FORMAT_D32_SFLOAT,
                            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT,
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

   glm::vec3 f[8];
   vkdf_compute_frustum_vertices(cam->pos, cam->rot,
                                 spec->shadow_map_near, spec->shadow_map_far,
                                 cam->proj.fov, cam->proj.aspect_ratio,
                                 f);

   VkdfBox box;
   vkdf_compute_frustum_clip_box(f, &box);

   float w = 2.0f * box.w;
   float h = 2.0f * box.h;
   float d = 2.0f * box.d;

   glm::mat4 proj(1.0f);
   proj[0][0] =  2.0f / w;
   proj[1][1] =  2.0f / h;
   proj[2][2] = -2.0f / d;
   proj[3][3] =  1.0f;

   sl->shadow.proj = clip * proj;
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
   glm::mat4 view = vkdf_light_get_view_matrix(sl->light);

   // The view matrix for directional lights considers a camera located at the
   // origin so we need to apply a translation
   if (vkdf_light_get_type(sl->light) == VKDF_LIGHT_DIRECTIONAL)
      view = glm::translate(view, -vkdf_camera_get_position(s->camera));

   sl->shadow.viewproj = sl->shadow.proj * view;
}

void
vkdf_scene_add_light(VkdfScene *s,
                     VkdfLight *light,
                     VkdfSceneShadowSpec *spec)
{
   assert(vkdf_light_casts_shadows(light) == (spec != NULL));

   VkdfSceneLight *slight = g_new0(VkdfSceneLight, 1);
   slight->light = light;
   if (light->casts_shadows) {
      assert(spec->pfc_kernel_size >= 1);
      slight->shadow.spec = *spec;
      slight->shadow.shadow_map =
         create_shadow_map_image(s, spec->shadow_map_size);
      slight->shadow.sampler =
         vkdf_create_shadow_sampler(s->ctx,
                             VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                             VK_FILTER_LINEAR,
                             VK_SAMPLER_MIPMAP_MODE_NEAREST);
      compute_light_projection(s, slight);
      s->has_shadow_caster_lights = true;
   }

   // Mark light as dirty so it gets into the lights UBO and we generate
   // the shadow map for it if needed
   if (vkdf_light_casts_shadows(light))
      vkdf_light_set_dirty_shadows(light, true);
   else
      vkdf_light_set_dirty(light, true);

   slight->frustum.dirty = true;

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

static VkCommandBuffer
build_primary_cmd_buf(VkdfScene *s)
{
   GList *active = sort_active_tiles_by_distance(s);

   VkCommandBuffer cmd_buf;

   vkdf_create_command_buffer(s->ctx,
                              s->cmd_buf.pool[0],
                              VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                              1, &cmd_buf);

   vkdf_command_buffer_begin(cmd_buf,
                             VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT);

   VkRenderPassBeginInfo rp_begin =
      vkdf_renderpass_begin_new(s->rp.static_geom.renderpass,
                                s->rp.static_geom.framebuffer,
                                0, 0, s->rt.width, s->rt.height,
                                2, s->rp.clear_values);

   vkCmdBeginRenderPass(cmd_buf,
                        &rp_begin,
                        VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);

   uint32_t cmd_buf_count = g_list_length(active);

   if (cmd_buf_count > 0) {
      VkCommandBuffer *cmd_bufs = g_new(VkCommandBuffer, cmd_buf_count);
      uint32_t idx = 0;
      GList *iter = active;
      while (iter) {
         VkdfSceneTile *t = (VkdfSceneTile *) iter->data;
         assert(t->cmd_buf != 0);
         cmd_bufs[idx++] = t->cmd_buf;
         iter = g_list_next(iter);
      }
      vkCmdExecuteCommands(cmd_buf, cmd_buf_count, cmd_bufs);
      g_free(cmd_bufs);
   }

   vkCmdEndRenderPass(cmd_buf);

   vkdf_command_buffer_end(cmd_buf);

   g_list_free(active);

   return cmd_buf;
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
         vkFreeCommandBuffers(s->ctx->device,
                              s->cmd_buf.pool[i], 1, &info->cmd_buf);

         // If this was a tile secondary, mark the tile as not having a command
         if (info->tile &&
             info->tile->cmd_buf == info->cmd_buf)
            info->tile->cmd_buf = 0;

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

   if (s->cache[job_id].size > 0) {
      GList *found = g_list_find(s->cache[job_id].cached, t);
      if (found) {
         remove_from_cache(data, t);
         s->cmd_buf.active[job_id] =
            g_list_prepend(s->cmd_buf.active[job_id], t);
         return;
      }
   }

   VkCommandBuffer cmd_buf;
   vkdf_create_command_buffer(s->ctx,
                              s->cmd_buf.pool[job_id],
                              VK_COMMAND_BUFFER_LEVEL_SECONDARY,
                              1, &cmd_buf);

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

   vkdf_command_buffer_begin_secondary(cmd_buf, flags, &inheritance_info);

   record_viewport_and_scissor_commands(cmd_buf, s->rt.width, s->rt.height);

   s->callbacks.record_commands(s->ctx, cmd_buf, t->sets, false,
                                s->callbacks.data);

   vkdf_command_buffer_end(cmd_buf);

   t->cmd_buf = cmd_buf;
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

   struct FreeCmdBufInfo *info = g_new(struct FreeCmdBufInfo, 1);
   info->cmd_buf = expired->cmd_buf;
   info->tile = expired;
   s->cmd_buf.free[job_id] =
      g_list_prepend(s->cmd_buf.free[job_id], info);
}

static void inline
new_inactive_cmd_buf(VkdfScene *s, uint32_t thread_id, VkCommandBuffer cmd_buf)
{
   struct FreeCmdBufInfo *info = g_new(struct FreeCmdBufInfo, 1);
   info->cmd_buf = cmd_buf;
   info->tile = NULL;
   s->cmd_buf.free[thread_id] =
      g_list_prepend(s->cmd_buf.free[thread_id], info);
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
frustum_contains_box(VkdfBox *box,
                     VkdfBox *frustum_box,
                     VkdfPlane *frustum_planes)
{
   if (!vkdf_box_collision(box, frustum_box))
      return OUTSIDE;

   return vkdf_box_is_in_frustum(box, frustum_planes);
}

static inline uint32_t
tile_is_visible(VkdfSceneTile *t, VkdfBox *visible_box, VkdfPlane *fp)
{
   if (t->obj_count == 0)
      return OUTSIDE;
   return frustum_contains_box(&t->box, visible_box, fp);
}

static inline uint32_t
subtile_is_visible(VkdfSceneTile *t, VkdfPlane *fp)
{
   if (t->obj_count == 0)
      return OUTSIDE;

   // We only check subtiles if the parent tile is inside the camera's box,
   // so no need to check if a subtile is inside it
   return vkdf_box_is_in_frustum(&t->box, fp);
}

static GList *
find_visible_subtiles(VkdfSceneTile *t, VkdfPlane *fplanes, GList *visible)
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
                   VkdfBox *visible_box,
                   VkdfPlane *fplanes)
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
   uint32_t pfc_kernel_size;
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

   const uint32_t
      inst_data_size = light_data_size + shadow_map_data_size;

   s->ubo.light.light_data_size = num_lights * light_data_size;
   s->ubo.light.shadow_map_data_size = num_lights * shadow_map_data_size;
   s->ubo.light.size = num_lights * inst_data_size;
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

static void
create_shadow_map_renderpass(VkdfScene *s)
{
   VkAttachmentDescription attachments[2];

   // Single depth attachment
   attachments[0].format = VK_FORMAT_D32_SFLOAT;
   attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
   attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
   attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
   attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
   attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
   attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
   attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
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

   VK_CHECK(vkCreateRenderPass(s->ctx->device, &rp_info, NULL,
                               &s->shadows.renderpass));
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

   VkPipelineVertexInputStateCreateInfo vi;
   VkVertexInputBindingDescription vi_binding[1];
   VkVertexInputAttributeDescription vi_attribs[1];
   vi_binding[0].binding = 0;
   vi_binding[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
   vi_binding[0].stride = vertex_data_stride;
   vi_attribs[0].binding = 0;
   vi_attribs[0].location = 0;
   vi_attribs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
   vi_attribs[0].offset = 0;
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

   s->shadows.pipeline.models_set =
      create_descriptor_set(s->ctx, s->ubo.static_pool,
                            s->shadows.pipeline.models_set_layout);

   s->shadows.pipeline.dyn_models_set =
      create_descriptor_set(s->ctx, s->ubo.static_pool,
                            s->shadows.pipeline.models_set_layout);

   VkDeviceSize ubo_offset = 0;
   VkDeviceSize ubo_size = s->ubo.shadow_map.size;
   vkdf_descriptor_set_buffer_update(s->ctx,
                                     s->shadows.pipeline.models_set,
                                     s->ubo.shadow_map.buf.buf,
                                     0, 1, &ubo_offset, &ubo_size, false, true);

   ubo_offset = 0;
   ubo_size = s->dynamic.ubo.shadow_map.size;
   vkdf_descriptor_set_buffer_update(s->ctx,
                                     s->shadows.pipeline.dyn_models_set,
                                     s->dynamic.ubo.shadow_map.buf.buf,
                                     0, 1, &ubo_offset, &ubo_size, false, true);

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

static void
create_shadow_map_framebuffer(VkdfScene *s, VkdfSceneLight *sl)
{
   VkFramebufferCreateInfo fb_info;
   fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
   fb_info.pNext = NULL;
   fb_info.renderPass = s->shadows.renderpass;
   fb_info.attachmentCount = 1;
   fb_info.pAttachments = &sl->shadow.shadow_map.view;
   fb_info.width = sl->shadow.spec.shadow_map_size;
   fb_info.height = sl->shadow.spec.shadow_map_size;
   fb_info.layers = 1;
   fb_info.flags = 0;

   VK_CHECK(vkCreateFramebuffer(s->ctx->device, &fb_info, NULL,
                                &sl->shadow.framebuffer));
}

// FIXME: we should have a frustum object, then we could put these functions
// there and share the logic between the lights, the camera, etc instead
// of replicating camera code here.
static void
scene_light_compute_frustum(VkdfScene *s, VkdfSceneLight *sl)
{
   // FIXME: support point lights
   assert(vkdf_light_get_type(sl->light) != VKDF_LIGHT_POINT);

   if (vkdf_light_get_type(sl->light) == VKDF_LIGHT_SPOTLIGHT) {
      float aperture_angle =
         RAD_TO_DEG(vkdf_light_get_aperture_angle(sl->light));

      vkdf_compute_frustum_vertices(
         vkdf_light_get_position(sl->light),
         vkdf_light_get_rotation(sl->light),
         sl->shadow.spec.shadow_map_near, sl->shadow.spec.shadow_map_far,
         aperture_angle,
         1.0f,
         sl->frustum.vertices);
   } else if (vkdf_light_get_type(sl->light) == VKDF_LIGHT_DIRECTIONAL) {
      vkdf_compute_frustum_vertices(vkdf_camera_get_position(s->camera),
                                    vkdf_camera_get_rotation(s->camera),
                                    sl->shadow.spec.shadow_map_near,
                                    sl->shadow.spec.shadow_map_far,
                                    s->camera->proj.fov,
                                    s->camera->proj.aspect_ratio,
                                    sl->frustum.vertices);
   }

   vkdf_compute_frustum_clip_box(sl->frustum.vertices, &sl->frustum.box);
   vkdf_compute_frustum_planes(sl->frustum.vertices, sl->frustum.planes);
   sl->frustum.dirty = false;
}

VkdfBox *
scene_light_get_frustum_box(VkdfScene *s, VkdfSceneLight *sl)
{
   if (sl->frustum.dirty)
      scene_light_compute_frustum(s, sl);

   return &sl->frustum.box;
}

glm::vec3 *
scene_light_get_frustum_vertices(VkdfScene *s, VkdfSceneLight *sl)
{
   if (sl->frustum.dirty)
      scene_light_compute_frustum(s, sl);

   return sl->frustum.vertices;
}

VkdfPlane *
scene_light_get_frustum_planes(VkdfScene *s, VkdfSceneLight *sl)
{
   if (sl->frustum.dirty)
      scene_light_compute_frustum(s, sl);

   return sl->frustum.planes;
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
   VkdfBox *frustum_box = scene_light_get_frustum_box(s, sl);
   VkdfPlane *frustum_planes = scene_light_get_frustum_planes(s, sl);

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

static void
record_shadow_map_cmd_buf(VkdfScene *s,
                          VkdfSceneLight *sl,
                          GHashTable *dyn_sets,
                          uint32_t thread_id)
{
   assert(sl->shadow.shadow_map.image);

   // FIXME: support point lights
   assert(vkdf_light_get_type(sl->light) != VKDF_LIGHT_POINT);

   assert(thread_id < s->thread.num_threads);
   sl->shadow.thread_id = thread_id;
   vkdf_create_command_buffer(s->ctx,
                              s->cmd_buf.pool[thread_id],
                              VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                              1, &sl->shadow.cmd_buf);

   vkdf_command_buffer_begin(sl->shadow.cmd_buf,
                             VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

   VkClearValue clear_values[1];
   clear_values[0].depthStencil.depth = 1.0f;
   clear_values[0].depthStencil.stencil = 0;

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

   vkCmdBeginRenderPass(sl->shadow.cmd_buf,
                        &rp_begin,
                        VK_SUBPASS_CONTENTS_INLINE);

   // Dynamic viewport / scissor / depth bias
   record_viewport_and_scissor_commands(sl->shadow.cmd_buf,
                                        shadow_map_size,
                                        shadow_map_size);

   vkCmdSetDepthBias(sl->shadow.cmd_buf,
                     sl->shadow.spec.depth_bias_const_factor,
                     0.0f,
                     sl->shadow.spec.depth_bias_slope_factor);

   // Push constants (Light View/projection)
   vkCmdPushConstants(sl->shadow.cmd_buf,
                      s->shadows.pipeline.layout,
                      VK_SHADER_STAGE_VERTEX_BIT,
                      0, sizeof(_shadow_map_pcb), &sl->shadow.viewproj[0][0]);

   // Descriptor sets (UBO with object model matrices)
   vkCmdBindDescriptorSets(sl->shadow.cmd_buf,
                           VK_PIPELINE_BIND_POINT_GRAPHICS,
                           s->shadows.pipeline.layout,
                           0,                               // First decriptor set
                           1,                               // Descriptor set count
                           &s->shadows.pipeline.models_set, // Descriptor sets
                           0,                               // Dynamic offset count
                           NULL);                           // Dynamic offsets

   // Draw
   VkPipeline current_pipeline = 0;

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
                  vkCmdBindPipeline(sl->shadow.cmd_buf,
                                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipeline);
                  current_pipeline = pipeline;
               }

               // FIXME: should we make this a callback to the app so it can
               // have better control of what and how gets rendered to the
               // shadow map?

               // Draw all instances
               const VkDeviceSize offsets[1] = { 0 };
               vkCmdBindVertexBuffers(sl->shadow.cmd_buf,
                                      0,                       // Start Binding
                                      1,                       // Binding Count
                                      &mesh->vertex_buf.buf,   // Buffers
                                      offsets);                // Offsets

               if (!mesh->index_buf.buf) {
                  vkCmdDraw(sl->shadow.cmd_buf,
                            mesh->vertices.size(),                // vertex count
                            set_info->shadow_caster_count,        // instance count
                            0,                                    // first vertex
                            set_info->shadow_caster_start_index); // first instance
               } else {
                  vkCmdBindIndexBuffer(sl->shadow.cmd_buf,
                                       mesh->index_buf.buf,       // Buffer
                                       0,                         // Offset
                                       VK_INDEX_TYPE_UINT32);     // Index type

                  vkCmdDrawIndexed(sl->shadow.cmd_buf,
                                   mesh->indices.size(),                 // index count
                                   set_info->shadow_caster_count,        // instance count
                                   0,                                    // first index
                                   0,                                    // first vertex
                                   set_info->shadow_caster_start_index); // first instance
               }
            }
         }
         set_iter = g_list_next(set_iter);
      }
      tile_iter = g_list_next(tile_iter);
   }

   // Render dynamic objects
   vkCmdBindDescriptorSets(sl->shadow.cmd_buf,
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
            vkCmdBindPipeline(sl->shadow.cmd_buf,
                              VK_PIPELINE_BIND_POINT_GRAPHICS,
                              pipeline);
            current_pipeline = pipeline;
         }

         // Draw all instances
         const VkDeviceSize offsets[1] = { 0 };
         vkCmdBindVertexBuffers(sl->shadow.cmd_buf,
                                0,                       // Start Binding
                                1,                       // Binding Count
                                &mesh->vertex_buf.buf,   // Buffers
                                offsets);                // Offsets

         if (!mesh->index_buf.buf) {
            vkCmdDraw(sl->shadow.cmd_buf,
                      mesh->vertices.size(),                // vertex count
                      set_info->shadow_caster_count,        // instance count
                      0,                                    // first vertex
                      set_info->shadow_caster_start_index); // first instance
         } else {
            vkCmdBindIndexBuffer(sl->shadow.cmd_buf,
                                 mesh->index_buf.buf,       // Buffer
                                 0,                         // Offset
                                 VK_INDEX_TYPE_UINT32);     // Index type

            vkCmdDrawIndexed(sl->shadow.cmd_buf,
                             mesh->indices.size(),                 // index count
                             set_info->shadow_caster_count,        // instance count
                             0,                                    // first index
                             0,                                    // first vertex
                             set_info->shadow_caster_start_index); // first instance
         }
      }
   }

   vkCmdEndRenderPass(sl->shadow.cmd_buf);

   vkdf_command_buffer_end(sl->shadow.cmd_buf);
}

static inline void
check_for_dirty_lights(VkdfScene *s)
{
   s->lights_dirty = false;
   s->shadow_maps_dirty = false;
   for (uint32_t i = 0;
        !(s->lights_dirty && s->shadow_maps_dirty) && i < s->lights.size();
        i++) {
      VkdfLight *l = s->lights[i]->light;
      if (vkdf_light_is_dirty(l))
         s->lights_dirty = true;
      if (vkdf_light_has_dirty_shadows(l))
         s->shadow_maps_dirty = true;
   }
}

static bool
record_scene_light_resource_updates(VkdfScene *s)
{
   if (!s->lights_dirty || !s->shadow_maps_dirty)
      return false;

   uint32_t num_lights = s->lights.size();

   // First we update dirty light descriptions
   //
   // FIXME: maybe a single update of the entire buffer is faster if we have
   // too many dirty lights
   VkDeviceSize light_inst_size = ALIGN(sizeof(VkdfLight), 16);
   if (s->lights_dirty) {
      for (uint32_t i = 0; i < num_lights; i++) {
         VkdfSceneLight *sl = s->lights[i];
         if (!vkdf_light_is_dirty(sl->light))
            continue;

         vkCmdUpdateBuffer(s->cmd_buf.update_resources,
                           s->ubo.light.buf.buf,
                           i * light_inst_size, light_inst_size,
                           sl->light);
      }
   }

   // Next we update dirty shadow mapping descriptions
   if (s->shadow_maps_dirty) {
      VkDeviceSize base_offset = num_lights * light_inst_size;
      VkDeviceSize shadow_map_inst_size =
         ALIGN(sizeof(struct _shadow_map_ubo_data), 16);
      for (uint32_t i = 0; i < num_lights; i++) {
         VkdfSceneLight *sl = s->lights[i];
         if (!vkdf_light_casts_shadows(sl->light))
            continue;
         if (!vkdf_light_has_dirty_shadows(sl->light))
            continue;

         struct _shadow_map_ubo_data data;
         memcpy(&data.light_viewproj[0][0],
                &sl->shadow.viewproj[0][0], sizeof(glm::mat4));
         memcpy(&data.shadow_map_size,
                &sl->shadow.spec.shadow_map_size, sizeof(uint32_t));
         memcpy(&data.pfc_kernel_size,
                &sl->shadow.spec.pfc_kernel_size, sizeof(uint32_t));

         vkCmdUpdateBuffer(s->cmd_buf.update_resources,
                           s->ubo.light.buf.buf,
                           base_offset + i * shadow_map_inst_size,
                           shadow_map_inst_size,
                           &data);
      }
   }

   s->cmd_buf.have_resource_updates = true;
   return true;
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

   VkdfBox *light_box = scene_light_get_frustum_box(s, sl);
   VkdfPlane *light_planes = scene_light_get_frustum_planes(s, sl);

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
            if (frustum_contains_box(obj_box, light_box, light_planes) != OUTSIDE) {
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
      struct _DirtyShadowMap *ds = (struct _DirtyShadowMap *) sm_iter->data;
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
      vkCmdUpdateBuffer(s->cmd_buf.update_resources,
                        s->dynamic.ubo.shadow_map.buf.buf,
                        0, offset, mem);

      s->cmd_buf.have_resource_updates = true;
   }

   return s->cmd_buf.have_resource_updates;
}

static inline void
free_dirty_shadow_maps(VkdfScene *s)
{
   if (!s->dirty_shadow_maps)
      return;

   GList *iter = s->dirty_shadow_maps;
   do {
      struct _DirtyShadowMap *ds = (struct _DirtyShadowMap *) iter->data;
      g_hash_table_foreach(ds->dyn_sets, destroy_set, NULL);
      g_hash_table_destroy(ds->dyn_sets);
      new_inactive_cmd_buf(s, ds->sl->shadow.thread_id, ds->sl->shadow.cmd_buf);
      ds->sl->shadow.cmd_buf = 0;
      g_free(ds);
      iter = g_list_next(iter);
   } while (iter);
   g_list_free(s->dirty_shadow_maps);
   s->dirty_shadow_maps = NULL;
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
   bool needs_new_shadow_map;
   if (vkdf_light_has_dirty_shadows(l)) {
      needs_new_shadow_map = true;
      compute_light_view_projection(s, sl);
      compute_visible_tiles_for_light(s, sl);
   }

   // Whether the area of influence has changed or not, we need to check if
   // we need to regen shadow maps due to dynamic objects anyway. If the
   // light has dynamic objects in its area of influence then we also need
   // an updated list of objects so we can render them to the shadow map
   bool has_dirty_objects;
   GHashTable *dyn_sets =
      find_dynamic_objects_for_light(s, sl, &has_dirty_objects);

   needs_new_shadow_map = needs_new_shadow_map || has_dirty_objects;

   if (needs_new_shadow_map) {
      record_shadow_map_cmd_buf(s, sl, dyn_sets, thread_id);
      data->dirty_shadow_map = g_new(struct _DirtyShadowMap, 1);
      data->dirty_shadow_map->sl = sl;
      data->dirty_shadow_map->dyn_sets = dyn_sets;
   }
}

static void
update_dirty_lights(VkdfScene *s)
{
   free_dirty_shadow_maps(s);

   if (s->lights.size() == 0)
      return;

   // Go through the list of lights and check if they are dirty and if they
   // require new shadow maps. If they require new shadow maps, record
   // the command buffers for them. We thread the shadow map checks per light.

   // If all lights are shadow casters then we can have as much that many
   // dirty shadow maps
   std::vector<struct LightThreadData> data;
   data.resize(s->lights.size());
   uint32_t data_count = 0;

   bool has_thread_jobs = false;
   for (uint32_t i = 0; i < s->lights.size(); i++) {
      VkdfSceneLight *sl = s->lights[i];
      VkdfLight *l = sl->light;

      if (vkdf_light_is_dirty(l))
         s->lights_dirty = true;

      if (vkdf_light_has_dirty_shadows(l))
         sl->frustum.dirty = true;

      if (!vkdf_light_casts_shadows(l))
         continue;

      data[data_count].id = i;
      data[data_count].s = s;
      data[data_count].sl = sl;
      data[data_count].dirty_shadow_map = NULL;

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

   // Collect list of dirty shadow maps from threads
   if (has_thread_jobs)
      vkdf_thread_pool_wait(s->thread.pool);

   for (uint32_t i = 0; i < data_count; i++) {
      if (data[i].dirty_shadow_map == NULL)
         continue;

      s->shadow_maps_dirty = true;
      s->dirty_shadow_maps = g_list_prepend(s->dirty_shadow_maps,
                                            data[i].dirty_shadow_map);
   }

   // Record the commands to update scene light resources for rendering
   record_scene_light_resource_updates(s);

   // Record commands to update dynamic shadow map resources for each light
   record_scene_dynamic_shadow_map_resource_updates(s, s->dirty_shadow_maps);
}

/**
 * Prepares state and resources required by light sources:
 * - Prepares rendering resources for shadow maps
 */
static void
prepare_scene_lights(VkdfScene *s)
{
   check_for_dirty_lights(s);
   if (!s->shadow_maps_dirty)
      return;

   // Create shared rendering resources for shadow maps
   create_shadow_map_renderpass(s);
   create_shadow_map_pipelines(s);

   // Create per-light resources
   for (uint32_t i = 0; i < s->lights.size(); i++) {
      VkdfSceneLight *sl = s->lights[i];
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
      VkClearValue color_clear;
      color_clear.color.float32[0] = 0.0f;
      color_clear.color.float32[1] = 0.0f;
      color_clear.color.float32[2] = 0.0f;
      color_clear.color.float32[3] = 1.0f;
      s->rp.clear_values[0] = color_clear;
   }

   s->rp.clear_values[1] = *depth;
}

static void
prepare_scene_render_passes(VkdfScene *s)
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
                                 VK_ATTACHMENT_LOAD_OP_CLEAR,
                                 VK_ATTACHMENT_STORE_OP_STORE,
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

static void
prepare_scene_present_command_buffers(VkdfScene *s)
{
   s->cmd_buf.present =
      vkdf_command_buffer_create_for_present(s->ctx,
                                             s->cmd_buf.pool[0],
                                             s->rt.color.image);
}

/**
 * Processess scene contents and sets things up for optimal rendering
 */
void
vkdf_scene_prepare(VkdfScene *s)
{
   prepare_scene_objects(s);
   prepare_scene_lights(s);
   prepare_scene_render_passes(s);
   prepare_scene_present_command_buffers(s);
}

static void
update_dirty_objects(VkdfScene *s)
{
   // Only need to do anything if we have dynamic objects
   if (s->obj_count == s->static_obj_count)
      return;

   VkdfBox *cam_box = vkdf_camera_get_frustum_box(s->camera);
   VkdfPlane *cam_planes = vkdf_camera_get_frustum_planes(s->camera);

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
         if (frustum_contains_box(obj_box, cam_box, cam_planes) != OUTSIDE) {
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

      vkCmdUpdateBuffer(s->cmd_buf.update_resources,
                        s->dynamic.ubo.obj.buf.buf,
                        0, obj_offset,
                        s->dynamic.ubo.obj.host_buf);

      if (s->dynamic.materials_dirty) {
         vkCmdUpdateBuffer(s->cmd_buf.update_resources,
                           s->dynamic.ubo.material.buf.buf,
                           0, mat_offset,
                           s->dynamic.ubo.material.host_buf);
      }
   }

   // We have processed all new materials by now
   s->dynamic.materials_dirty = false;

   // Record dynamic object rendering command buffer
   if (s->cmd_buf.dynamic) {
      new_inactive_cmd_buf(s, 0, s->cmd_buf.dynamic);
   }
   if (s->dynamic.visible_obj_count > 0) {
      VkCommandBuffer cmd_buf;
      vkdf_create_command_buffer(s->ctx,
                                 s->cmd_buf.pool[0],
                                 VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                 1, &cmd_buf);

      vkdf_command_buffer_begin(cmd_buf,
                                VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

      VkRenderPassBeginInfo rp_begin =
         vkdf_renderpass_begin_new(s->rp.dynamic_geom.renderpass,
                                   s->rp.dynamic_geom.framebuffer,
                                   0, 0, s->rt.width, s->rt.height,
                                   0, NULL);

      vkCmdBeginRenderPass(cmd_buf, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

      record_viewport_and_scissor_commands(cmd_buf, s->rt.width, s->rt.height);

      s->callbacks.record_commands(s->ctx, cmd_buf, s->dynamic.visible, true,
                                   s->callbacks.data);

      vkCmdEndRenderPass(cmd_buf);

      vkdf_command_buffer_end(cmd_buf);

      s->cmd_buf.dynamic = cmd_buf;
   } else {
      s->cmd_buf.dynamic = 0;
   }
}

static void
thread_update_cmd_bufs(uint32_t thread_id, void *arg)
{
   struct TileThreadData *data = (struct TileThreadData *) arg;

   VkdfScene *s = data->s;

   VkdfBox *visible_box = data->visible_box;
   VkdfPlane *fplanes = data->fplanes;

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
   VkdfBox *cam_box = vkdf_camera_get_frustum_box(s->camera);
   VkdfPlane *cam_planes = vkdf_camera_get_frustum_planes(s->camera);

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

void
vkdf_scene_update_cmd_bufs(VkdfScene *s)
{
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

      if (!s->cmd_buf.primary || cmd_buf_changes) {
         if (s->cmd_buf.primary)
            new_inactive_cmd_buf(s, 0, s->cmd_buf.primary);
         s->cmd_buf.primary = build_primary_cmd_buf(s);
      }
   }
}

VkSemaphore
vkdf_scene_draw(VkdfScene *s)
{
   VkPipelineStageFlags wait_stage;
   uint32_t wait_sem_count;
   VkSemaphore *wait_sem;

   /* ========== Submit resource updates for the current frame ========== */

   // Before we do this, we need to make sure that the gpu isn't still
   // accessing these resources from any shaders to render the previous frame.
   // We wait on the render semaphore for this, which points to the last
   // render job that we have submitted.

   // If we have resource update commands, execute them first
   wait_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
   wait_sem_count = s->sync.render_sem_active ? 1 : 0;
   wait_sem = &s->sync.render_sem;

   if (s->cmd_buf.have_resource_updates) {
      vkdf_command_buffer_execute(s->ctx,
                                  s->cmd_buf.update_resources,
                                  &wait_stage,
                                  wait_sem_count, wait_sem,
                                  1, &s->sync.update_resources_sem);

      wait_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
      wait_sem_count = 1;
      wait_sem = &s->sync.update_resources_sem;
   }

   // If we have dirty shadow maps, update them.
   //
   // This is the last stage for handling dirty lights in the scene, so we
   // mark all of them them as not being dirty after this step.
   if (s->dirty_shadow_maps) {
      assert(s->shadow_maps_dirty == true);
      VkCommandBuffer *cmd_bufs = g_new(VkCommandBuffer, s->lights.size());
      uint32_t count = 0;
      GList *iter = s->dirty_shadow_maps;
      while (iter) {
         struct _DirtyShadowMap *ds = (_DirtyShadowMap *) iter->data;
         VkdfSceneLight *sl = ds->sl;

         assert(vkdf_light_casts_shadows(sl->light));
         assert(sl->shadow.cmd_buf != 0);

         cmd_bufs[count++] = sl->shadow.cmd_buf;

         vkdf_light_set_dirty_shadows(sl->light, false);
         vkdf_light_set_dirty(sl->light, false);

         iter = g_list_next(iter);
      }

      assert(count > 0);
      vkdf_command_buffer_execute_many(s->ctx,
                                       cmd_bufs,
                                       count,
                                       &wait_stage,
                                       wait_sem_count, wait_sem,
                                       1, &s->sync.shadow_maps_sem);

      g_free(cmd_bufs);

      wait_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
      wait_sem_count = 1;
      wait_sem = &s->sync.shadow_maps_sem;
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
#if ENABLE_DEBUG
         if (status == VK_NOT_READY || status == VK_TIMEOUT) {
            vkdf_info("debug: perf: scene: warning: stall before rendering. "
                      "Reason: gpu busy copying previous frame to swapchain \n");
         }
#endif
      } while (status == VK_NOT_READY || status == VK_TIMEOUT);
      vkResetFences(s->ctx->device, 1, &s->sync.present_fence);
      s->sync.present_fence_active = false;
   }

   // Execute rendering commands for static and dynamic geometry
   if (!s->cmd_buf.dynamic) {
      vkdf_command_buffer_execute(s->ctx,
                                  s->cmd_buf.primary,
                                  &wait_stage,
                                  wait_sem_count, wait_sem,
                                  1, &s->sync.draw_sem);
   } else {
      vkdf_command_buffer_execute(s->ctx,
                                  s->cmd_buf.primary,
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

   // Execute postprocess callback
   if (s->callbacks.postprocess) {
      assert(wait_sem_count == 1);
      s->callbacks.postprocess(s->ctx,
                               *wait_sem,
                               s->sync.postprocess_sem,
                               s->callbacks.data);

      wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      wait_sem_count = 1;
      wait_sem = &s->sync.postprocess_sem;
   }

   // Keep track of the last render job so we can sync commands in the
   // next frame that depend on render target rendering to be completed
   assert(wait_sem_count == 1);
   s->sync.render_sem = *wait_sem;
   s->sync.render_sem_active = true;

   /* ========== Copy rendering result to swapchain ========== */

   assert(wait_sem_count == 1);
   vkdf_copy_to_swapchain(s->ctx,
                          s->cmd_buf.present,
                          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                          *wait_sem,
                          s->sync.present_fence);

   s->sync.present_fence_active = true;
   free_inactive_command_buffers(s);

   return s->sync.draw_sem;
}
