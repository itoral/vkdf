#include "vkdf.hpp"

#include <stdlib.h>

#include <assimp/cimport.h>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

// ----------------------------------------------------------------------------
// Loads a 3D model from a file with multiple meshes, them sets up single
// per-vertex and per-instance buffers with vertex data from all meshes in
// the model, as well as single index buffer and renders it multiple times
// using instancing.
// ----------------------------------------------------------------------------

// WARNING: this must not be larger than the the size of the Model array in
// the vertex shader
#define NUM_OBJECTS 500

typedef struct {
   VkCommandPool cmd_pool;
   VkCommandBuffer *cmd_bufs;
   VkRenderPass render_pass;
   VkPipelineLayout pipeline_layout;
   VkPipeline pipeline;
   VkShaderModule vs_module;
   VkShaderModule fs_module;
   VkFramebuffer *framebuffers;
   VkdfImage depth_image;

   // Pool for UBO descriptor
   VkDescriptorPool ubo_pool;

   // UBOs for View/Projection and Model matrices
   VkdfBuffer VP_ubo;
   VkdfBuffer M_ubo;
   VkdfBuffer material_ubo;

   // Descriptor sets for UBO bindings
   VkDescriptorSetLayout set_layout;
   VkDescriptorSet descriptor_set;

   // View/Projection matrices
   glm::mat4 view;
   glm::mat4 projection;

   // Objects
   VkdfObject *objs[NUM_OBJECTS];
   VkdfModel *model;
   VkdfBuffer instance_buf;
} DemoResources;

static VkdfBuffer
create_ubo(VkdfContext *ctx, uint32_t size, uint32_t mem_props)
{
   VkdfBuffer buf =
      vkdf_create_buffer(ctx,
                         0,                                    // flags
                         size,                                 // size
                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,   // usage
                         mem_props);                           // memory props
   return buf;
}

static VkRenderPass
create_render_pass(VkdfContext *ctx, DemoResources *res)
{
   VkAttachmentDescription attachments[2];

   // Single color attachment
   attachments[0].format = ctx->surface_format;
   attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
   attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
   attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
   attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
   attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
   attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
   attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
   attachments[0].flags = 0;

   // Depth attachment
   attachments[1].format = res->depth_image.format;
   attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
   attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
   attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
   attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
   attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
   attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
   attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
   attachments[1].flags = 0;

   // Color and depth attachment references
   VkAttachmentReference color_reference;
   color_reference.attachment = 0;
   color_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

   VkAttachmentReference depth_reference;
   depth_reference.attachment = 1;
   depth_reference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

   // Subpass for rendering to color and depth attachments
   VkSubpassDescription subpass;
   subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
   subpass.flags = 0;
   subpass.inputAttachmentCount = 0;
   subpass.pInputAttachments = NULL;
   subpass.colorAttachmentCount = 1;
   subpass.pColorAttachments = &color_reference;
   subpass.pResolveAttachments = NULL;
   subpass.pDepthStencilAttachment = &depth_reference;
   subpass.preserveAttachmentCount = 0;
   subpass.pPreserveAttachments = NULL;

   // Create render pass
   VkRenderPassCreateInfo rp_info;
   rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
   rp_info.pNext = NULL;
   rp_info.attachmentCount = 2;
   rp_info.pAttachments = attachments;
   rp_info.subpassCount = 1;
   rp_info.pSubpasses = &subpass;
   rp_info.dependencyCount = 0;
   rp_info.pDependencies = NULL;
   rp_info.flags = 0;

   VkRenderPass render_pass;
   VkResult result =
      vkCreateRenderPass(ctx->device, &rp_info, NULL, &render_pass);
   if (result != VK_SUCCESS)
      vkdf_fatal("Failed to create render pass");

   return render_pass;
}

static void
render_pass_commands(VkdfContext *ctx, DemoResources *res, uint32_t index)
{
   VkClearValue clear_values[2];
   vkdf_color_clear_set(&clear_values[0], glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
   vkdf_depth_stencil_clear_set(&clear_values[1], 1.0f, 0);

   VkRenderPassBeginInfo rp_begin;
   rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
   rp_begin.pNext = NULL;
   rp_begin.renderPass = res->render_pass;
   rp_begin.framebuffer = res->framebuffers[index];
   rp_begin.renderArea.offset.x = 0;
   rp_begin.renderArea.offset.y = 0;
   rp_begin.renderArea.extent.width = ctx->width;
   rp_begin.renderArea.extent.height = ctx->height;
   rp_begin.clearValueCount = 2;
   rp_begin.pClearValues = clear_values;

   vkCmdBeginRenderPass(res->cmd_bufs[index],
                        &rp_begin,
                        VK_SUBPASS_CONTENTS_INLINE);

   // Viewport and Scissor
   VkViewport viewport;
   viewport.height = ctx->height;
   viewport.width = ctx->width;
   viewport.minDepth = 0.0f;
   viewport.maxDepth = 1.0f;
   viewport.x = 0;
   viewport.y = 0;
   vkCmdSetViewport(res->cmd_bufs[index], 0, 1, &viewport);

   VkRect2D scissor;
   scissor.extent.width = ctx->width;
   scissor.extent.height = ctx->height;
   scissor.offset.x = 0;
   scissor.offset.y = 0;
   vkCmdSetScissor(res->cmd_bufs[index], 0, 1, &scissor);

   // Pipeline
   vkCmdBindPipeline(res->cmd_bufs[index], VK_PIPELINE_BIND_POINT_GRAPHICS,
                     res->pipeline);

   // Bind static UBO descriptor set: MVP matrices and model materials
   vkCmdBindDescriptorSets(res->cmd_bufs[index],
                           VK_PIPELINE_BIND_POINT_GRAPHICS,
                           res->pipeline_layout,
                           0,                        // First decriptor set
                           1,                        // Descriptor set count
                           &res->descriptor_set,     // Descriptor sets
                           0,                        // Dynamic offset count
                           NULL);                    // Dynamic offsets

   // Render NUM_OBJECTS instances of each mesh of the model
   // We have a single vertex buffer for all per-vertex data with data for
   // all the meshes, the same for per-instance attributes and for the index
   // data, so we always bind the same buffers but update the offsets depending
   // on the mesh we are rendering.
   VkdfModel *model = res->model;
   for (uint32_t i = 0; i < res->model->meshes.size(); i++) {
      // Index buffer for this mesh
      vkCmdBindIndexBuffer(res->cmd_bufs[index],
                           model->index_buf.buf,               // Buffer
                           model->index_buf_offsets[i],        // Offset
                           VK_INDEX_TYPE_UINT32);              // Index type

      // Per-vertex attributes for this mesh
      vkCmdBindVertexBuffers(res->cmd_bufs[index],
                             0,                                // Start Binding
                             1,                                // Binding Count
                             &model->vertex_buf.buf,           // Buffers
                             &model->vertex_buf_offsets[i]);   // Offsets

      // Per-instance attributes for this mesh: we have a buffer with the
      // material index for each instance of each mesh, ordered by mesh.
      VkDeviceSize instance_buf_offset = i * NUM_OBJECTS * sizeof(uint32_t);
      vkCmdBindVertexBuffers(res->cmd_bufs[index],
                             1,                                // Start Binding
                             1,                                // Binding Count
                             &res->instance_buf.buf,           // Buffers
                             &instance_buf_offset);            // Offsets

      // Draw NUM_OBJECTS instances of this mesh
      vkCmdDrawIndexed(res->cmd_bufs[index],
                       model->meshes[i]->indices.size(),       // Index count
                       NUM_OBJECTS,                            // Instance count
                       0,                                      // First index
                       0,                                      // Vertex offset
                       0);                                     // First instance
   }

   vkCmdEndRenderPass(res->cmd_bufs[index]);
}

static VkPipelineLayout
create_pipeline_layout(VkdfContext *ctx,
                       VkDescriptorSetLayout set_layout)
{
   VkPipelineLayoutCreateInfo pipeline_layout_info;
   pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
   pipeline_layout_info.pNext = NULL;
   pipeline_layout_info.pushConstantRangeCount = 0;
   pipeline_layout_info.pPushConstantRanges = NULL;
   pipeline_layout_info.setLayoutCount = 1;
   pipeline_layout_info.pSetLayouts = &set_layout;
   pipeline_layout_info.flags = 0;

   VkPipelineLayout pipeline_layout;
   VkResult res = vkCreatePipelineLayout(ctx->device,
                                         &pipeline_layout_info,
                                         NULL,
                                         &pipeline_layout);
   if (res != VK_SUCCESS)
      vkdf_fatal("Failed to create pipeline layout");

   return pipeline_layout;
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
   VkResult res = vkAllocateDescriptorSets(ctx->device, alloc_info, &set);

   if (res != VK_SUCCESS)
      vkdf_fatal("Failed to allocate descriptor set");

   return set;
}

static void
init_matrices(DemoResources *res)
{
   glm::mat4 clip = glm::mat4(1.0f, 0.0f, 0.0f, 0.0f,
                              0.0f,-1.0f, 0.0f, 0.0f,
                              0.0f, 0.0f, 0.5f, 0.0f,
                              0.0f, 0.0f, 0.5f, 1.0f);

   res->projection =
      clip * glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 100.0f);

   res->view = glm::lookAt(glm::vec3( 0,  0, -15),  // Camera position
                           glm::vec3( 0,  0,  0),   // Looking at origin
                           glm::vec3( 0,  1,  0));  // Up direction
}

static void
init_models(VkdfContext *ctx, DemoResources *res)
{
   res->model = vkdf_model_load("./data/tree.obj");

   // Create per-vertex and index buffers for this model. Make it so we have
   // a single buffer for the entire model that packs data from all meshes
   // (instead of having a different vertex/index buffer per mesh). This way,
   // when we render the meshes we do not have to bind a different buffer
   // for each one, instead we simply update the byte offset into the buffer
   // where the mesh's data is stored.
   vkdf_model_fill_vertex_buffers(ctx, res->model, false);
}

static void
init_objects(VkdfContext *ctx, DemoResources *res)
{
   const VkdfModel *model = res->model;

   // Create objects
   glm::vec3 start_pos = glm::vec3(-10.0f, -1.0f, -8.0f);
   glm::vec3 pos = start_pos;
   for (int i = 0; i < NUM_OBJECTS; i++) {
      uint32_t col = i % 10;
      res->objs[i] = vkdf_object_new(pos, res->model);
      vkdf_object_set_scale(res->objs[i], glm::vec3(0.25f, 0.25f, 0.25f));

      if (col < 9) {
         pos.x += 2.0f;
      } else {
         pos.x = -10.0f;
         pos.z += 4.0f;
      }
   }

   // Prepare per-instance vertex buffer with material indices for each
   // mesh instance. The first NUM_OBJECTS indices belong to the materials
   // for each instance of the first mesh, then we have the next NUM_OBJECTS
   // material indices for the second mesh, etc
   VkDeviceSize instance_data_size =
      sizeof(uint32_t) * model->meshes.size() * NUM_OBJECTS;
   res->instance_buf =
      vkdf_create_buffer(ctx,
                         0,
                         instance_data_size,
                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

   uint32_t *map;
   vkdf_memory_map(ctx, res->instance_buf.mem, 0, instance_data_size,
                   (void **) &map);

   for (uint32_t j = 0; j < model->meshes.size(); j++) {
      assert(model->meshes[j]->material_idx >= 0 &&
             model->meshes[j]->material_idx < (int32_t) model->materials.size());
      for (uint32_t i = 0; i < NUM_OBJECTS; i++) {
         *map = model->meshes[j]->material_idx;
         map++;
      }
   }

   vkdf_memory_unmap(ctx, res->instance_buf.mem, res->instance_buf.mem_props,
                     0, instance_data_size);
}

static void
fill_model_ubo(VkdfContext *ctx, DemoResources *res)
{
   uint8_t *map;
   vkdf_memory_map(ctx, res->M_ubo.mem, 0, VK_WHOLE_SIZE, (void**) &map);

   for (uint32_t i = 0; i < NUM_OBJECTS; i++) {
      VkdfObject *obj = res->objs[i];
      glm::mat4 Model = vkdf_object_get_model_matrix(obj);
      memcpy(map, &Model[0][0], sizeof(glm::mat4));
      map += sizeof(glm::mat4);
   }

   vkdf_memory_unmap(ctx, res->M_ubo.mem, res->M_ubo.mem_props,
                     0, VK_WHOLE_SIZE);
}

static void
init_resources(VkdfContext *ctx, DemoResources *res)
{
   memset(res, 0, sizeof(DemoResources));

   // Compute View, Projection and Cliip matrices
   init_matrices(res);

   // Load models
   init_models(ctx, res);

   // Create the object and its mesh
   init_objects(ctx, res);

   // Create UBO for View and Projection matrices
   res->VP_ubo = create_ubo(ctx, 2 * sizeof(glm::mat4),
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

   vkdf_buffer_map_and_fill(ctx, res->VP_ubo,
                            0, sizeof(glm::mat4),
                            &res->view[0][0]);

   vkdf_buffer_map_and_fill(ctx, res->VP_ubo,
                            sizeof(glm::mat4), sizeof(glm::mat4),
                            &res->projection[0][0]);

   // Create UBO for Model matrix
   res->M_ubo = create_ubo(ctx, NUM_OBJECTS * sizeof(glm::mat4),
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

   fill_model_ubo(ctx, res);

   // Create UBO for material

   // The code below assumes a packed 16-byte aligned array of materials to
   // comply with std140 rules
   assert(sizeof(VkdfMaterial) % 16 == 0);

   float materials_size = sizeof(VkdfMaterial) * res->model->materials.size();
   res->material_ubo = create_ubo(ctx, materials_size,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

   vkdf_buffer_map_and_fill(ctx, res->material_ubo,
                            0, materials_size,
                            &res->model->materials[0]);

   // Create depth image
   res->depth_image =
      vkdf_create_image(ctx,
                        ctx->width,
                        ctx->height,
                        1,
                        VK_IMAGE_TYPE_2D,
                        VK_FORMAT_D16_UNORM,
                        0,
                        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        VK_IMAGE_ASPECT_DEPTH_BIT,
                        VK_IMAGE_VIEW_TYPE_2D);

   // Shaders
   res->vs_module = vkdf_create_shader_module(ctx, "shader.vert.spv");
   res->fs_module = vkdf_create_shader_module(ctx, "shader.frag.spv");

   // Render pass
   res->render_pass = create_render_pass(ctx, res);

   // Framebuffers
   res->framebuffers =
      vkdf_create_framebuffers_for_swap_chain(ctx, res->render_pass,
                                              1, &res->depth_image);

   // Descriptor pool
   res->ubo_pool =
      vkdf_create_descriptor_pool(ctx, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3);

   // Descriptor set for UBO
   res->set_layout =
      vkdf_create_ubo_descriptor_set_layout(ctx, 0, 3,
                                            VK_SHADER_STAGE_VERTEX_BIT |
                                            VK_SHADER_STAGE_FRAGMENT_BIT,
                                            false);

   res->descriptor_set =
      create_descriptor_set(ctx, res->ubo_pool, res->set_layout);

   // Map View and Projection UBOs to set binding 0
   VkDeviceSize VP_offset = 0;
   VkDeviceSize VP_size = 2 * sizeof(glm::mat4);
   vkdf_descriptor_set_buffer_update(ctx, res->descriptor_set,
                                     res->VP_ubo.buf,
                                     0, 1, &VP_offset, &VP_size, false, true);

   // Map Model UBO to set binding 1
   VkDeviceSize M_offset = 0;
   VkDeviceSize M_size = NUM_OBJECTS * sizeof(glm::mat4);
   vkdf_descriptor_set_buffer_update(ctx, res->descriptor_set,
                                     res->M_ubo.buf,
                                     1, 1, &M_offset, &M_size, false, true);

   // Map model material UBOs to set binding 2
   VkDeviceSize material_offset = 0;
   VkDeviceSize material_size = sizeof(VkdfMaterial) * res->model->materials.size();
   vkdf_descriptor_set_buffer_update(ctx, res->descriptor_set,
                                     res->material_ubo.buf,
                                     2, 1, &material_offset, &material_size,
                                     false, true);

   // Pipeline
   res->pipeline_layout = create_pipeline_layout(ctx, res->set_layout);

   // Vertex attribute binding 0: position and normal
   VkVertexInputBindingDescription vi_bindings[2];
   vi_bindings[0].binding = 0;
   vi_bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
   vi_bindings[0].stride =
      vkdf_mesh_get_vertex_data_stride(res->model->meshes[0]);

   // Vertex attribute binding 1: material index (per-instance)
   vi_bindings[1].binding = 1;
   vi_bindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
   vi_bindings[1].stride = sizeof(uint32_t);

   VkVertexInputAttributeDescription vi_attribs[3];

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

   // binding 1, location 2: per-instance material index
   vi_attribs[2].binding = 1;
   vi_attribs[2].location = 2;
   vi_attribs[2].format = VK_FORMAT_R32_UINT;
   vi_attribs[2].offset = 0;

   // We assume all meshes in the model use the same primitive type
   VkdfMesh *mesh = res->model->meshes[0];
   VkPrimitiveTopology primitive = vkdf_mesh_get_primitive(mesh);
   res->pipeline = vkdf_create_gfx_pipeline(ctx,
                                            NULL,
                                            2,
                                            vi_bindings,
                                            3,
                                            vi_attribs,
                                            true,
                                            VK_COMPARE_OP_LESS,
                                            res->render_pass,
                                            res->pipeline_layout,
                                            primitive,
                                            VK_CULL_MODE_BACK_BIT,
                                            1,
                                            res->vs_module,
                                            res->fs_module);

   // Command pool
   res->cmd_pool = vkdf_create_gfx_command_pool(ctx, 0);

   // Command buffers
   res->cmd_bufs = g_new(VkCommandBuffer, ctx->swap_chain_length);
   vkdf_create_command_buffer(ctx,
                              res->cmd_pool,
                              VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                              ctx->swap_chain_length,
                              res->cmd_bufs);

   for (uint32_t i = 0; i < ctx->swap_chain_length; i++) {
      vkdf_command_buffer_begin(res->cmd_bufs[i],
                                VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT);
      render_pass_commands(ctx, res, i);
      vkdf_command_buffer_end(res->cmd_bufs[i]);
   }
}

static void
scene_update(VkdfContext *ctx, void *data)
{
}

static void
scene_render(VkdfContext *ctx, void *data)
{
   DemoResources *res = (DemoResources *) data;

   VkPipelineStageFlags pipeline_stages =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

   vkdf_command_buffer_execute(ctx,
                               res->cmd_bufs[ctx->swap_chain_index],
                               &pipeline_stages,
                               1, &ctx->acquired_sem[ctx->swap_chain_index],
                               1, &ctx->draw_sem[ctx->swap_chain_index]);
}

static void
destroy_pipeline_resources(VkdfContext *ctx, DemoResources *res)
{
   vkDestroyPipeline(ctx->device, res->pipeline, NULL);
   vkDestroyPipelineLayout(ctx->device, res->pipeline_layout, NULL);
}

static void
destroy_framebuffer_resources(VkdfContext *ctx, DemoResources *res)
{
   for (uint32_t i = 0; i < ctx->swap_chain_length; i++)
      vkDestroyFramebuffer(ctx->device, res->framebuffers[i], NULL);
   g_free(res->framebuffers);
}

static void
destroy_shader_resources(VkdfContext *ctx, DemoResources *res)
{
  vkDestroyShaderModule(ctx->device, res->vs_module, NULL);
  vkDestroyShaderModule(ctx->device, res->fs_module, NULL);
}

static void
destroy_command_buffer_resources(VkdfContext *ctx, DemoResources *res)
{
   vkFreeCommandBuffers(ctx->device,
                        res->cmd_pool,
                        ctx->swap_chain_length,
                        res->cmd_bufs);
   vkDestroyCommandPool(ctx->device, res->cmd_pool, NULL);
}

static void
destroy_descriptor_resources(VkdfContext *ctx, DemoResources *res)
{
   vkFreeDescriptorSets(ctx->device,
                        res->ubo_pool, 1, &res->descriptor_set);
   vkDestroyDescriptorSetLayout(ctx->device, res->set_layout, NULL);
   vkDestroyDescriptorPool(ctx->device, res->ubo_pool, NULL);
}

static void
destroy_ubo_resources(VkdfContext *ctx, DemoResources *res)
{
   vkDestroyBuffer(ctx->device, res->material_ubo.buf, NULL);
   vkFreeMemory(ctx->device, res->material_ubo.mem, NULL);

   vkDestroyBuffer(ctx->device, res->VP_ubo.buf, NULL);
   vkFreeMemory(ctx->device, res->VP_ubo.mem, NULL);

   vkDestroyBuffer(ctx->device, res->M_ubo.buf, NULL);
   vkFreeMemory(ctx->device, res->M_ubo.mem, NULL);
}

void
cleanup_resources(VkdfContext *ctx, DemoResources *res)
{
   vkDestroyBuffer(ctx->device, res->instance_buf.buf, NULL);
   vkFreeMemory(ctx->device, res->instance_buf.mem, NULL);
   for (uint32_t i = 0; i < NUM_OBJECTS; i++)
      vkdf_object_free(res->objs[i]);
   vkdf_model_free(ctx, res->model);
   destroy_pipeline_resources(ctx, res);
   vkDestroyRenderPass(ctx->device, res->render_pass, NULL);
   destroy_descriptor_resources(ctx, res);
   destroy_ubo_resources(ctx, res);
   vkdf_destroy_image(ctx, &res->depth_image);
   destroy_framebuffer_resources(ctx, res);
   destroy_shader_resources(ctx, res);
   destroy_command_buffer_resources(ctx, res);
}

int
main()
{
   VkdfContext ctx;
   DemoResources resources;

   srandom(time(NULL));

   vkdf_init(&ctx, 800, 600, false, false, ENABLE_DEBUG);
   init_resources(&ctx, &resources);

   vkdf_event_loop_run(&ctx, false, scene_update, scene_render, &resources);

   cleanup_resources(&ctx, &resources);
   vkdf_cleanup(&ctx);

   return 0;
}
