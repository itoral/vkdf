#include "vkdf.hpp"

const float WIN_WIDTH  = 1024.0f;
const float WIN_HEIGHT = 768.0f;

const uint32_t NUM_LIGHTS = 2;
const bool LIGHT_IS_DYNAMIC[NUM_LIGHTS] = { true, false };

// FIXME: we only show the shadow map for one light, would be nice
// to allow the user to switch the shadow map to display at run-time
const uint32_t debug_light_idx = 0;

// ----------------------------------------------------------------------------
// Renders a scene with lighting
//
// The scene contains different object models with varying material sets
// ----------------------------------------------------------------------------

struct PCBData {
   uint8_t proj[sizeof(glm::mat4)];
};

typedef struct {
   VkdfContext *ctx;

   VkdfScene *scene;

   VkdfCamera *camera;
   VkdfLight *lights[NUM_LIGHTS];

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
         VkDescriptorSet dyn_obj_set;
         VkDescriptorSetLayout light_layout;
         VkDescriptorSet light_set;
         VkDescriptorSetLayout shadow_map_sampler_layout;
         VkDescriptorSet shadow_map_sampler_set;
      } descr;

      struct {
         VkPipelineLayout common;
      } layout;

      struct {
         VkPipeline static_pipeline;
         VkPipeline dynamic_pipeline;
      } obj;

      struct {
         VkPipeline pipeline;
      } floor;
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
      struct {
         VkShaderModule vs;
         VkShaderModule fs;
      } floor;
   } shaders;

   struct {
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

   VkdfMesh *cube_mesh;
   VkdfModel *cube_model;

   VkdfMesh *floor_mesh;
   VkdfModel *floor_model;

   VkdfModel *tree_model;

   VkdfMesh *tile_mesh;
} SceneResources;

typedef struct {
   glm::vec4 pos;
} VertexData;

static VkdfImage
postprocess_draw(VkdfContext *ctx,
                 VkSemaphore scene_draw_sem,
                 VkSemaphore postprocess_draw_sem,
                 void *data);

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
   res->ubos.camera_view.size = 2 * sizeof(glm::mat4);
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

   glm::mat4 view_inv = glm::inverse(view);
   vkCmdUpdateBuffer(cmd_buf,
                     res->ubos.camera_view.buf.buf,
                     sizeof(glm::mat4), sizeof(glm::mat4),
                     &view_inv[0][0]);

   return true;
}

static void
record_instanced_draw(VkCommandBuffer cmd_buf,
                      VkPipeline pipeline,
                      VkdfModel *model,
                      uint32_t count,
                      uint32_t first_instance)
{
   vkCmdBindPipeline(cmd_buf,
                     VK_PIPELINE_BIND_POINT_GRAPHICS,
                     pipeline);

   for (uint32_t i = 0; i < model->meshes.size(); i++) {
      VkdfMesh *mesh = model->meshes[i];

      const VkDeviceSize offsets[1] = { 0 };
      vkCmdBindVertexBuffers(cmd_buf,
                             0,                         // Start Binding
                             1,                         // Binding Count
                             &mesh->vertex_buf.buf,     // Buffers
                             offsets);                  // Offsets

      vkdf_mesh_draw(mesh, cmd_buf, count, first_instance);
   }
}

static void
record_scene_commands(VkdfContext *ctx, VkCommandBuffer cmd_buf,
                      GHashTable *sets, bool is_dynamic,
                      bool is_deth_prepass, void *data)
{
   SceneResources *res = (SceneResources *) data;

   // Push constants
   struct PCBData pcb_data;
   glm::mat4 *proj = vkdf_camera_get_projection_ptr(res->scene->camera);
   memcpy(&pcb_data.proj, &(*proj)[0][0], sizeof(pcb_data.proj));

   vkCmdPushConstants(cmd_buf,
                      res->pipelines.layout.common,
                      VK_SHADER_STAGE_VERTEX_BIT,
                      0, sizeof(pcb_data), &pcb_data);

   // Descriptors
   VkDescriptorSet descriptor_sets[] = {
      res->pipelines.descr.camera_view_set,
      !is_dynamic ? res->pipelines.descr.obj_set :
                    res->pipelines.descr.dyn_obj_set,
      res->pipelines.descr.light_set,
      res->pipelines.descr.shadow_map_sampler_set
   };

   vkCmdBindDescriptorSets(cmd_buf,
                           VK_PIPELINE_BIND_POINT_GRAPHICS,
                           res->pipelines.layout.common,
                           0,                        // First decriptor set
                           4,                        // Descriptor set count
                           descriptor_sets,          // Descriptor sets
                           0,                        // Dynamic offset count
                           NULL);                    // Dynamic offsets

   char *set_id;
   VkdfSceneSetInfo *set_info;
   GHashTableIter iter;
   g_hash_table_iter_init(&iter, sets);
   while (g_hash_table_iter_next(&iter, (void **)&set_id, (void **)&set_info)) {
      if (!set_info || set_info->count == 0)
         continue;

      if (!strcmp(set_id, "cube") || !strcmp(set_id, "dyn-cube")) {
         VkPipeline *pipeline = is_dynamic ?
                                   &res->pipelines.obj.dynamic_pipeline :
                                   &res->pipelines.obj.static_pipeline;

         record_instanced_draw(cmd_buf,
                               *pipeline,
                               res->cube_model,
                               set_info->count, set_info->start_index);
         continue;
      }

      if (!strcmp(set_id, "tree")) {
         record_instanced_draw(cmd_buf,
                               res->pipelines.obj.static_pipeline,
                               res->tree_model,
                               set_info->count, set_info->start_index);
         continue;
      }

      if (!strcmp(set_id, "floor")) {
         record_instanced_draw(cmd_buf,
                               res->pipelines.floor.pipeline,
                               res->floor_model,
                               set_info->count, set_info->start_index);
         continue;
      }

      assert(!"unkown object category");
   }
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
update_objects(SceneResources *res)
{
   VkdfSceneSetInfo *info =
      vkdf_scene_get_dynamic_object_set(res->scene, "dyn-cube");
   if (!info || info->count == 0)
      return;

   GList *iter = info->objs;
   while (iter) {
      VkdfObject *obj = (VkdfObject *) iter->data;
      glm::vec3 rot = obj->rot;
      rot.x += 0.1f;
      rot.y += 0.5f;
      rot.z += 1.0f;
      vkdf_object_set_rotation(obj, rot);
      iter = g_list_next(iter);
   }
}

static void
update_lights(SceneResources *res)
{
   const float rot_speeds[NUM_LIGHTS] = { 1.5f, 2.0f };

   for (uint32_t i = 0; i < NUM_LIGHTS; i++) {
      if (LIGHT_IS_DYNAMIC[i]) {
         VkdfLight *l = res->lights[i];
         glm::vec3 rot = vkdf_light_get_rotation(l);
         rot.y += rot_speeds[i];
         if (rot.y > 360.0f)
            rot.y -= 360.0f;
         vkdf_light_set_rotation(l, rot);
      }
   }
}

static void
scene_update(void *data)
{
   SceneResources *res = (SceneResources *) data;

   update_camera(res);
   update_objects(res);
   update_lights(res);
}

static void
init_scene(SceneResources *res)
{
   VkdfContext *ctx = res->ctx;

   res->camera = vkdf_camera_new(0.0f, 10.0f, -30.0f,
                                 0.0f, 180.0f, 0.0f,
                                 45.0f, 0.1f, 500.0f, WIN_WIDTH / WIN_HEIGHT);

   vkdf_camera_look_at(res->camera, 0.0f, 3.0f, 0.0f);

   glm::vec3 scene_origin = glm::vec3(-50.0f, -50.0f, -50.0f);
   glm::vec3 scene_size = glm::vec3(100.0f, 100.0f, 100.0f);
   glm::vec3 tile_size = glm::vec3(25.0f, 25.0f, 25.0f);
   uint32_t cache_size = 32;
   res->scene = vkdf_scene_new(ctx,
                               WIN_WIDTH, WIN_HEIGHT,
                               res->camera,
                               scene_origin, scene_size, tile_size, 2,
                               cache_size, 1);

   vkdf_scene_set_scene_callbacks(res->scene,
                                  scene_update,
                                  record_update_resources_command,
                                  record_scene_commands,
                                  postprocess_draw,
                                  res);
}

static void
init_pipeline_descriptors(SceneResources *res)
{
   if (res->pipelines.layout.common)
      return;

   VkPushConstantRange pcb_range;
   pcb_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
   pcb_range.offset = 0;
   pcb_range.size = sizeof(PCBData);

   VkPushConstantRange pcb_ranges[] = {
      pcb_range,
   };

   res->pipelines.descr.camera_view_layout =
      vkdf_create_ubo_descriptor_set_layout(res->ctx, 0, 1,
                                            VK_SHADER_STAGE_VERTEX_BIT,
                                            false);

   res->pipelines.descr.obj_layout =
      vkdf_create_ubo_descriptor_set_layout(res->ctx, 0, 2,
                                            VK_SHADER_STAGE_VERTEX_BIT |
                                               VK_SHADER_STAGE_FRAGMENT_BIT,
                                            false);

   res->pipelines.descr.light_layout =
      vkdf_create_ubo_descriptor_set_layout(res->ctx, 0, 2,
                                            VK_SHADER_STAGE_VERTEX_BIT |
                                                VK_SHADER_STAGE_FRAGMENT_BIT,
                                            false);

   res->pipelines.descr.shadow_map_sampler_layout =
      vkdf_create_sampler_descriptor_set_layout(res->ctx, 0, NUM_LIGHTS,
                                                VK_SHADER_STAGE_FRAGMENT_BIT);

   VkDescriptorSetLayout layouts[] = {
      res->pipelines.descr.camera_view_layout,
      res->pipelines.descr.obj_layout,
      res->pipelines.descr.light_layout,
      res->pipelines.descr.shadow_map_sampler_layout,
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
                                   &res->pipelines.layout.common));

   // Camera descriptor
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

   // Static objects descriptor
   res->pipelines.descr.obj_set =
      create_descriptor_set(res->ctx,
                            res->descriptor_pool.static_ubo_pool,
                            res->pipelines.descr.obj_layout);

   VkdfBuffer *obj_ubo = vkdf_scene_get_object_ubo(res->scene);
   VkDeviceSize obj_ubo_size = vkdf_scene_get_object_ubo_size(res->scene);
   ubo_offset = 0;
   ubo_size = obj_ubo_size;
   vkdf_descriptor_set_buffer_update(res->ctx,
                                     res->pipelines.descr.obj_set,
                                     obj_ubo->buf,
                                     0, 1, &ubo_offset, &ubo_size, false, true);

   VkdfBuffer *material_ubo = vkdf_scene_get_material_ubo(res->scene);
   VkDeviceSize material_ubo_size = vkdf_scene_get_material_ubo_size(res->scene);
   ubo_offset = 0;
   ubo_size = material_ubo_size;
   vkdf_descriptor_set_buffer_update(res->ctx,
                                     res->pipelines.descr.obj_set,
                                     material_ubo->buf,
                                     1, 1, &ubo_offset, &ubo_size, false, true);

   // Dynamic objects descriptor
   res->pipelines.descr.dyn_obj_set =
      create_descriptor_set(res->ctx,
                            res->descriptor_pool.static_ubo_pool,
                            res->pipelines.descr.obj_layout);

   obj_ubo = vkdf_scene_get_dynamic_object_ubo(res->scene);
   obj_ubo_size = vkdf_scene_get_dynamic_object_ubo_size(res->scene);
   ubo_offset = 0;
   ubo_size = obj_ubo_size;
   vkdf_descriptor_set_buffer_update(res->ctx,
                                     res->pipelines.descr.dyn_obj_set,
                                     obj_ubo->buf,
                                     0, 1, &ubo_offset, &ubo_size, false, true);

   material_ubo = vkdf_scene_get_dynamic_material_ubo(res->scene);
   material_ubo_size = vkdf_scene_get_dynamic_material_ubo_size(res->scene);
   ubo_offset = 0;
   ubo_size = material_ubo_size;
   vkdf_descriptor_set_buffer_update(res->ctx,
                                     res->pipelines.descr.dyn_obj_set,
                                     material_ubo->buf,
                                     1, 1, &ubo_offset, &ubo_size, false, true);

   // Lihgts descriptor
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


   // Shadow map data descriptor
   vkdf_scene_get_shadow_map_ubo_range(res->scene, &ubo_offset, &ubo_size);
   vkdf_descriptor_set_buffer_update(res->ctx,
                                     res->pipelines.descr.light_set,
                                     light_ubo->buf,
                                     1, 1, &ubo_offset, &ubo_size, false, true);

   // Shadow map sampler descriptors
   res->pipelines.descr.shadow_map_sampler_set =
      create_descriptor_set(res->ctx,
                            res->descriptor_pool.sampler_pool,
                            res->pipelines.descr.shadow_map_sampler_layout);

   for (uint32_t i = 0; i < NUM_LIGHTS; i++) {
      VkSampler shadow_map_sampler =
         vkdf_scene_light_get_shadow_map_sampler(res->scene, i);

      VkdfImage *shadow_map_image =
         vkdf_scene_light_get_shadow_map_image(res->scene, i);

      vkdf_descriptor_set_sampler_update(res->ctx,
                                         res->pipelines.descr.shadow_map_sampler_set,
                                         shadow_map_sampler,
                                         shadow_map_image->view,
                                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                         i, 1);
   }
}

static void
init_obj_pipeline(SceneResources *res, bool dynamic)
{
   VkVertexInputBindingDescription vi_bindings[1];
   VkVertexInputAttributeDescription vi_attribs[3];

   // Vertex attribute binding 0: position, normal, material
   uint32_t stride =
      vkdf_mesh_get_vertex_data_stride(res->cube_mesh);
   vkdf_vertex_binding_set(&vi_bindings[0],
                           0, VK_VERTEX_INPUT_RATE_VERTEX, stride);

   assert(vkdf_mesh_get_vertex_data_stride(res->tree_model->meshes[0]) ==
          vi_bindings[0].stride);
   assert(vkdf_mesh_get_vertex_data_stride(res->tree_model->meshes[1]) ==
          vi_bindings[0].stride);

   /* binding 0, location 0: position
    * binding 0, location 1: normal
    * binding 0, location 2: material
    */
   vkdf_vertex_attrib_set(&vi_attribs[0], 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0);
   vkdf_vertex_attrib_set(&vi_attribs[1], 0, 1, VK_FORMAT_R32G32B32_SFLOAT, 12);
   vkdf_vertex_attrib_set(&vi_attribs[2], 0, 2, VK_FORMAT_R32_UINT, 24);

   VkPipeline *pipeline = dynamic ?
         &res->pipelines.obj.dynamic_pipeline :
         &res->pipelines.obj.static_pipeline;

   VkRenderPass renderpass = dynamic ?
         vkdf_scene_get_dynamic_render_pass(res->scene) :
         vkdf_scene_get_static_render_pass(res->scene);

   *pipeline =
      vkdf_create_gfx_pipeline(res->ctx,
                               NULL,
                               1,
                               vi_bindings,
                               3,
                               vi_attribs,
                               true,
                               VK_COMPARE_OP_LESS,
                               renderpass,
                               res->pipelines.layout.common,
                               VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                               VK_CULL_MODE_BACK_BIT,
                               1,
                               res->shaders.obj.vs,
                               res->shaders.obj.fs);
}

static void
init_floor_pipeline(SceneResources *res, bool init_cache)
{
   VkVertexInputBindingDescription vi_bindings[1];
   VkVertexInputAttributeDescription vi_attribs[3];

   // Vertex attribute binding 0: position, normal, material
   uint32_t stride =
      vkdf_mesh_get_vertex_data_stride(res->floor_mesh);
   vkdf_vertex_binding_set(&vi_bindings[0],
                           0, VK_VERTEX_INPUT_RATE_VERTEX, stride);

   /* binding 0, location 0: position
    * binding 0, location 1: normal
    * binding 0, location 2: material
    */
   vkdf_vertex_attrib_set(&vi_attribs[0], 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0);
   vkdf_vertex_attrib_set(&vi_attribs[1], 0, 1, VK_FORMAT_R32G32B32_SFLOAT, 12);
   vkdf_vertex_attrib_set(&vi_attribs[2], 0, 2, VK_FORMAT_R32_UINT, 24);

   res->pipelines.floor.pipeline =
      vkdf_create_gfx_pipeline(res->ctx,
                               NULL,
                               1,
                               vi_bindings,
                               3,
                               vi_attribs,
                               true,
                               VK_COMPARE_OP_LESS,
                               vkdf_scene_get_static_render_pass(res->scene),
                               res->pipelines.layout.common,
                               VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                               VK_CULL_MODE_BACK_BIT,
                               1,
                               res->shaders.floor.vs,
                               res->shaders.floor.fs);
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
   res->shaders.obj.vs = vkdf_create_shader_module(res->ctx, "obj.vert.spv");
   res->shaders.obj.fs = vkdf_create_shader_module(res->ctx, "obj.frag.spv");

   res->shaders.floor.vs =
      vkdf_create_shader_module(res->ctx, "floor.vert.spv");
   res->shaders.floor.fs =
      vkdf_create_shader_module(res->ctx, "floor.frag.spv");

   res->debug.shaders.vs =
      vkdf_create_shader_module(res->ctx, "debug-tile.vert.spv");
   res->debug.shaders.fs =
      vkdf_create_shader_module(res->ctx, "debug-tile.frag.spv");
}

static inline void
init_pipelines(SceneResources *res)
{
   init_pipeline_descriptors(res);
   init_obj_pipeline(res, false);
   init_obj_pipeline(res, true);
   init_floor_pipeline(res, true);
}

static void
init_meshes(SceneResources *res)
{
   // Cube
   VkdfMaterial red;
   red.diffuse = glm::vec4(0.80f, 0.15f, 0.15f, 1.0f);
   red.ambient = glm::vec4(0.80f, 0.15f, 0.15f, 1.0f);
   red.specular = glm::vec4(1.0f, 0.75f, 0.75f, 1.0f);
   red.shininess = 8.0f;

   VkdfMaterial green;
   green.diffuse = glm::vec4(0.15f, 0.80f, 0.15f, 1.0f);
   green.ambient = glm::vec4(0.15f, 0.80f, 0.15f, 1.0f);
   green.specular = glm::vec4(0.75f, 1.0f, 0.75f, 1.0f);
   green.shininess = 8.0f;

   VkdfMaterial blue;
   blue.diffuse = glm::vec4(0.15f, 0.15f, 0.80f, 1.0f);
   blue.ambient = glm::vec4(0.15f, 0.15f, 0.80f, 1.0f);
   blue.specular = glm::vec4(0.75f, 0.75f, 1.0f, 1.0f);
   blue.shininess = 8.0f;

   VkdfMaterial white;
   white.diffuse = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
   white.ambient = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
   white.specular = glm::vec4(0.75f, 0.75f, 1.0f, 1.0f);
   white.shininess = 8.0f;

   VkdfMaterial yellow;
   yellow.diffuse = glm::vec4(0.7f, 0.7f, 0.15f, 1.0f);
   yellow.ambient = glm::vec4(0.7f, 0.7f, 0.15f, 1.0f);
   yellow.specular = glm::vec4(0.75f, 0.75f, 1.0f, 1.0f);
   yellow.shininess = 8.0f;

   res->cube_mesh = vkdf_cube_mesh_new(res->ctx);
   res->cube_mesh->material_idx = 0;
   vkdf_mesh_fill_vertex_buffer(res->ctx, res->cube_mesh);

   res->cube_model = vkdf_model_new();
   vkdf_model_add_mesh(res->cube_model, res->cube_mesh);
   vkdf_model_compute_box(res->cube_model);

   vkdf_model_add_material(res->cube_model, &red);
   vkdf_model_add_material(res->cube_model, &green);
   vkdf_model_add_material(res->cube_model, &blue);
   vkdf_model_add_material(res->cube_model, &white);

   // Floor
   VkdfMaterial grey1;
   grey1.diffuse = glm::vec4(0.75f, 0.75f, 0.75f, 1.0f);
   grey1.ambient = glm::vec4(0.75f, 0.75f, 0.75f, 1.0f);
   grey1.specular = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
   grey1.shininess = 4.0f;

   VkdfMaterial grey2;
   grey2.diffuse = glm::vec4(0.25f, 0.25f, 0.25f, 1.0f);
   grey2.ambient = glm::vec4(0.25f, 0.25f, 0.25f, 1.0f);
   grey2.specular = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
   grey2.shininess = 4.0f;

   res->floor_mesh = vkdf_cube_mesh_new(res->ctx);
   res->floor_mesh->material_idx = 0;
   vkdf_mesh_fill_vertex_buffer(res->ctx, res->floor_mesh);

   res->floor_model = vkdf_model_new();
   vkdf_model_add_mesh(res->floor_model, res->floor_mesh);
   vkdf_model_compute_box(res->floor_model);

   vkdf_model_add_material(res->floor_model, &grey1);
   vkdf_model_add_material(res->floor_model, &grey2);

   // Tree
   res->tree_model = vkdf_model_load("./tree.obj");
   vkdf_model_fill_vertex_buffers(res->ctx, res->tree_model, true);

   /* Add another set of materials so we can have a tree variant */
   vkdf_model_add_material(res->tree_model, &white);
   vkdf_model_add_material(res->tree_model, &red);
   vkdf_model_add_material(res->tree_model, &yellow);

   // Debug tile
   res->tile_mesh = vkdf_2d_tile_mesh_new(res->ctx);
   vkdf_mesh_fill_vertex_buffer(res->ctx, res->tile_mesh);

}

static void
init_objects(SceneResources *res)
{
   // Cubes
   glm::vec3 pos = glm::vec3(0.0f, 3.0f, 0.0f);
   VkdfObject *obj = vkdf_object_new_from_model(pos, res->cube_model);
   vkdf_object_set_scale(obj, glm::vec3(2.0f, 3.0f, 2.0f));
   vkdf_object_set_lighting_behavior(obj, true, true);
   vkdf_object_set_material_idx_base(obj, 0);
   vkdf_scene_add_object(res->scene, "cube", obj);

   pos = glm::vec3(0.0f, 1.0f, -12.0f);
   obj = vkdf_object_new_from_model(pos, res->cube_model);
   vkdf_object_set_lighting_behavior(obj, true, true);
   vkdf_object_set_scale(obj, glm::vec3(3.0f, 1.0f, 3.0f));
   vkdf_object_set_material_idx_base(obj, 1);
   vkdf_scene_add_object(res->scene, "cube", obj);

   pos = glm::vec3(-12.0f, 2.0f, -5.0f);
   obj = vkdf_object_new_from_model(pos, res->cube_model);
   vkdf_object_set_lighting_behavior(obj, true, true);
   vkdf_object_set_rotation(obj, glm::vec3(0.0f, 45.0f, 0.0f));
   vkdf_object_set_scale(obj, glm::vec3(3.0f, 2.0f, 2.0f));
   vkdf_object_set_material_idx_base(obj, 2);
   vkdf_scene_add_object(res->scene, "cube", obj);

   pos = glm::vec3(0.0f, 10.0f, 10.0f);
   obj = vkdf_object_new_from_model(pos, res->cube_model);
   vkdf_object_set_scale(obj, glm::vec3(20.0f, 10.0f, 1.0f));
   vkdf_object_set_lighting_behavior(obj, true, true);
   vkdf_object_set_material_idx_base(obj, 3);
   vkdf_scene_add_object(res->scene, "cube", obj);

   // Dynamic cube
   pos = glm::vec3(0.0f, 8.0f, 6.0f);
   obj = vkdf_object_new_from_model(pos, res->cube_model);
   vkdf_object_set_rotation(obj, glm::vec3(45.0f, 45.0f, 45.0f));
   vkdf_object_set_lighting_behavior(obj, true, true);
   vkdf_object_set_material_idx_base(obj, 0);
   vkdf_object_set_dynamic(obj, true);
   vkdf_scene_add_object(res->scene, "dyn-cube", obj);

   // Trees
   pos = glm::vec3(5.0f, 3.0f, -5.0f);
   obj = vkdf_object_new_from_model(pos, res->tree_model);
   vkdf_object_set_lighting_behavior(obj, true, true);
   vkdf_object_set_scale(obj, glm::vec3(2.0f, 2.0f, 2.0f));
   vkdf_object_set_material_idx_base(obj, 0);
   vkdf_scene_add_object(res->scene, "tree", obj);

   pos = glm::vec3(-5.0f, 5.0f, 4.0f);
   obj = vkdf_object_new_from_model(pos, res->tree_model);
   vkdf_object_set_lighting_behavior(obj, true, true);
   vkdf_object_set_scale(obj, glm::vec3(3.0f, 3.0f, 3.0f));
   vkdf_object_set_material_idx_base(obj, 3);
   vkdf_scene_add_object(res->scene, "tree", obj);

   // Floor
   // FIXME: this should be handled in untiled-mode, maybe we should do that
   // automatically for any object that is too big or something...
   pos = glm::vec3(0.0f, 0.0f - 0.1f / 2.0f, 0.0f);
   VkdfObject *floor = vkdf_object_new_from_model(pos, res->floor_model);
   vkdf_object_set_scale(floor, glm::vec3(res->scene->scene_area.w / 2.0f,
                                          0.1f,
                                          res->scene->scene_area.d / 2.0f));
   vkdf_object_set_lighting_behavior(floor, false, true);
   vkdf_scene_add_object(res->scene, "floor", floor);
   vkdf_object_set_material_idx_base(floor, 0);

   VkClearValue color_clear;
   vkdf_color_clear_set(&color_clear, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));

   VkClearValue depth_clear;
   vkdf_depth_stencil_clear_set(&depth_clear, 1.0f, 0);

   vkdf_scene_set_clear_values(res->scene, &color_clear, &depth_clear);

   vkdf_scene_prepare(res->scene);
}

static void
init_lights(SceneResources *res)
{
   // Light 0
   uint32_t idx = 0;
   glm::vec4 origin = glm::vec4(10.0f, 10.0f, -5.0f, 2.0f);
   glm::vec4 diffuse = glm::vec4(0.25f, 1.0f, 0.25f, 0.0f);
   glm::vec4 ambient = glm::vec4(0.01f, 0.04f, 0.01f, 1.0f);
   glm::vec4 specular = glm::vec4(0.7f, 1.0f, 0.7f, 0.0f);
   glm::vec4 attenuation = glm::vec4(0.1f, 0.05f, 0.005f, 0.0f);
   float cutoff_angle = DEG_TO_RAD(45.0f);

   res->lights[idx] =
      vkdf_light_new_spotlight(origin, cutoff_angle,
                               diffuse, ambient, specular,
                               attenuation);

   vkdf_light_look_at(res->lights[idx], glm::vec3(0.0f, 0.0f, 0.0f));

   VkdfSceneShadowSpec shadow_spec;
   vkdf_scene_shadow_spec_set(&shadow_spec,
                              1024, 0.1f, 100.0f, 4.0f, 1.5f,
                              0.0f, glm::vec3(0.0f), 2);

   vkdf_scene_add_light(res->scene, res->lights[idx], &shadow_spec);

   // Light 1
   idx++;
   origin = glm::vec4(-15.0f, 5.0f, -30.0f, 2.0f);
   diffuse = glm::vec4(1.0f, 0.25f, 0.25f, 0.0f);
   ambient = glm::vec4(0.04f, 0.01f, 0.01f, 1.0f);
   specular = glm::vec4(1.0f, 0.7f, 0.7f, 0.0f);
   attenuation = glm::vec4(0.1f, 0.05f, 0.005f, 0.0f);
   cutoff_angle = DEG_TO_RAD(25.0f);

   res->lights[idx] =
      vkdf_light_new_spotlight(origin, cutoff_angle,
                               diffuse, ambient, specular,
                               attenuation);

   vkdf_light_look_at(res->lights[idx], glm::vec3(0.0f, 0.0f, 10.0f));

   vkdf_scene_shadow_spec_set(&shadow_spec, 1024, 0.1f, 100.0f, 4.0f, 1.5f,
                              0.0f, glm::vec3(0.0f), 2);

   vkdf_scene_add_light(res->scene, res->lights[idx], &shadow_spec);
}

static void
init_descriptor_pools(SceneResources *res)
{
   res->descriptor_pool.static_ubo_pool =
      vkdf_create_descriptor_pool(res->ctx,
                                  VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 8);
   res->descriptor_pool.sampler_pool =
      vkdf_create_descriptor_pool(res->ctx,
                                  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8);
}

static void
create_debug_tile_pipeline(SceneResources *res)
{
   // Sampler binding (for the first light's shadow map)
   res->debug.pipeline.sampler_set_layout =
      vkdf_create_sampler_descriptor_set_layout(res->ctx,
                                                0, 1,
                                                VK_SHADER_STAGE_FRAGMENT_BIT);

   res->debug.pipeline.sampler_set =
      create_descriptor_set(res->ctx,
                            res->descriptor_pool.sampler_pool,
                            res->debug.pipeline.sampler_set_layout);

   // FIXME: only showing the first light in the scene
   VkSampler shadow_map_sampler =
      vkdf_scene_light_get_shadow_map_sampler(res->scene, debug_light_idx);

    VkdfImage *shadow_map_image =
      vkdf_scene_light_get_shadow_map_image(res->scene, debug_light_idx);


   vkdf_descriptor_set_sampler_update(res->ctx,
                                      res->debug.pipeline.sampler_set,
                                      shadow_map_sampler,
                                      shadow_map_image->view,
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

   // Vertex attribute binding 0: position, uv
   uint32_t stride = vkdf_mesh_get_vertex_data_stride(res->tile_mesh);
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
   uint32_t width = res->ctx->width / 3;
   uint32_t height = res->ctx->height / 3;

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

   VkdfImage *color_image = vkdf_scene_get_color_render_target(res->scene);

   attachments[0].format = color_image->format;
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

   init_scene(res);
   init_lights(res);
   init_meshes(res);
   init_objects(res);
   init_ubos(res);
   init_shaders(res);
   init_descriptor_pools(res);
   init_pipelines(res);
   init_cmd_bufs(res);
   init_debug_tile_resources(res);
}

static VkdfImage
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

   // Present from scene framebuffer
   return res->scene->rt.color;
}

static void
destroy_models(SceneResources *res)
{
   vkdf_model_free(res->ctx, res->cube_model);
   vkdf_model_free(res->ctx, res->floor_model);
   vkdf_model_free(res->ctx, res->tree_model);
   vkdf_mesh_free(res->ctx, res->tile_mesh);
}

static void
destroy_cmd_bufs(SceneResources *res)
{
   vkDestroyCommandPool(res->ctx->device, res->cmd_pool, NULL);
}

static void
destroy_pipelines(SceneResources *res)
{
   vkDestroyPipeline(res->ctx->device, res->pipelines.obj.static_pipeline, NULL);
   vkDestroyPipeline(res->ctx->device, res->pipelines.obj.dynamic_pipeline, NULL);
   vkDestroyPipeline(res->ctx->device, res->pipelines.floor.pipeline, NULL);

   vkDestroyPipelineLayout(res->ctx->device, res->pipelines.layout.common, NULL);

   vkFreeDescriptorSets(res->ctx->device,
                        res->descriptor_pool.static_ubo_pool,
                        1, &res->pipelines.descr.camera_view_set);
   vkDestroyDescriptorSetLayout(res->ctx->device,
                                res->pipelines.descr.camera_view_layout, NULL);

   vkFreeDescriptorSets(res->ctx->device,
                        res->descriptor_pool.static_ubo_pool,
                        1, &res->pipelines.descr.obj_set);
   vkFreeDescriptorSets(res->ctx->device,
                        res->descriptor_pool.static_ubo_pool,
                        1, &res->pipelines.descr.dyn_obj_set);
   vkDestroyDescriptorSetLayout(res->ctx->device,
                                res->pipelines.descr.obj_layout, NULL);

   vkFreeDescriptorSets(res->ctx->device,
                        res->descriptor_pool.static_ubo_pool,
                        1, &res->pipelines.descr.light_set);
   vkDestroyDescriptorSetLayout(res->ctx->device,
                                res->pipelines.descr.light_layout, NULL);

   vkFreeDescriptorSets(res->ctx->device,
                        res->descriptor_pool.sampler_pool,
                        1, &res->pipelines.descr.shadow_map_sampler_set);
   vkDestroyDescriptorSetLayout(res->ctx->device,
                                res->pipelines.descr.shadow_map_sampler_layout,
                                NULL);

   vkDestroyDescriptorPool(res->ctx->device,
                           res->descriptor_pool.static_ubo_pool, NULL);
   vkDestroyDescriptorPool(res->ctx->device,
                           res->descriptor_pool.sampler_pool, NULL);

}

static void
destroy_shader_modules(SceneResources *res)
{
   vkDestroyShaderModule(res->ctx->device, res->shaders.obj.vs, NULL);
   vkDestroyShaderModule(res->ctx->device, res->shaders.obj.fs, NULL);
   vkDestroyShaderModule(res->ctx->device, res->shaders.floor.vs, NULL);
   vkDestroyShaderModule(res->ctx->device, res->shaders.floor.fs, NULL);
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

void
cleanup_resources(SceneResources *res)
{
   vkdf_scene_free(res->scene);
   destroy_debug_tile_resources(res);
   destroy_models(res);
   destroy_cmd_bufs(res);
   destroy_shader_modules(res);
   destroy_pipelines(res);
   destroy_ubos(res);

   vkdf_camera_free(res->camera);
}

int
main()
{
   VkdfContext ctx;
   SceneResources resources;

   vkdf_init(&ctx, WIN_WIDTH, WIN_HEIGHT, false, false, false);
   init_resources(&ctx, &resources);

   vkdf_scene_event_loop_run(resources.scene);

   cleanup_resources(&resources);
   vkdf_cleanup(&ctx);

   return 0;
}
