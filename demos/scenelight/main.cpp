#include "vkdf.hpp"

const float WIN_WIDTH  = 800.0f;
const float WIN_HEIGHT = 600.0f;

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
   VkdfLight *light;

   VkRenderPass render_pass;
   VkClearValue clear_values[2];

   VkFramebuffer framebuffer;

   struct {
      VkdfImage color;
      VkdfImage depth;
   } images;

   struct {
      VkDescriptorPool static_ubo_pool;
      VkDescriptorPool sampler_pool;
   } descriptor_pool;

   VkCommandPool cmd_pool;

   VkCommandBuffer *present_cmd_bufs;

   struct {
      struct {
         VkDescriptorSetLayout camera_view_layout;
         VkDescriptorSet camera_view_set;
         VkDescriptorSetLayout obj_layout;
         VkDescriptorSet obj_set;
         VkDescriptorSetLayout light_layout;
         VkDescriptorSet light_set;
         VkDescriptorSetLayout shadow_map_sampler_layout;
         VkDescriptorSet shadow_map_sampler_set;
      } descr;

      struct {
         VkPipelineLayout common;
      } layout;

      struct {
         VkPipeline pipeline;
         VkPipelineCache cache;
      } obj;

      struct {
         VkPipeline pipeline;
         VkPipelineCache cache;
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
      VkSemaphore draw_sem;
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
   VkDeviceSize offset = 0;
   vkCmdUpdateBuffer(cmd_buf, res->ubos.camera_view.buf.buf,
                     offset, sizeof(glm::mat4), &view[0][0]);
   offset += sizeof(glm::mat4);

   glm::mat4 view_inv = glm::inverse(view);
   vkCmdUpdateBuffer(cmd_buf, res->ubos.camera_view.buf.buf,
                    offset, sizeof(glm::mat4), &view_inv[0][0]);

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

   // Vieport and scissor
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
      res->pipelines.descr.obj_set,
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

   VkdfSceneSetInfo *cube_info =
      (VkdfSceneSetInfo *) g_hash_table_lookup(sets, "cube");

   if (cube_info->count > 0) {
      VkdfModel *model = res->cube_model;

      // Pipeline
      vkCmdBindPipeline(cmd_buf,
                        VK_PIPELINE_BIND_POINT_GRAPHICS,
                        res->pipelines.obj.pipeline);

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

   VkdfSceneSetInfo *tree_info =
      (VkdfSceneSetInfo *) g_hash_table_lookup(sets, "tree");

   if (tree_info->count > 0) {
      VkdfModel *model = res->tree_model;

      // Pipeline
      vkCmdBindPipeline(cmd_buf,
                        VK_PIPELINE_BIND_POINT_GRAPHICS,
                        res->pipelines.obj.pipeline);

      for (uint32_t i = 0; i < model->meshes.size(); i++) {
         VkdfMesh *mesh = model->meshes[i];

         // Bind index buffer
         vkCmdBindIndexBuffer(cmd_buf,
                              mesh->index_buf.buf,      // Buffer
                              0,                        // Offset
                              VK_INDEX_TYPE_UINT32);    // Index type

         // Vertex buffers
         const VkDeviceSize offsets[1] = { 0 };
         vkCmdBindVertexBuffers(cmd_buf,
                                0,                         // Start Binding
                                1,                         // Binding Count
                                &mesh->vertex_buf.buf,     // Buffers
                                offsets);                  // Offsets

         // Draw
         vkCmdDrawIndexed(cmd_buf,
                          mesh->indices.size(),                // index count
                          tree_info->count,                    // instance count
                          0,                                   // first index
                          0,                                   // first vertex
                          tree_info->start_index);             // first instance
      }
   }

   VkdfSceneSetInfo *floor_info =
      (VkdfSceneSetInfo *) g_hash_table_lookup(sets, "floor");

   if (floor_info->count > 0) {
      VkdfModel *model = res->floor_model;

      // Pipeline
      vkCmdBindPipeline(cmd_buf,
                        VK_PIPELINE_BIND_POINT_GRAPHICS,
                        res->pipelines.floor.pipeline);

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
                   mesh->vertices.size(),                // vertex count
                   floor_info->count,                    // instance count
                   0,                                    // first vertex
                   floor_info->start_index);             // first instance
      }
   }

   vkdf_command_buffer_end(cmd_buf);

   return cmd_buf;
}

static void
init_scene(SceneResources *res)
{
   VkdfContext *ctx = res->ctx;

   res->camera = vkdf_camera_new(0.0f, 10.0f, -20.0f,
                                 0.0f, 180.0f, 0.0f);
   // FIXME: we should pass the projection in the constructor
   vkdf_camera_set_projection(res->camera,
                              45.0f, 0.1f, 500.0f, WIN_WIDTH / WIN_HEIGHT);
   vkdf_camera_look_at(res->camera, 0.0f, 0.0f, 0.0f);

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

   glm::vec3 scene_origin = glm::vec3(-50.0f, -50.0f, -50.0f);
   glm::vec3 scene_size = glm::vec3(100.0f, 100.0f, 100.0f);
   glm::vec3 tile_size = glm::vec3(25.0f, 25.0f, 25.0f);
   uint32_t cache_size = 32;
   res->scene = vkdf_scene_new(ctx,
                               res->camera,
                               scene_origin, scene_size, tile_size, 2,
                               cache_size, 1);

   vkdf_scene_set_render_target(res->scene, res->framebuffer, ctx->width, ctx->height);
   vkdf_scene_set_scene_callbacks(res->scene,
                                  record_update_resources_command,
                                  record_render_pass_begin,
                                  record_scene_commands,
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
      vkdf_create_sampler_descriptor_set_layout(res->ctx, 0, 1,
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

   res->pipelines.descr.camera_view_set =
      create_descriptor_set(res->ctx,
                            res->descriptor_pool.static_ubo_pool,
                            res->pipelines.descr.camera_view_layout);

   VkDeviceSize ubo_offset = 0;
   VkDeviceSize ubo_size = res->ubos.camera_view.size;
   vkdf_descriptor_set_buffer_update(res->ctx,
                                     res->pipelines.descr.camera_view_set,
                                     res->ubos.camera_view.buf.buf,
                                     0, 1, &ubo_offset, &ubo_size, false);

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
                                     0, 1, &ubo_offset, &ubo_size, false);

   VkdfBuffer *material_ubo = vkdf_scene_get_material_ubo(res->scene);
   VkDeviceSize material_ubo_size = vkdf_scene_get_material_ubo_size(res->scene);
   ubo_offset = 0;
   ubo_size = material_ubo_size;
   vkdf_descriptor_set_buffer_update(res->ctx,
                                     res->pipelines.descr.obj_set,
                                     material_ubo->buf,
                                     1, 1, &ubo_offset, &ubo_size, false);

   res->pipelines.descr.light_set =
      create_descriptor_set(res->ctx,
                            res->descriptor_pool.static_ubo_pool,
                            res->pipelines.descr.light_layout);

   VkdfBuffer *light_ubo = vkdf_scene_get_light_ubo(res->scene);
   uint32_t num_lights = vkdf_scene_get_num_lights(res->scene);
   ubo_offset = 0;
   ubo_size = num_lights * ALIGN(sizeof(VkdfLight), 16);
   vkdf_descriptor_set_buffer_update(res->ctx,
                                     res->pipelines.descr.light_set,
                                     light_ubo->buf,
                                     0, 1, &ubo_offset, &ubo_size, false);

   ubo_offset = ubo_size;
   ubo_size = num_lights * ALIGN(sizeof(glm::mat4), 16);
   vkdf_descriptor_set_buffer_update(res->ctx,
                                     res->pipelines.descr.light_set,
                                     light_ubo->buf,
                                     1, 1, &ubo_offset, &ubo_size, false);

   res->pipelines.descr.shadow_map_sampler_set =
      create_descriptor_set(res->ctx,
                            res->descriptor_pool.sampler_pool,
                            res->pipelines.descr.shadow_map_sampler_layout);

   // FIXME: only supporting a single light for now
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
}

static void
init_obj_pipeline(SceneResources *res, bool init_cache)
{
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
   VkVertexInputAttributeDescription vi_attribs[3];

   // Vertex attribute binding 0: position, normal, material
   vi_bindings[0].binding = 0;
   vi_bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
   vi_bindings[0].stride = vkdf_mesh_get_vertex_data_stride(res->cube_mesh);

   assert(vkdf_mesh_get_vertex_data_stride(res->tree_model->meshes[0]) ==
          vi_bindings[0].stride);
   assert(vkdf_mesh_get_vertex_data_stride(res->tree_model->meshes[1]) ==
          vi_bindings[0].stride);

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

   // binding 0, location 2: material
   vi_attribs[2].binding = 0;
   vi_attribs[2].location = 2;
   vi_attribs[2].format = VK_FORMAT_R32_UINT;
   vi_attribs[2].offset = 24;

   res->pipelines.obj.pipeline =
      vkdf_create_gfx_pipeline(res->ctx,
                               &res->pipelines.obj.cache,
                               1,
                               vi_bindings,
                               3,
                               vi_attribs,
                               true,
                               res->render_pass,
                               res->pipelines.layout.common,
                               VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                               VK_CULL_MODE_BACK_BIT,
                               res->shaders.obj.vs,
                               res->shaders.obj.fs);
}

static void
init_floor_pipeline(SceneResources *res, bool init_cache)
{
   if (init_cache) {
      VkPipelineCacheCreateInfo info;
      info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
      info.pNext = NULL;
      info.initialDataSize = 0;
      info.pInitialData = NULL;
      info.flags = 0;
      VK_CHECK(vkCreatePipelineCache(res->ctx->device, &info, NULL,
                                     &res->pipelines.floor.cache));
   }

   VkVertexInputBindingDescription vi_bindings[1];
   VkVertexInputAttributeDescription vi_attribs[3];

   // Vertex attribute binding 0: position, normal, material
   vi_bindings[0].binding = 0;
   vi_bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
   vi_bindings[0].stride = vkdf_mesh_get_vertex_data_stride(res->floor_mesh);

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

   // binding 0, location 2: material
   vi_attribs[2].binding = 0;
   vi_attribs[2].location = 2;
   vi_attribs[2].format = VK_FORMAT_R32_UINT;
   vi_attribs[2].offset = 24;

   res->pipelines.floor.pipeline =
      vkdf_create_gfx_pipeline(res->ctx,
                               &res->pipelines.floor.cache,
                               1,
                               vi_bindings,
                               3,
                               vi_attribs,
                               true,
                               res->render_pass,
                               res->pipelines.layout.common,
                               VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                               VK_CULL_MODE_BACK_BIT,
                               res->shaders.floor.vs,
                               res->shaders.floor.fs);
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
   init_obj_pipeline(res, true);
   init_floor_pipeline(res, true);
}

static void
init_meshes(SceneResources *res)
{
   // Cube
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

   VkdfMaterial white;
   white.diffuse = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
   white.ambient = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
   white.specular = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
   white.shininess = 48.0f;

   res->cube_mesh = vkdf_cube_mesh_new(res->ctx);
   res->cube_mesh->material_idx = 0;
   vkdf_mesh_fill_vertex_buffer(res->ctx, res->cube_mesh);

   res->cube_model = vkdf_model_new();
   vkdf_model_add_mesh(res->cube_model, res->cube_mesh);
   vkdf_model_compute_size(res->cube_model);

   vkdf_model_add_material(res->cube_model, &red);
   vkdf_model_add_material(res->cube_model, &green);
   vkdf_model_add_material(res->cube_model, &blue);
   vkdf_model_add_material(res->cube_model, &white);

   // Floor
   VkdfMaterial grey1;
   grey1.diffuse = glm::vec4(0.75f, 0.75f, 0.75f, 1.0f);
   grey1.ambient = glm::vec4(0.75f, 0.75f, 0.75f, 1.0f);
   grey1.specular = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
   grey1.shininess = 48.0f;

   VkdfMaterial grey2;
   grey2.diffuse = glm::vec4(0.25f, 0.25f, 0.25f, 1.0f);
   grey2.ambient = glm::vec4(0.25f, 0.25f, 0.25f, 1.0f);
   grey2.specular = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
   grey2.shininess = 48.0f;

   res->floor_mesh = vkdf_cube_mesh_new(res->ctx);
   res->floor_mesh->material_idx = 0;
   vkdf_mesh_fill_vertex_buffer(res->ctx, res->floor_mesh);

   res->floor_model = vkdf_model_new();
   vkdf_model_add_mesh(res->floor_model, res->floor_mesh);
   vkdf_model_compute_size(res->floor_model);

   vkdf_model_add_material(res->floor_model, &grey1);
   vkdf_model_add_material(res->floor_model, &grey2);

   // Tree
   res->tree_model = vkdf_model_load("./tree.obj");
   vkdf_model_fill_vertex_buffers(res->ctx, res->tree_model, true);

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

   // Tree
   pos = glm::vec3(5.0f, 3.0f, -5.0f);
   obj = vkdf_object_new_from_model(pos, res->tree_model);
   vkdf_object_set_lighting_behavior(obj, true, true);
   vkdf_object_set_scale(obj, glm::vec3(2.0f, 2.0f, 2.0f));
   vkdf_object_set_material_idx_base(obj, 0);
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

   vkdf_scene_prepare(res->scene);
}

static void
init_lights(SceneResources *res)
{
   glm::vec4 origin = glm::vec4(10.0f, 10.0f, 5.0f, 2.0f);
   glm::vec4 diffuse = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
   glm::vec4 ambient = glm::vec4(0.02f, 0.02f, 0.02f, 1.0f);
   glm::vec4 specular = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
   glm::vec4 attenuation = glm::vec4(0.1f, 0.05f, 0.01f, 0.0f);
   float cutoff_angle = DEG_TO_RAD(90.0f / 2.0f);

   res->light =
      vkdf_light_new_spotlight(origin, cutoff_angle,
                               diffuse, ambient, specular,
                               attenuation);

   vkdf_light_look_at(res->light, glm::vec3(0.0f, 0.0f, 0.0f));

   vkdf_light_enable_shadows(res->light, true);

   VkdfSceneShadowSpec shadow_spec;
   shadow_spec.shadow_map_near = 0.1f;
   shadow_spec.shadow_map_far = 100.0f;
   shadow_spec.shadow_map_size = 1024;
   shadow_spec.depth_bias_const_factor = 4.0f;
   shadow_spec.depth_bias_slope_factor = 1.5f;
   vkdf_scene_add_light(res->scene, res->light, &shadow_spec);
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
      vkdf_scene_light_get_shadow_map_sampler(res->scene, 0);

   VkdfImage *shadow_map_image =
      vkdf_scene_light_get_shadow_map_image(res->scene, 0);

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
   vi_binding[0].binding = 0;
   vi_binding[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
   vi_binding[0].stride = vkdf_mesh_get_vertex_data_stride(res->tile_mesh);

   // binding 0, location 0: position
   vi_attribs[0].binding = 0;
   vi_attribs[0].location = 0;
   vi_attribs[0].format = VK_FORMAT_R32G32_SFLOAT;
   vi_attribs[0].offset = 0;

   // binding 0, location 1: uv
   vi_attribs[1].binding = 0;
   vi_attribs[1].location = 1;
   vi_attribs[1].format = VK_FORMAT_R32G32_SFLOAT;
   vi_attribs[1].offset = 12;

   res->debug.pipeline.pipeline =
      vkdf_create_gfx_pipeline(res->ctx,
                               NULL,
                               1,
                               vi_binding,
                               2,
                               vi_attribs,
                               false,
                               res->debug.renderpass,
                               res->debug.pipeline.layout,
                               VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
                               VK_CULL_MODE_BACK_BIT,
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

   attachments[0].format = res->ctx->surface_format;
   attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
   attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
   attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
   attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
   attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
   attachments[0].initialLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
   attachments[0].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
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

   res->debug.framebuffer =
      vkdf_create_framebuffer(res->ctx,
                              res->debug.renderpass,
                              res->images.color.view,
                              res->ctx->width, res->ctx->height,
                              0, NULL);

   create_debug_tile_pipeline(res);

   record_debug_tile_cmd_buf(res);

   res->debug.draw_sem = vkdf_create_semaphore(res->ctx);
}

static void
init_resources(VkdfContext *ctx, SceneResources *res)
{
   memset(res, 0, sizeof(SceneResources));

   res->ctx = ctx;

   init_clear_values(res);
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

   // Render scene
   VkSemaphore scene_draw_sem = vkdf_scene_draw(res->scene);

   // Render debug tile
   VkPipelineStageFlags debug_tile_wait_stages =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
   vkdf_command_buffer_execute(ctx,
                               res->debug.cmd_buf,
                               &debug_tile_wait_stages,
                               1, &scene_draw_sem,
                               1, &res->debug.draw_sem);

   // Present
   VkSemaphore present_wait_sems[2] = {
      ctx->acquired_sem[ctx->swap_chain_index],
      res->debug.draw_sem
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

   vkDestroyPipelineCache(res->ctx->device, res->pipelines.floor.cache, NULL);
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

   vkDestroySemaphore(res->ctx->device, res->debug.draw_sem, NULL);
}

void
cleanup_resources(SceneResources *res)
{
   vkdf_scene_free(res->scene);
   destroy_debug_tile_resources(res);
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

   vkdf_init(&ctx, WIN_WIDTH, WIN_HEIGHT, false, false, ENABLE_DEBUG);
   init_resources(&ctx, &resources);

   vkdf_event_loop_run(&ctx, scene_update, scene_render, &resources);

   cleanup_resources(&resources);
   vkdf_cleanup(&ctx);

   return 0;
}
