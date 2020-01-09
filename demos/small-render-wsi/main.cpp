#include "vkdf.hpp"

typedef struct {
   VkCommandPool cmd_pool;
   VkCommandBuffer *cmd_bufs;
   VkRenderPass render_pass;
   VkPipelineLayout pipeline_layout;
   VkPipeline pipeline;
   VkShaderModule vs_module;
   VkShaderModule fs_module;
   VkFramebuffer *framebuffers;
} DemoResources;

static void
render_pass_commands(VkdfContext *ctx, DemoResources *res, uint32_t index)
{
   VkClearValue clear_values[1];
   vkdf_color_clear_set(&clear_values[0], glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));

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

   vkCmdBindPipeline(res->cmd_bufs[index], VK_PIPELINE_BIND_POINT_GRAPHICS,
                     res->pipeline);

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

   vkCmdDraw(res->cmd_bufs[index], 3, 1, 0, 0);

   vkCmdEndRenderPass(res->cmd_bufs[index]);
}

static VkPipelineLayout
create_empty_pipeline_layout(VkdfContext *ctx)
{
   VkPipelineLayoutCreateInfo pipeline_layout_info;
   pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
   pipeline_layout_info.pNext = NULL;
   pipeline_layout_info.pushConstantRangeCount = 0;
   pipeline_layout_info.pPushConstantRanges = NULL;
   pipeline_layout_info.setLayoutCount = 0;
   pipeline_layout_info.pSetLayouts = NULL;
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

static void
init_resources(VkdfContext *ctx, DemoResources *res)
{
   memset(res, 0, sizeof(DemoResources));

   res->vs_module = vkdf_create_shader_module(ctx, "shader.vert.spv");
   res->fs_module = vkdf_create_shader_module(ctx, "shader.frag.spv");

   res->render_pass =
      vkdf_renderpass_simple_new(ctx,
                                 ctx->surface_format.format,
                                 VK_ATTACHMENT_LOAD_OP_CLEAR,
                                 VK_ATTACHMENT_STORE_OP_STORE,
                                 VK_IMAGE_LAYOUT_UNDEFINED,
                                 VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                 VK_FORMAT_UNDEFINED,
                                 VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                 VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                 VK_IMAGE_LAYOUT_UNDEFINED,
                                 VK_IMAGE_LAYOUT_UNDEFINED);

   res->framebuffers =
      vkdf_create_framebuffers_for_swap_chain(ctx, res->render_pass, 0, NULL);

   res->pipeline_layout = create_empty_pipeline_layout(ctx);

   res->pipeline = vkdf_create_gfx_pipeline(ctx, NULL,
                                            /* No vertex bindings */
                                            0, NULL,
                                            0, NULL,
                                            /* Disable depth testing */
                                            false, VK_COMPARE_OP_ALWAYS,
                                            res->render_pass,
                                            res->pipeline_layout,
                                            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                            VK_CULL_MODE_NONE,
                                            1,
                                            res->vs_module,
                                            res->fs_module);

   res->cmd_pool = vkdf_create_gfx_command_pool(ctx, 0);

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

void
cleanup_resources(VkdfContext *ctx, DemoResources *res)
{
   vkDestroyPipelineLayout(ctx->device, res->pipeline_layout, NULL);
   vkDestroyPipeline(ctx->device, res->pipeline, NULL);

   vkDestroyRenderPass(ctx->device, res->render_pass, NULL);

   for (uint32_t i = 0; i < ctx->swap_chain_length; i++)
      vkDestroyFramebuffer(ctx->device, res->framebuffers[i], NULL);
   g_free(res->framebuffers);

   vkDestroyShaderModule(ctx->device, res->vs_module, NULL);
   vkDestroyShaderModule(ctx->device, res->fs_module, NULL);

   vkFreeCommandBuffers(ctx->device,
                        res->cmd_pool,
                        ctx->swap_chain_length,
                        res->cmd_bufs);
   vkDestroyCommandPool(ctx->device, res->cmd_pool, NULL);
}

int
main()
{
   VkdfContext ctx;
   DemoResources resources;

   vkdf_init(&ctx, 800, 600, false, false, true);
   init_resources(&ctx, &resources);

   vkdf_event_loop_run(&ctx, scene_update, scene_render, &resources);

   cleanup_resources(&ctx, &resources);
   vkdf_cleanup(&ctx);

   return 0;
}
