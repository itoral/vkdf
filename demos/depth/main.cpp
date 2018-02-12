#include "vkdf.hpp"

// ----------------------------------------------------------------------------
// Renders a rotating triangle.
// ----------------------------------------------------------------------------

typedef struct {
   VkCommandPool cmd_pool;
   VkCommandBuffer *cmd_bufs;
   VkdfBuffer vertex_buf;
   VkdfBuffer ubo;
   VkRenderPass render_pass;
   VkDescriptorSetLayout set_layout;
   VkPipelineLayout pipeline_layout;
   VkPipeline pipeline;
   VkShaderModule vs_module;
   VkShaderModule fs_module;
   VkFramebuffer *framebuffers;
   VkDescriptorPool descriptor_pool;
   VkDescriptorSet descriptor_set;
   VkdfImage depth_image;

   glm::mat4 clip;
   glm::mat4 view;
   glm::mat4 projection;
   glm::mat4 mvp;
} DemoResources;

typedef struct {
   glm::vec4 pos;
   glm::vec4 col;
} VertexData;

static VkdfBuffer
create_vertex_buffer(VkdfContext *ctx)
{
   const VertexData vertex_data[6] = {
      // Green triangle
      {
         glm::vec4(-1.0f, -1.0f, 0.0f, 1.0f),
         glm::vec4(0.0f, 1.0f, 0.0f, 1.0f)
      },
      {
         glm::vec4( 1.0f, -1.0f, 0.0f, 1.0f),
         glm::vec4(0.0f, 1.0f, 0.0f, 1.0f)
      },
      {
         glm::vec4( 0.0f,  1.0f, 0.0f, 1.0f),
         glm::vec4(0.0f, 1.0f, 0.0f, 1.0f)
      },
      // Red triangle
      {
         glm::vec4(-1.0f, -1.0f, 1.0f, 1.0f),
         glm::vec4(1.0f, 0.0f, 0.0f, 1.0f)
      },
      {
         glm::vec4( 1.0f, -1.0f, 1.0f, 1.0f),
         glm::vec4(1.0f, 0.0f, 0.0f, 1.0f)
      },
      {
         glm::vec4( 0.0f,  1.0f, -1.0f, 1.0f),
         glm::vec4(1.0f, 0.0f, 0.0f, 1.0f)
      },
   };

   VkdfBuffer buf =
      vkdf_create_buffer(ctx,
                         0,                                    // flag
                         sizeof(vertex_data),                  // size
                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,    // usage
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT); // memory type

   vkdf_buffer_map_and_fill(ctx, buf, 0, sizeof(vertex_data), vertex_data);

   return buf;
}

static VkdfBuffer
create_ubo(VkdfContext *ctx, glm::mat4 mvp)
{
   VkdfBuffer buf =
      vkdf_create_buffer(ctx,
                         0,                                    // flags
                         sizeof(mvp),                          // size
                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,   // usage
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT); // memory type
   return buf;
}

static VkRenderPass
create_render_pass(VkdfContext *ctx, DemoResources *res)
{
   VkAttachmentDescription attachments[2];

   // Color attachment
   attachments[0].format = ctx->surface_format.format;
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

   // Single subpass
   VkAttachmentReference color_reference;
   color_reference.attachment = 0;
   color_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

   VkAttachmentReference depth_reference;
   depth_reference.attachment = 1;
   depth_reference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

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
   vkdf_color_clear_set(&clear_values[0], glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));
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

   // Pipeline
   vkCmdBindPipeline(res->cmd_bufs[index], VK_PIPELINE_BIND_POINT_GRAPHICS,
                     res->pipeline);

   // Descriptor set
   vkCmdBindDescriptorSets(res->cmd_bufs[index],
                           VK_PIPELINE_BIND_POINT_GRAPHICS,
                           res->pipeline_layout,
                           0,                      // First decriptor set
                           1,                      // Descriptor set count
                           &res->descriptor_set,   // Descriptor sets
                           0,                      // Dynamic offset count
                           NULL);                  // Dynamic offsets

   // Vertex buffer
   const VkDeviceSize offsets[1] = { 0 };
   vkCmdBindVertexBuffers(res->cmd_bufs[index],
                          0,                       // Start Binding
                          1,                       // Binding Count
                          &res->vertex_buf.buf,    // Buffers
                          offsets);                // Offsets

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

   // Draw
   vkCmdDraw(res->cmd_bufs[index],
             6,                    // vertex count
             1,                    // instance count
             0,                    // first vertex
             0);                   // first instance

   vkCmdEndRenderPass(res->cmd_bufs[index]);
}

static VkPipelineLayout
create_pipeline_layout(VkdfContext *ctx, VkDescriptorSetLayout set_layout)
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
   res->clip = glm::mat4(1.0f,  0.0f, 0.0f, 0.0f,
                         0.0f, -1.0f, 0.0f, 0.0f,
                         0.0f,  0.0f, 0.5f, 0.0f,
                         0.0f,  0.0f, 0.5f, 1.0f);

   res->projection = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 100.0f);

   res->view = glm::lookAt(glm::vec3( 0,  0, -5),  // Camera position
                           glm::vec3( 0,  0,  0),  // Looking at origin
                           glm::vec3( 0,  1,  0)); // Head is up
}

static void
init_resources(VkdfContext *ctx, DemoResources *res)
{
   memset(res, 0, sizeof(DemoResources));

   // Compute View, Projection and Cliip matrices
   init_matrices(res);

   // Vertex buffer
   res->vertex_buf = create_vertex_buffer(ctx);

   // UBO (for MVP matrix)
   res->ubo = create_ubo(ctx, res->mvp);

   // Depth image
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
   res->descriptor_pool =
      vkdf_create_descriptor_pool(ctx, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1);

   // Descriptor set (bound to UBO)
   res->set_layout =
      vkdf_create_ubo_descriptor_set_layout(ctx, 0, 1,
                                            VK_SHADER_STAGE_VERTEX_BIT, false);

   res->descriptor_set =
      create_descriptor_set(ctx, res->descriptor_pool, res->set_layout);

   VkDeviceSize ubo_offset = 0;
   VkDeviceSize ubo_size = sizeof(res->mvp);
   vkdf_descriptor_set_buffer_update(ctx, res->descriptor_set, res->ubo.buf,
                                     0, 1, &ubo_offset, &ubo_size, false, true);

   // Pipeline
   res->pipeline_layout = create_pipeline_layout(ctx, res->set_layout);

   VkVertexInputBindingDescription vi_binding;
   vkdf_vertex_binding_set(&vi_binding,
                           0, VK_VERTEX_INPUT_RATE_VERTEX, sizeof(VertexData));

   VkVertexInputAttributeDescription vi_attribs[2];
   vkdf_vertex_attrib_set(&vi_attribs[0],
                          0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0);
   vkdf_vertex_attrib_set(&vi_attribs[1], 0, 1, VK_FORMAT_R32G32_SFLOAT, 16);

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
                                            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                            VK_CULL_MODE_NONE,
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
update_mvp(DemoResources *res)
{
   static float rotX = 0.0f;
   static float rotY = 0.0f;
   static float rotZ = 0.0f;

   rotY += 0.005f;
   rotX += 0.007f;
   rotZ += 0.009f;

   glm::mat4 Model(1.0f);
   Model = glm::rotate(Model, rotX, glm::vec3(1, 0, 0));
   Model = glm::rotate(Model, rotY, glm::vec3(0, 1, 0));
   Model = glm::rotate(Model, rotZ, glm::vec3(0, 0, 1));

   res->mvp = res->clip * res->projection * res->view * Model;
}

static void
scene_update(VkdfContext *ctx, void *data)
{
   DemoResources *res = (DemoResources *) data;

   // MVP in UBO
   update_mvp(res);
   vkdf_buffer_map_and_fill(ctx, res->ubo, 0, sizeof(res->mvp), &res->mvp);
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
                        res->descriptor_pool, 1, &res->descriptor_set);
   vkDestroyDescriptorSetLayout(ctx->device, res->set_layout, NULL);
   vkDestroyDescriptorPool(ctx->device, res->descriptor_pool, NULL);
}

static void
destroy_ubo_resources(VkdfContext *ctx, DemoResources *res)
{
   vkDestroyBuffer(ctx->device, res->ubo.buf, NULL);
   vkFreeMemory(ctx->device, res->ubo.mem, NULL);
}

void
cleanup_resources(VkdfContext *ctx, DemoResources *res)
{
   destroy_pipeline_resources(ctx, res);
   vkDestroyRenderPass(ctx->device, res->render_pass, NULL);
   vkdf_destroy_buffer(ctx, &res->vertex_buf);
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

   vkdf_init(&ctx, 800, 600, false, false, ENABLE_DEBUG);
   init_resources(&ctx, &resources);

   vkdf_event_loop_run(&ctx, false, scene_update, scene_render, &resources);

   cleanup_resources(&ctx, &resources);
   vkdf_cleanup(&ctx);

   return 0;
}
