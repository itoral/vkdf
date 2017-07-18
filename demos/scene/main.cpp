#include "vkdf.hpp"

const float WIN_WIDTH  = 800.0f;
const float WIN_HEIGHT = 600.0f;

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

   VkRenderPass render_pass;
   VkClearValue clear_values[2];

   VkFramebuffer framebuffer;

   struct {
      VkdfImage color;
      VkdfImage depth;
   } images;

   struct {
      VkDescriptorPool static_ubo_pool;
   } descriptor_pool;

   VkCommandPool cmd_pool;

   VkCommandBuffer *present_cmd_bufs;

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
      struct {
         VkdfBuffer buf;
         VkDeviceSize inst_size;          // Per-instance data size
         VkDeviceSize inst_total_size;    // Total instance data size
         VkDeviceSize mat_size;           // Material data size
         VkDeviceSize total_size;         // Total buffer size
      } obj;
   } ubos;

   struct {
      struct {
         VkShaderModule vs;
         VkShaderModule fs;
      } obj;
   } shaders;

   VkdfMaterial materials[6];

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
   // Instance data is packed in a std140 array so needs to be 16B aligned
   res->ubos.obj.inst_size = ALIGN(sizeof(glm::mat4) + sizeof(uint32_t), 16);
   uint32_t num_objects = vkdf_scene_get_num_objects(res->scene);
   res->ubos.obj.inst_total_size = num_objects * res->ubos.obj.inst_size;

   // Material data is packed in a std140 array so meeds to be 16B aligned
   res->ubos.obj.mat_size = 6 * sizeof(VkdfMaterial);
   assert(res->ubos.obj.mat_size % 16 == 0);

   res->ubos.obj.total_size =
      res->ubos.obj.inst_total_size + res->ubos.obj.mat_size;

   res->ubos.obj.buf = create_ubo(res->ctx,
                                  res->ubos.obj.total_size,
                                  0,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);


   // Pre-load instance data
   uint8_t *mem;
   VK_CHECK(vkMapMemory(res->ctx->device, res->ubos.obj.buf.mem,
                        0, VK_WHOLE_SIZE,
                        0, (void **) &mem));

   VkdfScene *s = res->scene;
   for (uint32_t i = 0; i < s->num_tiles.total; i++) {
      VkdfSceneTile *t = &s->tiles[i];
      if (t->obj_count == 0)
          continue;

      VkdfSceneSetInfo *info =
         (VkdfSceneSetInfo *) g_hash_table_lookup(t->sets, "cube");
      if (info && info->count > 0) {
         VkDeviceSize offset = info->start_index * res->ubos.obj.inst_size;
         GList *iter = info->objs;
         while (iter) {
            VkdfObject *obj = (VkdfObject *) iter->data;

            glm::mat4 model = vkdf_object_get_model_matrix(obj);
            float *model_data = glm::value_ptr(model);
            memcpy(mem + offset, model_data, sizeof(glm::mat4));
            offset += sizeof(glm::mat4);

            memcpy(mem + offset, &obj->material_idx_base, sizeof(uint32_t));
            offset += sizeof(uint32_t);

            offset = ALIGN(offset, 16);

            iter = g_list_next(iter);
         }
      }
   }

   if (!(res->ubos.obj.buf.mem_props & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
      VkMappedMemoryRange range;
      range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
      range.pNext = NULL;
      range.memory = res->ubos.obj.buf.mem;
      range.offset = 0;
      range.size = VK_WHOLE_SIZE;
      VK_CHECK(vkFlushMappedMemoryRanges(res->ctx->device, 1, &range));
   }

   vkUnmapMemory(res->ctx->device, res->ubos.obj.buf.mem);

   // Pre-load material data
   VkDeviceSize map_offset = res->ubos.obj.inst_total_size;
   VkDeviceSize map_size = res->ubos.obj.mat_size;
   VK_CHECK(vkMapMemory(res->ctx->device, res->ubos.obj.buf.mem,
                        map_offset, map_size,
                        0, (void **) &mem));

   memcpy(mem, res->materials, map_size);

   if (!(res->ubos.obj.buf.mem_props & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
      VkMappedMemoryRange range;
      range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
      range.pNext = NULL;
      range.memory = res->ubos.obj.buf.mem;
      range.offset = map_offset;
      range.size = map_size;
      VK_CHECK(vkFlushMappedMemoryRanges(res->ctx->device, 1, &range));
   }

   vkUnmapMemory(res->ctx->device, res->ubos.obj.buf.mem);

   // Camera view
   res->ubos.camera_view.size = sizeof(glm::mat4);
   res->ubos.camera_view.buf = create_ubo(res->ctx,
                                          res->ubos.camera_view.size,
                                          VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
}

static VkCommandBuffer
record_update_resources_command(VkdfContext *ctx,
                                VkCommandPool cmd_pool,
                                void *data)
{
   SceneResources *res = (SceneResources *) data;

   VkdfCamera *camera = vkdf_scene_get_camera(res->scene);
   if (!vkdf_camera_is_dirty(camera))
      return 0;

   // FIXME: maybe use a different pool that has the
   // VK_COMMAND_POOL_CREATE_TRANSIENT_BIT
   VkCommandBuffer cmd_buf;
   vkdf_create_command_buffer(ctx,
                              cmd_pool,
                              VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                              1, &cmd_buf);

   vkdf_command_buffer_begin(cmd_buf,
                             VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

   glm::mat4 view = vkdf_camera_get_view_matrix(res->camera);
   vkCmdUpdateBuffer(cmd_buf, res->ubos.camera_view.buf.buf,
                     0, sizeof(glm::mat4), &view[0][0]);

   vkdf_command_buffer_end(cmd_buf);

   return cmd_buf;
}

static void
record_render_pass_begin(VkdfContext *ctx,
                         VkRenderPassBeginInfo *rp_begin,
                         VkFramebuffer framebuffer,
                         uint32_t fb_width, uint32_t fb_height,
                         void *data)
{
   SceneResources *res = (SceneResources *) data;

   rp_begin->sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
   rp_begin->pNext = NULL;
   rp_begin->renderPass = res->render_pass;
   rp_begin->framebuffer = framebuffer;
   rp_begin->renderArea.offset.x = 0;
   rp_begin->renderArea.offset.y = 0;
   rp_begin->renderArea.extent.width = fb_width;
   rp_begin->renderArea.extent.height = fb_height;
   rp_begin->clearValueCount = 2;
   rp_begin->pClearValues = res->clear_values;
}

static VkCommandBuffer
record_scene_commands(VkdfContext *ctx,
                      VkCommandPool cmd_pool,
                      VkFramebuffer framebuffer,
                      uint32_t fb_width, uint32_t fb_height,
                      GHashTable *sets,
                      void *data)
{
   SceneResources *res = (SceneResources *) data;

   // Record command buffer
   VkCommandBuffer cmd_buf;
   vkdf_create_command_buffer(ctx,
                              cmd_pool,
                              VK_COMMAND_BUFFER_LEVEL_SECONDARY,
                              1, &cmd_buf);

   VkCommandBufferUsageFlags flags =
      VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT |
      VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;

   VkCommandBufferInheritanceInfo inheritance_info;
   inheritance_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
   inheritance_info.pNext = NULL;
   inheritance_info.renderPass = res->render_pass;
   inheritance_info.subpass = 0;
   inheritance_info.framebuffer = framebuffer;
   inheritance_info.occlusionQueryEnable = 0;
   inheritance_info.queryFlags = 0;
   inheritance_info.pipelineStatistics = 0;

   vkdf_command_buffer_begin_secondary(cmd_buf, flags, &inheritance_info);

   // Vieport aand scissor
   VkViewport viewport;
   viewport.width = fb_width;
   viewport.height = fb_height;
   viewport.minDepth = 0.0f;
   viewport.maxDepth = 1.0f;
   viewport.x = 0;
   viewport.y = 0;
   vkCmdSetViewport(cmd_buf, 0, 1, &viewport);

   VkRect2D scissor;
   scissor.extent.width = fb_width;
   scissor.extent.height = fb_height;
   scissor.offset.x = 0;
   scissor.offset.y = 0;
   vkCmdSetScissor(cmd_buf, 0, 1, &scissor);

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

   vkdf_command_buffer_end(cmd_buf);

   return cmd_buf;
}

static void
init_scene(SceneResources *res)
{
   VkdfContext *ctx = res->ctx;

   res->camera = vkdf_camera_new(0.0f, 0.0f, 0.0f,
                                 0.0f, 180.0f, 0.0f);
   // FIXME: we should pass the projection in the constructor
   vkdf_camera_set_projection(res->camera,
                              45.0f, 0.1f, 2000.0f, WIN_WIDTH / WIN_HEIGHT);

   res->images.color =
      vkdf_create_image(ctx,
                        ctx->width,
                        ctx->height,
                        1,
                        VK_IMAGE_TYPE_2D,
                        ctx->surface_format,
                        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT,
                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                           VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        VK_IMAGE_ASPECT_COLOR_BIT,
                        VK_IMAGE_VIEW_TYPE_2D);

   res->images.depth =
      vkdf_create_image(ctx,
                        ctx->width,
                        ctx->height,
                        1,
                        VK_IMAGE_TYPE_2D,
                        VK_FORMAT_D32_SFLOAT,
                        0,
                        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        VK_IMAGE_ASPECT_DEPTH_BIT,
                        VK_IMAGE_VIEW_TYPE_2D);

   res->render_pass =
      vkdf_renderpass_simple_new(ctx,
                                 res->images.color.format,
                                 res->images.depth.format);

   res->framebuffer =
      vkdf_create_framebuffer(ctx,
                              res->render_pass,
                              res->images.color.view,
                              ctx->width, ctx->height,
                              1, &res->images.depth);

   glm::vec3 scene_origin = glm::vec3(-500.0f, -500.0f, -500.0f);
   glm::vec3 scene_size = glm::vec3(1000.0f, 1000.0f, 1000.0f);
   glm::vec3 tile_size = glm::vec3(250.0f, 250.0f, 250.0f);
   uint32_t cache_size = 8;
   res->scene = vkdf_scene_new(ctx,
                               res->camera,
                               scene_origin, scene_size, tile_size, 2,
                               cache_size);

   vkdf_scene_set_render_target(res->scene, res->framebuffer, ctx->width, ctx->height);
   vkdf_scene_set_scene_callbacks(res->scene,
                                  record_update_resources_command,
                                  record_render_pass_begin,
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
                                        0, 1, &ubo_offset, &ubo_size, false);

      res->pipelines.obj.descr.obj_set =
         create_descriptor_set(res->ctx,
                               res->descriptor_pool.static_ubo_pool,
                               res->pipelines.obj.descr.obj_layout);

      ubo_offset = 0;
      ubo_size = res->ubos.obj.inst_total_size;
      vkdf_descriptor_set_buffer_update(res->ctx,
                                        res->pipelines.obj.descr.obj_set,
                                        res->ubos.obj.buf.buf,
                                        0, 1, &ubo_offset, &ubo_size, false);

      ubo_offset = res->ubos.obj.inst_total_size;
      ubo_size = res->ubos.obj.mat_size;
      vkdf_descriptor_set_buffer_update(res->ctx,
                                        res->pipelines.obj.descr.obj_set,
                                        res->ubos.obj.buf.buf,
                                        1, 1, &ubo_offset, &ubo_size, false);
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
   vi_bindings[0].binding = 0;
   vi_bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
   vi_bindings[0].stride = 2 * sizeof(glm::vec3);

   // binding 0, location 0: position
   vi_attribs[0].binding = 0;
   vi_attribs[0].location = 0;
   vi_attribs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
   vi_attribs[0].offset = 0;

   // binding 0, location 1: normal
   vi_attribs[1].binding = 0;
   vi_attribs[1].location = 1;
   vi_attribs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
   vi_attribs[1].offset = 12;

   res->pipelines.obj.pipeline =
      vkdf_create_gfx_pipeline(res->ctx,
                               &res->pipelines.obj.cache,
                               1,
                               vi_bindings,
                               2,
                               vi_attribs,
                               true,
                               res->render_pass,
                               res->pipelines.obj.layout,
                               VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                               VK_CULL_MODE_BACK_BIT,
                               res->shaders.obj.vs,
                               res->shaders.obj.fs);
}

static void
init_cmd_bufs(SceneResources *res)
{
   if (!res->cmd_pool)
      res->cmd_pool = vkdf_create_gfx_command_pool(res->ctx, 0);

   res->present_cmd_bufs =
      vkdf_command_buffer_create_for_present(res->ctx, res->cmd_pool,
                                             res->images.color.image);
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
   VkdfMaterial *m = res->materials;

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

   VkdfMaterial cyan;
   cyan.diffuse = glm::vec4(0.0f, 0.5f, 0.5f, 1.0f);
   cyan.ambient = glm::vec4(0.0f, 0.5f, 0.5f, 1.0f);
   cyan.specular = glm::vec4(0.0f, 1.0f, 1.0f, 1.0f);
   cyan.shininess = 48.0f;

   VkdfMaterial purple;
   purple.diffuse = glm::vec4(0.5f, 0.0f, 0.5f, 1.0f);
   purple.ambient = glm::vec4(0.5f, 0.0f, 0.5f, 1.0f);
   purple.specular = glm::vec4(1.0f, 0.0f, 1.0f, 1.0f);
   purple.shininess = 48.0f;

   assert(sizeof(res->materials) / sizeof(VkdfMaterial) == 6);

   m[0] = red;
   m[1] = green;
   m[2] = blue;
   m[3] = yellow;
   m[4] = cyan;
   m[5] = purple;

   res->cube_mesh = vkdf_cube_mesh_new(res->ctx);
   vkdf_mesh_fill_vertex_buffer(res->ctx, res->cube_mesh);

   res->cube_model = vkdf_model_new();
   vkdf_model_add_mesh(res->cube_model, res->cube_mesh);
   vkdf_model_compute_size(res->cube_model);

   vkdf_model_add_material(res->cube_model, &red);
   vkdf_model_add_material(res->cube_model, &green);
   vkdf_model_add_material(res->cube_model, &blue);
   vkdf_model_add_material(res->cube_model, &yellow);
   vkdf_model_add_material(res->cube_model, &cyan);
   vkdf_model_add_material(res->cube_model, &purple);
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
      uint32_t mat_idx = random() % 6;
      vkdf_object_set_material_idx_base(obj, mat_idx);
   }

   vkdf_scene_prepare(res->scene);
}

static void
init_clear_values(SceneResources *res)
{
   res->clear_values[0].color.float32[0] = 0.0f;
   res->clear_values[0].color.float32[1] = 0.0f;
   res->clear_values[0].color.float32[2] = 0.0f;
   res->clear_values[0].color.float32[3] = 1.0f;
   res->clear_values[1].depthStencil.depth = 1.0f;
   res->clear_values[1].depthStencil.stencil = 0;
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

   init_clear_values(res);
   init_scene(res);
   init_meshes(res);
   init_objects(res);
   init_ubos(res);
   init_shaders(res);
   init_descriptor_pools(res);
   init_pipelines(res);
   init_cmd_bufs(res);
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
scene_update(VkdfContext *ctx, void *data)
{
   SceneResources *res = (SceneResources *) data;

   update_camera(res); // FIXME: this should be a callback called from the scene
   vkdf_scene_update_cmd_bufs(res->scene, res->cmd_pool);
   vkdf_camera_set_dirty(res->camera, false); // FIXME: this should be done by the scene
}

static void
scene_render(VkdfContext *ctx, void *data)
{
   SceneResources *res = (SceneResources *) data;

   // Render
   VkSemaphore scene_draw_sem = vkdf_scene_draw(res->scene);

   // Present
   VkSemaphore present_wait_sems[2] = {
      ctx->acquired_sem[ctx->swap_chain_index],
      scene_draw_sem
   };
   VkPipelineStageFlags present_wait_stages[2] = {
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
   };
   vkdf_command_buffer_execute(ctx,
                               res->present_cmd_bufs[ctx->swap_chain_index],
                               present_wait_stages,
                               2, present_wait_sems,
                               1, &ctx->draw_sem[ctx->swap_chain_index]);
#if 0
   uint32_t total = 0;
   GList *iter = res->scene->visible;
   while (iter) {
      VkdfSceneTile *t = (VkdfSceneTile *) iter->data;
      total += t->obj_count;
      iter = g_list_next(iter);
   }
   printf("%d tiles VISIBLE (%d objs)\n", g_list_length(res->scene->visible), total);
#endif
}

static void
destroy_models(SceneResources *res)
{
   vkdf_model_free(res->ctx, res->cube_model);
}

static void
destroy_cmd_bufs(SceneResources *res)
{
   vkFreeCommandBuffers(res->ctx->device,
                        res->cmd_pool,
                        res->ctx->swap_chain_length,
                        res->present_cmd_bufs);
   g_free(res->present_cmd_bufs);

   vkDestroyCommandPool(res->ctx->device, res->cmd_pool, NULL);
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

   vkDestroyBuffer(res->ctx->device, res->ubos.obj.buf.buf, NULL);
   vkFreeMemory(res->ctx->device, res->ubos.obj.buf.mem, NULL);
}

static void
destroy_images(SceneResources *res)
{
   vkdf_destroy_image(res->ctx, &res->images.color);
   vkdf_destroy_image(res->ctx, &res->images.depth);
}

static void
destroy_framebuffers(SceneResources *res)
{
   vkDestroyFramebuffer(res->ctx->device, res->framebuffer, NULL);
}

static void
destroy_renderpasses(SceneResources *res)
{
   vkDestroyRenderPass(res->ctx->device, res->render_pass, NULL);
}

void
cleanup_resources(SceneResources *res)
{
   vkdf_scene_free(res->scene);
   destroy_images(res);
   destroy_models(res);
   destroy_cmd_bufs(res);
   destroy_shader_modules(res);
   destroy_pipelines(res);
   destroy_ubos(res);
   destroy_renderpasses(res);
   destroy_framebuffers(res);

   vkdf_camera_free(res->camera);
}

int
main()
{
   VkdfContext ctx;
   SceneResources resources;

   vkdf_init(&ctx, WIN_WIDTH, WIN_HEIGHT, false, false, false);
   init_resources(&ctx, &resources);

   vkdf_event_loop_run(&ctx, scene_update, scene_render, &resources);

   cleanup_resources(&resources);
   vkdf_cleanup(&ctx);

   return 0;
}
