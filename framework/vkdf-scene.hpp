#ifndef __VKDF_SCENE_H__
#define __VKDF_SCENE_H__

typedef struct {
   VkdfLight *light;
   VkdfImage shadow_map;
   bool has_shadow_map;
} VkdfSceneLight;

typedef struct _VkdfSceneTile VkdfSceneTile;
typedef struct _VkdfScene VkdfScene;

struct ThreadData {
   uint32_t id;
   VkdfScene *s;
   uint32_t first_idx;
   uint32_t last_idx;
   VkdfBox *visible_box;
   VkdfPlane *fplanes;
   GList *visible;
   bool cmd_buf_changes;
};

typedef struct {
   GList *objs;                 // Set list
   uint32_t start_index;        // The global scene set index of the first object in this set
   uint32_t count;              // Number of objects in the set
} VkdfSceneSetInfo;

struct _VkdfSceneTile {
   int32_t parent;
   uint32_t level;               // Level of the tile
   uint32_t index;               // Index of the tile in the level
   glm::vec3 offset;             // world-space offset of the tile
   bool dirty;                   // Whether new objects have been added
   VkdfBox box;                  // Bounding box of the ojects in the tile
   GHashTable *sets;             // Objects in the tile
   uint32_t obj_count;           // Number of objects in the tile
   VkCommandBuffer cmd_buf;      // Secondary command buffer for this tile
   VkdfSceneTile *subtiles;      // Subtiles within this tile
};

typedef VkCommandBuffer (*VkdfSceneUpdateResourcesCB)(VkdfContext *, VkCommandPool, void *);
typedef VkCommandBuffer (*VkdfSceneCommandsCB)(VkdfContext *, VkCommandPool, VkFramebuffer, uint32_t, uint32_t, GHashTable *sets, void *);
typedef void (*VkdfSceneRenderPassBeginInfoCB)(VkdfContext *, VkRenderPassBeginInfo *, VkFramebuffer, uint32_t, uint32_t, void *);

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

   // Render target
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

   bool dirty;
   uint32_t obj_count;

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
      VkCommandBuffer primary;
      VkCommandBuffer update_resources;
   } cmd_buf;

   struct {
      VkSemaphore update_resources_sem;
      VkSemaphore draw_sem;
      VkFence draw_fence;
      bool draw_fence_active;
   } sync;

   struct {
      VkdfSceneUpdateResourcesCB update_resources;
      VkdfSceneRenderPassBeginInfoCB render_pass_begin_info;
      VkdfSceneCommandsCB record_commands;
      void *data;
   } callbacks;

   struct {
      VkdfThreadPool *pool;
      uint32_t num_threads;
      uint32_t work_size;
      struct ThreadData *data;
   } thread;
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

inline void
vkdf_scene_set_render_target(VkdfScene *s,
                             VkFramebuffer fb,
                             uint32_t width,
                             uint32_t height)
{
   s->framebuffer = fb;
   s->fb_width = width;
   s->fb_height = height;
}

inline VkdfCamera *
vkdf_scene_get_camera(VkdfScene *scene)
{
   return scene->camera;
}

void
vkdf_scene_add_object(VkdfScene *scene, const char *set_id, VkdfObject *obj);

void
vkdf_scene_prepare(VkdfScene *scene);

inline uint32_t
vkdf_scene_get_num_objects(VkdfScene *scene)
{
   return scene->obj_count;
}

inline GList *
vkdf_scene_get_tile_object_set(VkdfSceneTile *t, const char *set_id)
{
   return (GList *) g_hash_table_lookup(t->sets, set_id);
}

inline uint32_t
vkdf_scene_get_num_tiles(VkdfScene *s)
{
   return s->num_tiles.total;
}

void
vkdf_scene_add_light(VkdfScene *s, VkdfLight *light);

inline void
vkdf_scene_set_scene_callbacks(VkdfScene *s,
                               VkdfSceneUpdateResourcesCB ur_cb,
                               VkdfSceneRenderPassBeginInfoCB rp_cb,
                               VkdfSceneCommandsCB cmd_cb,
                               void *data)
{
   s->callbacks.update_resources = ur_cb;
   s->callbacks.render_pass_begin_info = rp_cb;
   s->callbacks.record_commands = cmd_cb;
   s->callbacks.data = data;
}

void
vkdf_scene_update_cmd_bufs(VkdfScene *s, VkCommandPool cmd_pool);

void
vkdf_scene_optimize_clip_boxes(VkdfScene *s);

VkSemaphore
vkdf_scene_draw(VkdfScene *s);


#endif
