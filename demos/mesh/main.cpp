#include "vkdf.hpp"

#include <stdlib.h>

// ----------------------------------------------------------------------------
// Renders a bunch of objects that share the same mesh using instancing
// ----------------------------------------------------------------------------

// WARNING: this must match the size of the Model array in the vertex shader
#define NUM_OBJECTS 501

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

   // Descriptor sets for UBO bindings
   VkDescriptorSetLayout MVP_set_layout;
   VkDescriptorSet MVP_descriptor_set;

   // View/Projection matrices
   glm::mat4 view;
   glm::mat4 projection;

   // Objects
   VkdfMesh *cube_mesh;
   VkdfObject *objs[NUM_OBJECTS];
} DemoResources;

typedef struct {
   glm::vec3 pos;
   glm::vec3 normal;
} VertexData;

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
   clear_values[0].color.float32[0] = 0.0f;
   clear_values[0].color.float32[1] = 0.0f;
   clear_values[0].color.float32[2] = 0.0f;
   clear_values[0].color.float32[3] = 1.0f;
   clear_values[1].depthStencil.depth = 1.0f;
   clear_values[1].depthStencil.stencil = 0;

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

   // Vertex buffer
   const VkdfMesh *mesh = res->objs[0]->model->meshes[0];
   const VkDeviceSize offsets[1] = { 0 };
   vkCmdBindVertexBuffers(res->cmd_bufs[index],
                          0,                       // Start Binding
                          1,                       // Binding Count
                          &mesh->vertex_buf.buf,   // Buffers
                          offsets);                // Offsets


   // Bind static VP descriptor set once
   vkCmdBindDescriptorSets(res->cmd_bufs[index],
                           VK_PIPELINE_BIND_POINT_GRAPHICS,
                           res->pipeline_layout,
                           0,                        // First decriptor set
                           1,                        // Descriptor set count
                           &res->MVP_descriptor_set, // Descriptor sets
                           0,                        // Dynamic offset count
                           NULL);                    // Dynamic offsets

   // Draw
   vkCmdDraw(res->cmd_bufs[index],
             mesh->vertices.size(),                // vertex count
             NUM_OBJECTS,                          // instance count
             0,                                    // first vertex
             0);                                   // first instance

   vkCmdEndRenderPass(res->cmd_bufs[index]);
}

static VkPipelineLayout
create_pipeline_layout(VkdfContext *ctx,
                       VkDescriptorSetLayout MVP_set_layout)
{
   VkPipelineLayoutCreateInfo pipeline_layout_info;
   pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
   pipeline_layout_info.pNext = NULL;
   pipeline_layout_info.pushConstantRangeCount = 0;
   pipeline_layout_info.pPushConstantRanges = NULL;
   pipeline_layout_info.setLayoutCount = 1;
   pipeline_layout_info.pSetLayouts = &MVP_set_layout;
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
init_meshes(VkdfContext *ctx, DemoResources *res)
{
   res->cube_mesh = vkdf_cube_mesh_new(ctx);
   vkdf_mesh_fill_vertex_buffer(ctx, res->cube_mesh);
}

static void
init_objects(VkdfContext *ctx, DemoResources *res)
{
   for (int i = 0; i < NUM_OBJECTS; i++) {
      glm::vec3 pos = glm::vec3(0.0f, 0.0f, 0.0f);
      res->objs[i] = vkdf_object_new_from_mesh(pos, res->cube_mesh);
      vkdf_object_set_scale(res->objs[i], glm::vec3(0.15f, 0.15f, 0.15f));
   }
}

static void
init_resources(VkdfContext *ctx, DemoResources *res)
{
   memset(res, 0, sizeof(DemoResources));

   // Compute View, Projection and Cliip matrices
   init_matrices(res);

   // Load meshes
   init_meshes(ctx, res);

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
      vkdf_create_descriptor_pool(ctx, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2);

   // Descriptor set for UBO
   res->MVP_set_layout =
      vkdf_create_ubo_descriptor_set_layout(ctx, 0, 2,
                                            VK_SHADER_STAGE_VERTEX_BIT, false);

   res->MVP_descriptor_set =
      create_descriptor_set(ctx, res->ubo_pool, res->MVP_set_layout);

   // Map View and Projection UBOs to set binding 0
   VkDeviceSize VP_offset = 0;
   VkDeviceSize VP_size = 2 * sizeof(glm::mat4);
   vkdf_descriptor_set_buffer_update(ctx, res->MVP_descriptor_set,
                                     res->VP_ubo.buf,
                                     0, 1, &VP_offset, &VP_size, false, true);

   // Map Model UBO to set binding 1
   VkDeviceSize M_offset = 0;
   VkDeviceSize M_size = NUM_OBJECTS * sizeof(glm::mat4);
   vkdf_descriptor_set_buffer_update(ctx, res->MVP_descriptor_set,
                                     res->M_ubo.buf,
                                     1, 1, &M_offset, &M_size, false, true);

   // Pipeline
   res->pipeline_layout =
      create_pipeline_layout(ctx, res->MVP_set_layout);

   // Vertex attribute binding 0: position and normal
   VkVertexInputBindingDescription vi_binding;
   vi_binding.binding = 0;
   vi_binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
   vi_binding.stride = sizeof(VertexData);

   VkVertexInputAttributeDescription vi_attribs[2];

   // location 0: position
   vi_attribs[0].binding = 0;
   vi_attribs[0].location = 0;
   vi_attribs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
   vi_attribs[0].offset = 0;

   // location 1: normal
   vi_attribs[1].binding = 0;
   vi_attribs[1].location = 1;
   vi_attribs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
   vi_attribs[1].offset = 12;

   VkPrimitiveTopology primitive = vkdf_mesh_get_primitive(res->cube_mesh);
   res->pipeline = vkdf_create_gfx_pipeline(ctx,
                                            NULL,
                                            1,
                                            &vi_binding,
                                            2,
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
   static bool initialized = false;
   static glm::vec3 pos_speeds[NUM_OBJECTS];
   static glm::vec3 rot_speeds[NUM_OBJECTS];

   if (!initialized) {
      for (int i = 0; i < NUM_OBJECTS; i++) {
         pos_speeds[i] = glm::vec3(RAND_NEG(100) / 1000.0f,
                                   RAND_NEG(100) / 1000.0f,
                                   RAND_NEG(100) / 1000.0f);
         rot_speeds[i] = glm::vec3(RAND_NEG(100) / 50.0f,
                                   RAND_NEG(100) / 50.0f,
                                   RAND_NEG(100) / 50.0f);
      }
      initialized = true;

      {
         DemoResources *res = (DemoResources *) data;

         VkDeviceSize buf_size = VK_WHOLE_SIZE;

         uint8_t *map;
         vkdf_memory_map(ctx, res->M_ubo.mem, 0, buf_size, (void**) &map);

         for (uint32_t i = 0; i < NUM_OBJECTS; i++) {
            VkdfObject *obj = res->objs[i];

            glm::vec3 new_rot = obj->rot + rot_speeds[i];
            vkdf_object_set_rotation(obj, new_rot);

            glm::vec3 new_pos = obj->pos + pos_speeds[i];
            vkdf_object_set_position(obj, new_pos);

            glm::mat4 Model = vkdf_object_get_model_matrix(obj);
            memcpy(map, &Model[0][0], sizeof(glm::mat4));
            map += sizeof(glm::mat4);

            if (obj->pos.x > 5.0f || obj->pos.x <  -5.0f)
               pos_speeds[i].x *= -1.0f;
            if (obj->pos.y > 5.0f || obj->pos.y <  -5.0f)
               pos_speeds[i].y *= -1.0f;
            if (obj->pos.z > 5.0f || obj->pos.z <  -5.0f)
               pos_speeds[i].z *= -1.0f;
         }

         vkdf_memory_unmap(ctx, res->M_ubo.mem, res->M_ubo.mem_props,
                           0, buf_size);
         return;
      }
   }

   DemoResources *res = (DemoResources *) data;

   VkDeviceSize buf_size = VK_WHOLE_SIZE;

   uint8_t *map;
   vkdf_memory_map(ctx, res->M_ubo.mem, 0, buf_size, (void**) &map);

   for (uint32_t i = 0; i < NUM_OBJECTS; i++) {
      VkdfObject *obj = res->objs[i];

      glm::vec3 new_rot = obj->rot + rot_speeds[i];
      vkdf_object_set_rotation(obj, new_rot);

      glm::vec3 new_pos = obj->pos + pos_speeds[i];
      vkdf_object_set_position(obj, new_pos);

      glm::mat4 Model = vkdf_object_get_model_matrix(obj);
      memcpy(map, &Model[0][0], sizeof(glm::mat4));
      map += sizeof(glm::mat4);

      if (obj->pos.x > 5.0f || obj->pos.x <  -5.0f)
         pos_speeds[i].x *= -1.0f;
      if (obj->pos.y > 5.0f || obj->pos.y <  -5.0f)
         pos_speeds[i].y *= -1.0f;
      if (obj->pos.z > 5.0f || obj->pos.z <  -5.0f)
         pos_speeds[i].z *= -1.0f;
   }

   vkdf_memory_unmap(ctx, res->M_ubo.mem, res->M_ubo.mem_props,
                     0, buf_size);
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
                        res->ubo_pool, 1, &res->MVP_descriptor_set);
   vkDestroyDescriptorSetLayout(ctx->device, res->MVP_set_layout, NULL);
   vkDestroyDescriptorPool(ctx->device, res->ubo_pool, NULL);
}

static void
destroy_ubo_resources(VkdfContext *ctx, DemoResources *res)
{
   vkDestroyBuffer(ctx->device, res->VP_ubo.buf, NULL);
   vkFreeMemory(ctx->device, res->VP_ubo.mem, NULL);

   vkDestroyBuffer(ctx->device, res->M_ubo.buf, NULL);
   vkFreeMemory(ctx->device, res->M_ubo.mem, NULL);
}

void
cleanup_resources(VkdfContext *ctx, DemoResources *res)
{
   for (uint32_t i = 0; i < NUM_OBJECTS; i++)
      vkdf_object_free(res->objs[i]);
   vkdf_mesh_free(ctx, res->cube_mesh);
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
