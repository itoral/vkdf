#include "vkdf.hpp"

//const float WIN_WIDTH  = 800.0f;
//const float WIN_HEIGHT = 600.0f;

const float WIN_WIDTH  = 1920.0f;
const float WIN_HEIGHT = 1080.0f;

const uint32_t NUM_OBJECTS = 1000000;

// ----------------------------------------------------------------------------
// Renders a scene
// ----------------------------------------------------------------------------

struct PCBData {
   uint8_t proj[sizeof(glm::mat4)];
};

typedef struct {
   VkdfContext *ctx;

   VkdfScene *scene;

   VkdfCamera *camera;

   struct {
      VkDescriptorPool static_ubo_pool;
   } descriptor_pool;

   struct {
      struct {
         VkPipeline pipeline;
         VkPipelineCache cache;
         VkPipelineLayout layout;
         struct {
            VkDescriptorSetLayout camera_view_layout;
            VkDescriptorSet camera_view_set;
            VkDescriptorSetLayout obj_layout;
            VkDescriptorSet obj_set;
         } descr;
      } obj;
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
         VkShaderModule fs;
      } obj;
   } shaders;

   VkdfMesh *cube_mesh;
   VkdfModel *cube_model;
} SceneResources;

typedef struct {
   glm::vec4 pos;
} VertexData;

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
   // Camera view
   res->ubos.camera_view.size = sizeof(glm::mat4);
   res->ubos.camera_view.buf = create_ubo(res->ctx,
                                          res->ubos.camera_view.size,
                                          VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
}

static bool
record_update_resources_command(VkdfContext *ctx,
                                VkCommandBuffer cmd_buf,
                                void *data)
{
   SceneResources *res = (SceneResources *) data;

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
record_scene_commands(VkdfContext *ctx,
                      VkCommandBuffer cmd_buf,
                      GHashTable *sets, bool is_dynamic,
                      bool is_depth_prepass, void *data)
{
   SceneResources *res = (SceneResources *) data;

   // Pipeline
   vkCmdBindPipeline(cmd_buf,
                     VK_PIPELINE_BIND_POINT_GRAPHICS,
                     res->pipelines.obj.pipeline);

   // Push constants
   struct PCBData pcb_data;
   glm::mat4 *proj = vkdf_camera_get_projection_ptr(res->scene->camera);
   memcpy(&pcb_data.proj, &(*proj)[0][0], sizeof(pcb_data.proj));

   vkCmdPushConstants(cmd_buf,
                      res->pipelines.obj.layout,
                      VK_SHADER_STAGE_VERTEX_BIT,
                      0, sizeof(pcb_data), &pcb_data);

   // Descriptors
   VkDescriptorSet descriptor_sets[] = {
      res->pipelines.obj.descr.camera_view_set,
      res->pipelines.obj.descr.obj_set,
   };

   vkCmdBindDescriptorSets(cmd_buf,
                           VK_PIPELINE_BIND_POINT_GRAPHICS,
                           res->pipelines.obj.layout,
                           0,                        // First decriptor set
                           2,                        // Descriptor set count
                           descriptor_sets,          // Descriptor sets
                           0,                        // Dynamic offset count
                           NULL);                    // Dynamic offsets

   VkdfModel *model = res->cube_model;

   VkdfSceneSetInfo *cube_info =
      (VkdfSceneSetInfo *) g_hash_table_lookup(sets, "cube");
   assert(cube_info->count > 0);

   for (uint32_t i = 0; i < model->meshes.size(); i++) {
      VkdfMesh *mesh = model->meshes[i];

      // Vertex buffers
      const VkDeviceSize offsets[1] = { 0 };
      vkCmdBindVertexBuffers(cmd_buf,
                             0,                         // Start Binding
                             1,                         // Binding Count
                             &mesh->vertex_buf.buf,     // Buffers
                             offsets);                  // Offsets
      // Draw
      vkCmdDraw(cmd_buf,
                mesh->vertices.size(),               // vertex count
                cube_info->count,                    // instance count
                0,                                   // first vertex
                cube_info->start_index);             // first instance
   }
}

static void
update_camera(SceneResources *res)
{
   const float mov_speed = 0.15f;
   const float rot_speed = 1.0f;

   VkdfCamera *cam = vkdf_scene_get_camera(res->scene);
   VkdfPlatform *platform = &res->ctx->platform;

   float base_speed = 1.0f;

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
   if (vkdf_platform_key_is_pressed(platform, VKDF_KEY_UP)) {
      float step_speed = base_speed * mov_speed;
      vkdf_camera_step(cam, step_speed, 1, 1, 1);
   } else if (vkdf_platform_key_is_pressed(platform, VKDF_KEY_DOWN)) {
      float step_speed = -base_speed * mov_speed;
      vkdf_camera_step(cam, step_speed, 1, 1, 1);
   }
}

static void
scene_update(void *data)
{
   SceneResources *res = (SceneResources *) data;
   update_camera(res);
}

static void
init_scene(SceneResources *res)
{
   VkdfContext *ctx = res->ctx;

   res->camera = vkdf_camera_new(0.0f, 0.0f, 0.0f,
                                 0.0f, 180.0f, 0.0f,
                                 45.0f, 0.1f, 2000.0f, WIN_WIDTH / WIN_HEIGHT);

   glm::vec3 scene_origin = glm::vec3(-500.0f, -500.0f, -500.0f);
   glm::vec3 scene_size = glm::vec3(1000.0f, 1000.0f, 1000.0f);
   uint32_t cache_size = 8;
#if 1
   // Final
   glm::vec3 tile_size = glm::vec3(250.0f, 250.0f, 250.0f);
   res->scene = vkdf_scene_new(ctx,
                               WIN_WIDTH, WIN_HEIGHT,
                               res->camera,
                               scene_origin, scene_size, tile_size, 2,
                               cache_size, 4);
#elif 0
   // Naive CPU clipping
   glm::vec3 tile_size = glm::vec3(25.0f, 25.0f, 25.0f);
   res->scene = vkdf_scene_new(ctx,
                               WIN_WIDTH, WIN_HEIGHT,
                               res->camera,
                               scene_origin, scene_size, tile_size, 1,
                               cache_size, 1);
#else
   // GPU clipping only
   glm::vec3 tile_size = glm::vec3(1000.0f, 1000.0f, 1000.0f);
   res->scene = vkdf_scene_new(ctx,
                               WIN_WIDTH, WIN_HEIGHT,
                               res->camera,
                               scene_origin, scene_size, tile_size, 1,
                               cache_size, 1);
#endif

   vkdf_scene_set_scene_callbacks(res->scene,
                                  scene_update,
                                  record_update_resources_command,
                                  record_scene_commands,
                                  res);
}

static void
init_obj_pipeline(SceneResources *res, bool init_cache)
{
   if (!res->pipelines.obj.layout) {
      VkPushConstantRange pcb_range;
      pcb_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
      pcb_range.offset = 0;
      pcb_range.size = sizeof(PCBData);

      VkPushConstantRange pcb_ranges[] = {
         pcb_range,
      };

      res->pipelines.obj.descr.camera_view_layout =
         vkdf_create_ubo_descriptor_set_layout(res->ctx, 0, 1,
                                               VK_SHADER_STAGE_VERTEX_BIT,
                                               false);

      res->pipelines.obj.descr.obj_layout =
         vkdf_create_ubo_descriptor_set_layout(res->ctx, 0, 2,
                                               VK_SHADER_STAGE_VERTEX_BIT |
                                                  VK_SHADER_STAGE_FRAGMENT_BIT,
                                               false);

      VkDescriptorSetLayout layouts[] = {
         res->pipelines.obj.descr.camera_view_layout,
         res->pipelines.obj.descr.obj_layout,
      };

      VkPipelineLayoutCreateInfo pipeline_layout_info;
      pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
      pipeline_layout_info.pNext = NULL;
      pipeline_layout_info.pushConstantRangeCount = 1;
      pipeline_layout_info.pPushConstantRanges = pcb_ranges;
      pipeline_layout_info.setLayoutCount = 2;
      pipeline_layout_info.pSetLayouts = layouts;
      pipeline_layout_info.flags = 0;

      VK_CHECK(vkCreatePipelineLayout(res->ctx->device,
                                      &pipeline_layout_info,
                                      NULL,
                                      &res->pipelines.obj.layout));

      res->pipelines.obj.descr.camera_view_set =
         create_descriptor_set(res->ctx,
                               res->descriptor_pool.static_ubo_pool,
                               res->pipelines.obj.descr.camera_view_layout);

      VkDeviceSize ubo_offset = 0;
      VkDeviceSize ubo_size = res->ubos.camera_view.size;
      vkdf_descriptor_set_buffer_update(res->ctx,
                                        res->pipelines.obj.descr.camera_view_set,
                                        res->ubos.camera_view.buf.buf,
                                        0, 1, &ubo_offset, &ubo_size, false, true);

      res->pipelines.obj.descr.obj_set =
         create_descriptor_set(res->ctx,
                               res->descriptor_pool.static_ubo_pool,
                               res->pipelines.obj.descr.obj_layout);

      VkdfBuffer *obj_ubo = vkdf_scene_get_object_ubo(res->scene);
      VkDeviceSize obj_ubo_size = vkdf_scene_get_object_ubo_size(res->scene);
      ubo_offset = 0;
      ubo_size = obj_ubo_size;
      vkdf_descriptor_set_buffer_update(res->ctx,
                                        res->pipelines.obj.descr.obj_set,
                                        obj_ubo->buf,
                                        0, 1, &ubo_offset, &ubo_size, false, true);

      VkdfBuffer *material_ubo = vkdf_scene_get_material_ubo(res->scene);
      VkDeviceSize material_ubo_size = vkdf_scene_get_material_ubo_size(res->scene);
      ubo_offset = 0;
      ubo_size = material_ubo_size;
      vkdf_descriptor_set_buffer_update(res->ctx,
                                        res->pipelines.obj.descr.obj_set,
                                        material_ubo->buf,
                                        1, 1, &ubo_offset, &ubo_size, false, true);
   }

   if (init_cache) {
      VkPipelineCacheCreateInfo info;
      info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
      info.pNext = NULL;
      info.initialDataSize = 0;
      info.pInitialData = NULL;
      info.flags = 0;
      VK_CHECK(vkCreatePipelineCache(res->ctx->device, &info, NULL,
                                     &res->pipelines.obj.cache));
   }

   VkVertexInputBindingDescription vi_bindings[1];
   VkVertexInputAttributeDescription vi_attribs[2];

   // Vertex attribute binding 0: position, normal
   uint32_t stride = 2 * sizeof(glm::vec3);
   vkdf_vertex_binding_set(&vi_bindings[0],
                           0, VK_VERTEX_INPUT_RATE_VERTEX, stride);

   /* binding 0, location 0: position
    * binding 0, location 1: normal
    */
   vkdf_vertex_attrib_set(&vi_attribs[0], 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0);
   vkdf_vertex_attrib_set(&vi_attribs[1], 0, 1, VK_FORMAT_R32G32B32_SFLOAT, 12);

   VkRenderPass renderpass = vkdf_scene_get_static_render_pass(res->scene);

   VkPrimitiveTopology primitive = vkdf_mesh_get_primitive(res->cube_mesh);
   res->pipelines.obj.pipeline =
      vkdf_create_gfx_pipeline(res->ctx,
                               &res->pipelines.obj.cache,
                               1,
                               vi_bindings,
                               2,
                               vi_attribs,
                               true,
                               VK_COMPARE_OP_LESS,
                               renderpass,
                               res->pipelines.obj.layout,
                               primitive,
                               VK_CULL_MODE_BACK_BIT,
                               1,
                               res->shaders.obj.vs,
                               res->shaders.obj.fs);
}

static void
init_shaders(SceneResources *res)
{
   res->shaders.obj.vs = vkdf_create_shader_module(res->ctx, "scene.vert.spv");
   res->shaders.obj.fs = vkdf_create_shader_module(res->ctx, "scene.frag.spv");
}

static inline void
init_pipelines(SceneResources *res)
{
   init_obj_pipeline(res, true);
}

static void
init_meshes(SceneResources *res)
{
   VkdfMaterial red;
   red.diffuse = glm::vec4(0.5f, 0.0f, 0.0f, 1.0f);
   red.ambient = glm::vec4(0.5f, 0.0f, 0.0f, 1.0f);
   red.specular = glm::vec4(1.0f, 0.75f, 0.75f, 1.0f);
   red.shininess = 48.0f;

   VkdfMaterial green;
   green.diffuse = glm::vec4(0.0f, 0.5f, 0.0f, 1.0f);
   green.ambient = glm::vec4(0.0f, 0.5f, 0.0f, 1.0f);
   green.specular = glm::vec4(0.75f, 1.0f, 0.75f, 1.0f);
   green.shininess = 48.0f;

   VkdfMaterial blue;
   blue.diffuse = glm::vec4(0.0f, 0.0f, 0.5f, 1.0f);
   blue.ambient = glm::vec4(0.0f, 0.0f, 0.5f, 1.0f);
   blue.specular = glm::vec4(0.75f, 0.75f, 1.0f, 1.0f);
   blue.shininess = 48.0f;

   VkdfMaterial yellow;
   yellow.diffuse = glm::vec4(0.5f, 0.5f, 0.0f, 1.0f);
   yellow.ambient = glm::vec4(0.0f, 0.0f, 0.5f, 1.0f);
   yellow.specular = glm::vec4(1.0f, 1.0f, 0.75f, 1.0f);
   yellow.shininess = 48.0f;

   res->cube_mesh = vkdf_cube_mesh_new(res->ctx);
   vkdf_mesh_fill_vertex_buffer(res->ctx, res->cube_mesh);

   res->cube_model = vkdf_model_new();
   vkdf_model_add_mesh(res->cube_model, res->cube_mesh);
   vkdf_model_compute_box(res->cube_model);

   vkdf_model_add_material(res->cube_model, &red);
   vkdf_model_add_material(res->cube_model, &green);
   vkdf_model_add_material(res->cube_model, &blue);
   vkdf_model_add_material(res->cube_model, &yellow);
}

static void
init_objects(SceneResources *res)
{
   glm::vec3 origin = res->scene->scene_area.origin;
   for (uint32_t i = 0; i < NUM_OBJECTS; i++) {
      glm::vec3 pos;
      pos.x= origin.x + random() % ((uint32_t) (res->scene->scene_area.w - 1.0f));
      pos.y= origin.y + random() % ((uint32_t) (res->scene->scene_area.h - 1.0f));
      pos.z= origin.z + random() % ((uint32_t) (res->scene->scene_area.d - 1.0f));
      pos.x += (random() % 100) / 100.0f;
      pos.y += (random() % 100) / 100.0f;
      pos.z += (random() % 100) / 100.0f;

      VkdfObject *obj = vkdf_object_new_from_model(pos, res->cube_model);
      vkdf_scene_add_object(res->scene, "cube", obj);

      // Assign a different material to objects in adjacent tiles
      uint32_t mat_idx = random() % 4;
      vkdf_object_set_material_idx_base(obj, mat_idx);
   }

   VkClearValue color_clear;
   vkdf_color_clear_set(&color_clear, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));

   VkClearValue depth_clear;
   vkdf_depth_stencil_clear_set(&depth_clear, 1.0f, 0);

   vkdf_scene_set_clear_values(res->scene, &color_clear, &depth_clear);

   vkdf_scene_prepare(res->scene);
}

static void
init_descriptor_pools(SceneResources *res)
{
   res->descriptor_pool.static_ubo_pool =
      vkdf_create_descriptor_pool(res->ctx,
                                  VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 8);
}

static void
init_resources(VkdfContext *ctx, SceneResources *res)
{
   memset(res, 0, sizeof(SceneResources));

   res->ctx = ctx;

   init_scene(res);
   init_meshes(res);
   init_objects(res);
   init_ubos(res);
   init_shaders(res);
   init_descriptor_pools(res);
   init_pipelines(res);
}

static void
destroy_models(SceneResources *res)
{
   vkdf_model_free(res->ctx, res->cube_model);
}

static void
destroy_pipelines(SceneResources *res)
{
   vkDestroyPipelineCache(res->ctx->device, res->pipelines.obj.cache, NULL);
   vkDestroyPipeline(res->ctx->device, res->pipelines.obj.pipeline, NULL);

   vkDestroyPipelineLayout(res->ctx->device, res->pipelines.obj.layout, NULL);

   vkFreeDescriptorSets(res->ctx->device,
                        res->descriptor_pool.static_ubo_pool,
                        1, &res->pipelines.obj.descr.obj_set);
   vkDestroyDescriptorSetLayout(res->ctx->device,
                                res->pipelines.obj.descr.obj_layout, NULL);

   vkFreeDescriptorSets(res->ctx->device,
                        res->descriptor_pool.static_ubo_pool,
                        1, &res->pipelines.obj.descr.camera_view_set);
   vkDestroyDescriptorSetLayout(res->ctx->device,
                                res->pipelines.obj.descr.camera_view_layout, NULL);

   vkDestroyDescriptorPool(res->ctx->device,
                           res->descriptor_pool.static_ubo_pool, NULL);
}

static void
destroy_shader_modules(SceneResources *res)
{
  vkDestroyShaderModule(res->ctx->device, res->shaders.obj.vs, NULL);
  vkDestroyShaderModule(res->ctx->device, res->shaders.obj.fs, NULL);
}

static void
destroy_ubos(SceneResources *res)
{
   vkDestroyBuffer(res->ctx->device, res->ubos.camera_view.buf.buf, NULL);
   vkFreeMemory(res->ctx->device, res->ubos.camera_view.buf.mem, NULL);
}

void
cleanup_resources(VkdfContext *ctx, SceneResources *res)
{
   vkdf_scene_free(res->scene);
   destroy_models(res);
   destroy_shader_modules(res);
   destroy_pipelines(res);
   destroy_ubos(res);

   vkdf_camera_free(ctx, res->camera);
}

int
main()
{
   VkdfContext ctx;
   SceneResources resources;

   vkdf_init(&ctx, WIN_WIDTH, WIN_HEIGHT, false, false, false);
   init_resources(&ctx, &resources);

   vkdf_scene_event_loop_run(resources.scene);

   cleanup_resources(&ctx, &resources);
   vkdf_cleanup(&ctx);

   return 0;
}
