#include "vkdf.hpp"

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

   s->thread.data = g_new0(struct ThreadData, num_threads);
   for (uint32_t thread_idx = 0; thread_idx < num_threads; thread_idx++) {
      s->thread.data[thread_idx].id = thread_idx;
      s->thread.data[thread_idx].s = s;
      s->thread.data[thread_idx].first_idx =
         thread_idx * s->thread.work_size;
      s->thread.data[thread_idx].last_idx =
         (thread_idx < num_threads - 1) ?
            s->thread.data[thread_idx].first_idx + s->thread.work_size - 1 :
            s->num_tiles.total - 1;
   }

   s->sync.update_resources_sem = vkdf_create_semaphore(s->ctx);
   s->sync.draw_sem = vkdf_create_semaphore(s->ctx);
   s->sync.draw_fence = vkdf_create_fence(s->ctx);

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
destroy_light(VkdfContext *ctx, VkdfSceneLight *slight)
{
   vkdf_light_free(slight->light);
   if (slight->has_shadow_map)
      vkdf_destroy_image(ctx, &slight->shadow_map);
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

void
vkdf_scene_free(VkdfScene *s)
{
   while (s->sync.draw_fence_active) {
      VkResult status;
      do {
         status = vkWaitForFences(s->ctx->device,
                                  1, &s->sync.draw_fence,
                                  true, 1000ull);
      } while (status == VK_NOT_READY || status == VK_TIMEOUT);
      vkResetFences(s->ctx->device, 1, &s->sync.draw_fence);
      s->sync.draw_fence_active = false;
   }

   if (s->thread.pool) {
      vkdf_thread_pool_wait(s->thread.pool);
      g_free(s->thread.data);
      vkdf_thread_pool_free(s->thread.pool);
   }

   g_list_free_full(s->set_ids, g_free);
   s->set_ids = NULL;

   for (uint32_t i = 0; i < s->num_tiles.total; i++)
      free_tile(&s->tiles[i]);
   g_free(s->tiles);

   for (uint32_t i = 0; i < s->lights.size(); i++)
      destroy_light(s->ctx, s->lights[i]);
   s->lights.clear();
   std::vector<VkdfSceneLight *>(s->lights).swap(s->lights);

   vkDestroySemaphore(s->ctx->device, s->sync.update_resources_sem, NULL);
   vkDestroySemaphore(s->ctx->device, s->sync.draw_sem, NULL);
   vkDestroyFence(s->ctx->device, s->sync.draw_fence, NULL);

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

   g_free(s->tile_size);

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

void
vkdf_scene_add_object(VkdfScene *s, const char *set_id, VkdfObject *obj)
{
   // FIXME: we don't support dynamic objects yet
   assert(vkdf_object_is_dynamic(obj) == false);

   if (!set_id_is_registered(s, set_id))
      s->set_ids = g_list_prepend(s->set_ids, g_strdup(set_id));

   // Find tile this object belongs to
   glm::vec3 tile_coord = tile_coord_from_position(s, obj->pos);

   uint32_t ti =
      tile_index_from_tile_coords(s,
                                  (uint32_t) tile_coord.x,
                                  (uint32_t) tile_coord.y,
                                  (uint32_t) tile_coord.z);
   VkdfSceneTile *tile = &s->tiles[ti];

   tile->obj_count++;
   tile->dirty = true;

   // Update the tile's box to fit this object
   vkdf_object_compute_box(obj);
   VkdfBox *box = vkdf_object_get_box(obj);
   glm::vec3 min_box = box->center - glm::vec3(box->w, box->h, box->d);
   glm::vec3 max_box = box->center + glm::vec3(box->w, box->h, box->d);

   update_tile_box_to_fit_box(tile, min_box, max_box);

   // Add the objects to subtiles of its tile
   while (tile->subtiles) {
      uint32_t subtile_idx = subtile_index_from_position(s, tile, obj->pos);
      VkdfSceneTile *subtile = &tile->subtiles[subtile_idx];

      subtile->obj_count++;
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

   s->obj_count++;
   s->dirty = true;   
}

void
vkdf_scene_add_light(VkdfScene *s, VkdfLight *light)
{
   VkdfSceneLight *slight = g_new0(VkdfSceneLight, 1);
   slight->light = light;
   s->lights.push_back(slight);
}

static inline uint32_t
tile_is_visible(VkdfSceneTile *t, VkdfBox *visible_box, VkdfPlane *fp)
{
   if (t->obj_count == 0)
      return OUTSIDE;

   if (!vkdf_box_collision(&t->box, visible_box))
      return OUTSIDE;

   return vkdf_box_is_in_frustum(&t->box, fp);
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

   VkRenderPassBeginInfo rp_begin;
   s->callbacks.render_pass_begin_info(s->ctx,
                                       &rp_begin,
                                       s->framebuffer,
                                       s->fb_width, s->fb_height,
                                       s->callbacks.data);

   vkCmdBeginRenderPass(cmd_buf,
                        &rp_begin,
                        VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);

   uint32_t cmd_buf_count = g_list_length(active);
   if (cmd_buf_count > 0) {
      VkCommandBuffer *cmd_bufs = g_new(VkCommandBuffer, cmd_buf_count);
      GList *iter = active;
      for (uint32_t i = 0; i < cmd_buf_count; i++, iter = g_list_next(iter)) {
         VkdfSceneTile *t = (VkdfSceneTile *) iter->data;
         assert(t->cmd_buf != 0);
         cmd_bufs[i] = t->cmd_buf;
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
   if (s->sync.draw_fence_active &&
       vkGetFenceStatus(s->ctx->device, s->sync.draw_fence) == VK_SUCCESS) {
      vkResetFences(s->ctx->device, 1, &s->sync.draw_fence);
      s->sync.draw_fence_active = false;
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
add_to_cache(struct ThreadData *data, VkdfSceneTile *t)
{
   VkdfScene *s = data->s;
   uint32_t thread_id = data->id;

   s->cache[thread_id].cached = g_list_prepend(s->cache[thread_id].cached, t);
   s->cache[thread_id].size++;
}

static inline void
remove_from_cache(struct ThreadData *data, VkdfSceneTile *t)
{
   VkdfScene *s = data->s;
   uint32_t thread_id = data->id;

   assert(s->cache[thread_id].size > 0);
   s->cache[thread_id].cached = g_list_remove(s->cache[thread_id].cached, t);
   s->cache[thread_id].size--;
}

static void
new_active_tile(struct ThreadData *data, VkdfSceneTile *t)
{
   VkdfScene *s = data->s;
   uint32_t thread_id = data->id;

   assert(t->obj_count > 0);

   if (s->cache[thread_id].size > 0) {
      GList *found = g_list_find(s->cache[thread_id].cached, t);
      if (found) {
         remove_from_cache(data, t);
         s->cmd_buf.active[thread_id] =
            g_list_prepend(s->cmd_buf.active[thread_id], t);
         return;
      }
   }

   VkCommandBuffer cmd_buf =
      s->callbacks.record_commands(s->ctx, s->cmd_buf.pool[thread_id],
                                   s->framebuffer, s->fb_width, s->fb_height,
                                   t->sets, s->callbacks.data);
   t->cmd_buf = cmd_buf;
   s->cmd_buf.active[thread_id] =
      g_list_prepend(s->cmd_buf.active[thread_id], t);

   t->dirty = false;
}

static void
new_inactive_tile(struct ThreadData *data, VkdfSceneTile *t)
{
   VkdfScene *s = data->s;
   uint32_t thread_id = data->id;

   s->cmd_buf.active[thread_id] =
      g_list_remove(s->cmd_buf.active[thread_id], t);

   VkdfSceneTile *expired;
   if (s->cache[thread_id].max_size <= 0) {
      expired = t;
   } else {
      if (s->cache[thread_id].size >= s->cache[thread_id].max_size) {
         GList *last = g_list_last(s->cache[thread_id].cached);
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
   s->cmd_buf.free[thread_id] =
      g_list_prepend(s->cmd_buf.free[thread_id], info);
}

static void inline
new_inactive_cmd_buf(struct ThreadData *data, VkCommandBuffer cmd_buf)
{
   VkdfScene *s = data->s;
   uint32_t thread_id = data->id;

   struct FreeCmdBufInfo *info = g_new(struct FreeCmdBufInfo, 1);
   info->cmd_buf = cmd_buf;
   info->tile = NULL;
   s->cmd_buf.free[thread_id] =
      g_list_prepend(s->cmd_buf.free[thread_id], info);
}

static inline VkCommandBuffer
record_resource_updates(VkdfScene *s)
{
   return s->callbacks.update_resources(s->ctx,
                                        s->cmd_buf.pool[0],
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
            tile_set_info->objs =
               g_list_prepend(tile_set_info->objs, st_objs->data);
            st_objs = g_list_next(st_objs);
            tile_set_info->count++;
         }
      }
   }

   tile_set_info->objs = g_list_reverse(tile_set_info->objs);
}

static uint32_t
compute_tile_start_indices(VkdfScene *s,
                           VkdfSceneTile *t,
                           const char *set_id,
                           uint32_t start_index)
{
   VkdfSceneSetInfo *tile_set_info =
      (VkdfSceneSetInfo *) g_hash_table_lookup(t->sets, set_id);
   tile_set_info->start_index = start_index;

   if (!t->subtiles)
      return tile_set_info->start_index + tile_set_info->count;

   for (uint32_t i = 0; i < 8; i++) {
      VkdfSceneTile *st = &t->subtiles[i];
      VkdfSceneSetInfo *subtile_set_info =
         (VkdfSceneSetInfo *) g_hash_table_lookup(st->sets, set_id);

      subtile_set_info->start_index = start_index;
      compute_tile_start_indices(s, st, set_id, subtile_set_info->start_index);
      start_index += subtile_set_info->count;
    }

   return start_index;
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

/**
 * - Builds object lists for non-leaf (sub)tiles (making sure object
 *   order is correct)
 * - Computes (sub)tile starting indices
 */
void
vkdf_scene_prepare(VkdfScene *s)
{
   if (!s->dirty)
      return;

   s->set_ids = g_list_reverse(s->set_ids);

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
   GList *iter = s->set_ids;
   while (iter) {
      const char *set_id = (const char *) iter->data;
      for (uint32_t i = 0; i < s->num_tiles.total; i++) {
         VkdfSceneTile *t = &s->tiles[i];
         start_index = compute_tile_start_indices(s, t, set_id, start_index);
      }
      iter = g_list_next(iter);
   }

   s->dirty = false;
}

static GList *
process_partial_tile(VkdfSceneTile *t, VkdfPlane *fplanes, GList *visible)
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
         visible = process_partial_tile(&t->subtiles[j], fplanes, visible);
      }
   }

   return visible;
}

static void
thread_update_cmd_bufs(void *arg)
{
   struct ThreadData *data = (struct ThreadData *) arg;

   VkdfScene *s = data->s;

   VkdfBox *visible_box = data->visible_box;
   VkdfPlane *fplanes = data->fplanes;

   uint32_t first_idx = data->first_idx;
   uint32_t last_idx = data->last_idx;

   GList *cur_visible = NULL;
   GList *prev_visible = data->visible;

   // Find visible tiles
   for (uint32_t i = first_idx; i <= last_idx; i++) {
      VkdfSceneTile *t = &s->tiles[i];
      uint32_t visibility = tile_is_visible(t, visible_box, fplanes);
      if (visibility == INSIDE) {
         cur_visible = g_list_prepend(cur_visible, t);
      } else if (visibility == INTERSECT) {
         cur_visible = process_partial_tile(t, fplanes, cur_visible);
      }
   }

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
   VkdfBox visible_box;
   vkdf_camera_get_clip_box(s->camera, &visible_box);

   VkdfPlane fplanes[6];
   vkdf_camera_get_frustum_planes(s->camera, fplanes);

   for (uint32_t thread_idx = 0;
        thread_idx < s->thread.num_threads;
        thread_idx++) {
      s->thread.data[thread_idx].visible_box = &visible_box;
      s->thread.data[thread_idx].fplanes = fplanes;
      s->thread.data[thread_idx].cmd_buf_changes = false;
   }

   if (s->thread.pool) {
      for (uint32_t thread_idx = 0;
           thread_idx < s->thread.num_threads;
           thread_idx++) {
         vkdf_thread_pool_add_job(s->thread.pool,
                                  thread_update_cmd_bufs,
                                  &s->thread.data[thread_idx]);
      }
      vkdf_thread_pool_wait(s->thread.pool);
   } else {
      thread_update_cmd_bufs(&s->thread.data[0]);
   }

   bool cmd_buf_changes = s->thread.data[0].cmd_buf_changes;
   for (uint32_t thread_idx = 1;
        cmd_buf_changes == false && thread_idx < s->thread.num_threads;
        thread_idx++) {
      cmd_buf_changes = cmd_buf_changes ||
                        s->thread.data[thread_idx].cmd_buf_changes;
   }

   return cmd_buf_changes;
}

void
vkdf_scene_update_cmd_bufs(VkdfScene *s, VkCommandPool cmd_pool)
{
   // Check if any fences have been signaled and if so free any disposable
   // command buffers that were pending execution on signaled fences
   if (check_fences(s))
      free_inactive_command_buffers(s);

   // Record the command buffer that updates rendering resources
   if (s->cmd_buf.update_resources)
      new_inactive_cmd_buf(&s->thread.data[0], s->cmd_buf.update_resources);
   s->cmd_buf.update_resources = record_resource_updates(s);

   // If the camera didn't change there is nothing to update
   // FIXME: once we have dynamic objects, we would need to check for those too
   if (!vkdf_camera_is_dirty(s->camera))
      return;

   // Update the list of active secondary command buffers
   bool cmd_buf_changes = update_cmd_bufs(s);

   // If any secondary has changed, we need to re-record the primary
   if (cmd_buf_changes || !s->cmd_buf.primary) {
      if (s->cmd_buf.primary)
         new_inactive_cmd_buf(&s->thread.data[0], s->cmd_buf.primary);
      s->cmd_buf.primary = build_primary_cmd_buf(s);
   }
}

VkSemaphore
vkdf_scene_draw(VkdfScene *s)
{
   VkPipelineStageFlags scene_wait_stage;
   uint32_t scene_wait_sem_count;
   VkSemaphore *scene_wait_sem;

   // If we are still rendering the previous frame we have to wait or we
   // might corrupt its rendering
   while (s->sync.draw_fence_active) {
      VkResult status;
      do {
         status = vkWaitForFences(s->ctx->device,
                                  1, &s->sync.draw_fence,
                                  true, 1000ull);
      } while (status == VK_NOT_READY || status == VK_TIMEOUT);
      vkResetFences(s->ctx->device, 1, &s->sync.draw_fence);
      s->sync.draw_fence_active = false;
      free_inactive_command_buffers(s);
   }

   // If we have a resource update command, execute it first
   if (s->cmd_buf.update_resources) {
      VkPipelineStageFlags resources_wait_stage = 0;
      vkdf_command_buffer_execute(s->ctx,
                                  s->cmd_buf.update_resources,
                                  &resources_wait_stage,
                                  0, NULL,
                                  1, &s->sync.update_resources_sem);


      scene_wait_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
      scene_wait_sem_count = 1;
      scene_wait_sem = &s->sync.update_resources_sem;
   } else {
      scene_wait_stage = 0;
      scene_wait_sem_count = 0;
      scene_wait_sem = NULL;
   }

   // Execute scene primary command. Use a fence so we know when the
   // secondaries in the primary are no longer in use
   vkdf_command_buffer_execute_with_fence(s->ctx,
                                          s->cmd_buf.primary,
                                          &scene_wait_stage,
                                          scene_wait_sem_count, scene_wait_sem,
                                          1, &s->sync.draw_sem,
                                          s->sync.draw_fence);

   s->sync.draw_fence_active = true;

   return s->sync.draw_sem;
}
