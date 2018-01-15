
#include "vkdf.hpp"

// ----------------------------------------------------------------------------
// Creates a full texture with data for all mimap levels, then uses it to
// a quad  at various distances to visualize various mipmap levels.
// ----------------------------------------------------------------------------

static uint32_t TEX_SIZE = 512;

typedef struct {
   VkCommandPool cmd_pool;
   VkCommandBuffer *cmd_bufs;
   VkdfBuffer vertex_buf;
   VkdfBuffer ubo;
   VkRenderPass render_pass;
   VkDescriptorSetLayout set_layout_ubo;
   VkDescriptorSetLayout set_layout_sampler;
   VkPipelineLayout pipeline_layout;
   VkPipeline pipeline;
   VkShaderModule vs_module;
   VkShaderModule fs_module;
   VkFramebuffer *framebuffers;
   VkDescriptorPool descriptor_pool_ubo;
   VkDescriptorPool descriptor_pool_sampler;
   VkDescriptorSet descriptor_set_ubo;
   VkDescriptorSet descriptor_set_sampler;
   VkdfImage texture;
   VkSampler sampler;

   glm::mat4 clip;
   glm::mat4 view;
   glm::mat4 projection;
   glm::mat4 mvp;
} DemoResources;

typedef struct {
   glm::vec4 pos;
   glm::vec2 tex_coord;
} VertexData;

typedef struct {
   uint32_t num_levels;
   VkDeviceSize total_bytes;
   uint32_t *size;
   VkDeviceSize *bytes;
} ImageLevelData;

static VkdfBuffer
create_vertex_buffer(VkdfContext *ctx)
{
   const VertexData vertex_data[4] = {
      { glm::vec4(-1.0f, -1.0f, 0.0f, 1.0f), glm::vec2(0.0f, 0.0f) },
      { glm::vec4( 1.0f, -1.0f, 0.0f, 1.0f), glm::vec2(1.0f, 0.0f) },
      { glm::vec4(-1.0f,  1.0f, 0.0f, 1.0f), glm::vec2(0.0f, 1.0f) },
      { glm::vec4( 1.0f,  1.0f, 0.0f, 1.0f), glm::vec2(1.0f, 1.0f) }
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
create_render_pass(VkdfContext *ctx)
{
   VkAttachmentDescription attachments[1];

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

   // Single subpass
   VkAttachmentReference color_reference;
   color_reference.attachment = 0;
   color_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

   VkSubpassDescription subpass;
   subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
   subpass.flags = 0;
   subpass.inputAttachmentCount = 0;
   subpass.pInputAttachments = NULL;
   subpass.colorAttachmentCount = 1;
   subpass.pColorAttachments = &color_reference;
   subpass.pResolveAttachments = NULL;
   subpass.pDepthStencilAttachment = NULL;
   subpass.preserveAttachmentCount = 0;
   subpass.pPreserveAttachments = NULL;

   // Create render pass
   VkRenderPassCreateInfo rp_info;
   rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
   rp_info.pNext = NULL;
   rp_info.attachmentCount = 1;
   rp_info.pAttachments = attachments;
   rp_info.subpassCount = 1;
   rp_info.pSubpasses = &subpass;
   rp_info.dependencyCount = 0;
   rp_info.pDependencies = NULL;
   rp_info.flags = 0;

   VkRenderPass render_pass; 
   VkResult res =
      vkCreateRenderPass(ctx->device, &rp_info, NULL, &render_pass);
   if (res != VK_SUCCESS)
      vkdf_fatal("Failed to create render pass");

   return render_pass;
}

static void
render_pass_commands(VkdfContext *ctx, DemoResources *res, uint32_t index)
{
   VkClearValue clear_values[1];
   clear_values[0].color.float32[0] = 0.0f;
   clear_values[0].color.float32[1] = 0.0f;
   clear_values[0].color.float32[2] = 0.0f;
   clear_values[0].color.float32[3] = 1.0f;

   VkRenderPassBeginInfo rp_begin;
   rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
   rp_begin.pNext = NULL;
   rp_begin.renderPass = res->render_pass;
   rp_begin.framebuffer = res->framebuffers[index];
   rp_begin.renderArea.offset.x = 0;
   rp_begin.renderArea.offset.y = 0;
   rp_begin.renderArea.extent.width = ctx->width;
   rp_begin.renderArea.extent.height = ctx->height;
   rp_begin.clearValueCount = 1;
   rp_begin.pClearValues = clear_values;

   vkCmdBeginRenderPass(res->cmd_bufs[index],
                        &rp_begin,
                        VK_SUBPASS_CONTENTS_INLINE);

   // Pipeline
   vkCmdBindPipeline(res->cmd_bufs[index], VK_PIPELINE_BIND_POINT_GRAPHICS,
                     res->pipeline);

   // Descriptor sets
   vkCmdBindDescriptorSets(res->cmd_bufs[index],
                           VK_PIPELINE_BIND_POINT_GRAPHICS,
                           res->pipeline_layout,
                           0,                         // First decriptor set
                           1,                         // Descriptor set count
                           &res->descriptor_set_ubo,  // Descriptor sets
                           0,                         // Dynamic offset count
                           NULL);                     // Dynamic offsets

   vkCmdBindDescriptorSets(res->cmd_bufs[index],
                           VK_PIPELINE_BIND_POINT_GRAPHICS,
                           res->pipeline_layout,
                           1,                            // Second decriptor set
                           1,                            // Descriptor set count
                           &res->descriptor_set_sampler, // Descriptor sets
                           0,                            // Dynamic offset count
                           NULL);                        // Dynamic offsets

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
             4,                    // vertex count
             1,                    // instance count
             0,                    // first vertex
             0);                   // first instance

   vkCmdEndRenderPass(res->cmd_bufs[index]);
}

static VkPipelineLayout
create_pipeline_layout(VkdfContext *ctx,
                       VkDescriptorSetLayout set_layout_ubo,
                       VkDescriptorSetLayout set_layout_sampler)
{
   VkDescriptorSetLayout set_layouts[2] = {
      set_layout_ubo,
      set_layout_sampler
   };

   VkPipelineLayoutCreateInfo pipeline_layout_info;
   pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
   pipeline_layout_info.pNext = NULL;
   pipeline_layout_info.pushConstantRangeCount = 0;
   pipeline_layout_info.pPushConstantRanges = NULL;
   pipeline_layout_info.setLayoutCount = 2;
   pipeline_layout_info.pSetLayouts = set_layouts;
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

   res->projection = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 1000.0f);

   res->view = glm::lookAt(glm::vec3( 0,  0, -1),  // Camera position
                           glm::vec3( 0,  0,  0),  // Looking at origin
                           glm::vec3( 0,  1,  0)); // Head is up
}

static void
compute_image_level_data(ImageLevelData *levels)
{
   levels->num_levels = log2(TEX_SIZE) + 1;
   levels->total_bytes = 0;
   levels->size = g_new0(uint32_t, levels->num_levels);
   levels->bytes = g_new0(VkDeviceSize, levels->num_levels);
   uint32_t level_size = TEX_SIZE;
   for (uint32_t l = 0; l < levels->num_levels; l++) {
      levels->size[l] = level_size;
      levels->bytes[l] = level_size * level_size * 4;
      levels->total_bytes += levels->bytes[l];
      level_size /= 2;
   }
}

static glm::vec4
get_level_color(uint32_t level)
{
   static glm::vec4 level_colors[9] = {
      glm::vec4(1.0f, 0.0f, 0.0f, 1.0f),
      glm::vec4(0.0f, 1.0f, 0.0f, 1.0f),
      glm::vec4(0.0f, 0.0f, 1.0f, 1.0f),
      glm::vec4(1.0f, 1.0f, 0.0f, 1.0f),
      glm::vec4(1.0f, 0.0f, 1.0f, 1.0f),
      glm::vec4(0.0f, 1.0f, 1.0f, 1.0f),
      glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
      glm::vec4(0.5f, 0.0f, 0.0f, 1.0f),
      glm::vec4(0.0f, 0.5f, 0.0f, 1.0f),
   };
   return level_colors[level % 9];
}

static VkdfImage
create_texture(VkdfContext *ctx, DemoResources *res)
{
   // Create a host-visible staging buffer where we will write image data
   ImageLevelData levels;
   compute_image_level_data(&levels);

   VkdfBuffer staging_buf =
      vkdf_create_buffer(ctx,
                         0,
                         levels.total_bytes,
                         VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

   // Write image data to the staging buffer for each mipmap. Each level has
   // a different color so it is easy to spot which level is being displayed.
   uint8_t *data;
   vkdf_memory_map(ctx, staging_buf.mem, 0, VK_WHOLE_SIZE, (void **)&data);

   for (uint32_t l = 0; l < levels.num_levels; l++) {
      for (uint32_t i = 0; i < levels.size[l] * levels.size[l]; i++) {
         glm::vec4 color = get_level_color(l);
         data[i * 4 + 0] = color.r * 255.0f;
         data[i * 4 + 1] = color.g * 255.0f;
         data[i * 4 + 2] = color.b * 255.0f;
         data[i * 4 + 3] = color.a * 255.0f;
      }
      data += levels.size[l] * levels.size[l] * 4;
   }

   vkdf_memory_unmap(ctx, staging_buf.mem, staging_buf.mem_props,
                     0, VK_WHOLE_SIZE);

   // Create a device-local texture image that we will sample from the
   // fragment shader. We will need to fill this image by copying texture
   // data from the staging buffer for each mipmap level.
   VkdfImage image =
      vkdf_create_image(ctx,
                        TEX_SIZE,
                        TEX_SIZE,
                        levels.num_levels,
                        VK_IMAGE_TYPE_2D,
                        VK_FORMAT_R8G8B8A8_UNORM,
                        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT,
                        VK_IMAGE_USAGE_SAMPLED_BIT |
                           VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        VK_IMAGE_ASPECT_COLOR_BIT,
                        VK_IMAGE_VIEW_TYPE_2D);

   // Create a command buffer to copy image data
   VkCommandBuffer upload_tex_cmd_buf;
   vkdf_create_command_buffer(ctx,
                              res->cmd_pool,
                              VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                              1,
                              &upload_tex_cmd_buf);
   vkdf_command_buffer_begin(upload_tex_cmd_buf,
                             VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

   // We need the image to be in transfer dst layout
   VkImageSubresourceRange subresource_range =
      vkdf_create_image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT,
                                          0, levels.num_levels, 0, 1);

   vkdf_image_set_layout(ctx,
                         upload_tex_cmd_buf,
                         image.image,
                         subresource_range,
                         VK_IMAGE_LAYOUT_UNDEFINED,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);

   // Copy all mipmap levels from the staging buffer to the image
   VkBufferImageCopy *regions = g_new0(VkBufferImageCopy, levels.num_levels);
   VkDeviceSize offset = 0;
   for (uint32_t l = 0; l < levels.num_levels; l++) {
      VkImageSubresourceLayers subresource_layers =
         vkdf_create_image_subresource_layers(VK_IMAGE_ASPECT_COLOR_BIT, l, 0, 1);

      regions[l].imageSubresource = subresource_layers;
      regions[l].imageExtent.width = levels.size[l];
      regions[l].imageExtent.height = levels.size[l];
      regions[l].imageExtent.depth = 1;
      regions[l].bufferOffset = offset;

      offset += levels.size[l] * levels.size[l] * 4;
   }

   vkCmdCopyBufferToImage(upload_tex_cmd_buf,
                          staging_buf.buf,
                          image.image,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          levels.num_levels,
                          regions);

   // Now that the image data has been copied we need the image to be in a
   // layout suitable for shader access, specifically in the fragment shader
   vkdf_image_set_layout(ctx,
                         upload_tex_cmd_buf,
                         image.image,
                         subresource_range,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

   vkdf_command_buffer_end(upload_tex_cmd_buf);

   // Execute the command buffer and wait for it to complete
   vkdf_command_buffer_execute_sync(ctx,
                                    upload_tex_cmd_buf,
                                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

   vkFreeCommandBuffers(ctx->device, res->cmd_pool, 1, &upload_tex_cmd_buf);
   vkdf_destroy_buffer(ctx, &staging_buf);
   g_free(levels.size);
   g_free(levels.bytes);

   return image;
}

static void
init_resources(VkdfContext *ctx, DemoResources *res)
{
   memset(res, 0, sizeof(DemoResources));

   // Command pool
   res->cmd_pool = vkdf_create_gfx_command_pool(ctx, 0);

   // Compute View, Projection and Cliip matrices
   init_matrices(res);

   // Vertex buffer
   res->vertex_buf = create_vertex_buffer(ctx);

   // UBO (for MVP matrix)
   res->ubo = create_ubo(ctx, res->mvp);

   // Shaders
   res->vs_module = vkdf_create_shader_module(ctx, "shader.vert.spv");
   res->fs_module = vkdf_create_shader_module(ctx, "shader.frag.spv");

   // Texture & Sampler
   res->texture = create_texture(ctx, res);
   res->sampler = vkdf_create_sampler(ctx,
                                      VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                      VK_FILTER_NEAREST,
                                      VK_SAMPLER_MIPMAP_MODE_NEAREST);

   // Render pass
   res->render_pass = create_render_pass(ctx);

   // Framebuffers
   res->framebuffers =
      vkdf_create_framebuffers_for_swap_chain(ctx, res->render_pass, 0, NULL);

   // Descriptor pool (UBO)
   res->descriptor_pool_ubo =
      vkdf_create_descriptor_pool(ctx, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1);

   // Descriptor pool (Sampler)
   res->descriptor_pool_sampler =
      vkdf_create_descriptor_pool(ctx, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1);

   // Descriptor set (UBO)
   res->set_layout_ubo =
      vkdf_create_ubo_descriptor_set_layout(ctx, 0, 1,
                                            VK_SHADER_STAGE_VERTEX_BIT, false);

   res->descriptor_set_ubo =
      create_descriptor_set(ctx, res->descriptor_pool_ubo, res->set_layout_ubo);

   VkDeviceSize ubo_offset = 0;
   VkDeviceSize ubo_size = sizeof(res->mvp);
   vkdf_descriptor_set_buffer_update(ctx, res->descriptor_set_ubo, res->ubo.buf,
                                     0, 1, &ubo_offset, &ubo_size, false, true);

   // Descriptor set (Sampler)
   res->set_layout_sampler =
      vkdf_create_sampler_descriptor_set_layout(ctx,
                                                0, 1,
                                                VK_SHADER_STAGE_FRAGMENT_BIT);

   res->descriptor_set_sampler =
      create_descriptor_set(ctx,
                            res->descriptor_pool_sampler,
                            res->set_layout_sampler);

   vkdf_descriptor_set_sampler_update(ctx,
                                      res->descriptor_set_sampler,
                                      res->sampler,
                                      res->texture.view,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                      0, 1);

   // Pipeline
   res->pipeline_layout = create_pipeline_layout(ctx,
                                                 res->set_layout_ubo,
                                                 res->set_layout_sampler);

   VkVertexInputBindingDescription vi_binding;
   vi_binding.binding = 0;
   vi_binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
   vi_binding.stride = sizeof(VertexData);

   VkVertexInputAttributeDescription vi_attribs[2];
   vi_attribs[0].binding = 0;
   vi_attribs[0].location = 0;
   vi_attribs[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
   vi_attribs[0].offset = 0;

   vi_attribs[1].binding = 0;
   vi_attribs[1].location = 1;
   vi_attribs[1].format = VK_FORMAT_R32G32_SFLOAT;
   vi_attribs[1].offset = 16;

   res->pipeline = vkdf_create_gfx_pipeline(ctx,
                                            NULL,
                                            1,
                                            &vi_binding,
                                            2,
                                            vi_attribs,
                                            false,
                                            VK_COMPARE_OP_LESS,
                                            res->render_pass,
                                            res->pipeline_layout,
                                            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
                                            VK_CULL_MODE_NONE,
                                            res->vs_module,
                                            res->fs_module);

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
   static float offsetZ = 0.0f;
   static int32_t dir = 1;

   // Increase speed with the distance from camera
   float speed = offsetZ * 0.01f + 0.1f;
   offsetZ += speed * dir;

   glm::mat4 Model(1.0f);
   Model = glm::translate(Model, glm::vec3(0.0f, 0.0f, offsetZ));

   res->mvp = res->clip * res->projection * res->view * Model;

   // Make the quad bounce back when it is too far or too close
   if (offsetZ >= 300.0f || offsetZ <= 0.0f)
      dir *= -1;
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
                        res->descriptor_pool_ubo, 1, &res->descriptor_set_ubo);
   vkDestroyDescriptorSetLayout(ctx->device, res->set_layout_ubo, NULL);
   vkDestroyDescriptorPool(ctx->device, res->descriptor_pool_ubo, NULL);

   vkFreeDescriptorSets(ctx->device,
                        res->descriptor_pool_sampler, 1, &res->descriptor_set_sampler);
   vkDestroyDescriptorSetLayout(ctx->device, res->set_layout_sampler, NULL);
   vkDestroyDescriptorPool(ctx->device, res->descriptor_pool_sampler, NULL);
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
   vkDestroySampler(ctx->device, res->sampler, NULL);
   vkdf_destroy_image(ctx, &res->texture);
   destroy_pipeline_resources(ctx, res);
   vkDestroyRenderPass(ctx->device, res->render_pass, NULL);
   vkdf_destroy_buffer(ctx, &res->vertex_buf);
   destroy_descriptor_resources(ctx, res);
   destroy_ubo_resources(ctx, res);
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
