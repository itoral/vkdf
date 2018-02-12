#include "vkdf.hpp"

VkCommandPool
vkdf_create_gfx_command_pool(VkdfContext *ctx,
                             VkCommandPoolCreateFlags flags)
{
   VkCommandPool cmd_pool;

   VkCommandPoolCreateInfo cmd_pool_info;
   cmd_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
   cmd_pool_info.pNext = NULL;
   cmd_pool_info.flags = flags;
   cmd_pool_info.queueFamilyIndex = ctx->gfx_queue_index;

   VK_CHECK(vkCreateCommandPool(ctx->device, &cmd_pool_info, NULL, &cmd_pool));

   return cmd_pool;
}

void
vkdf_create_command_buffer(VkdfContext *ctx,
                           VkCommandPool cmd_pool,
                           VkCommandBufferLevel level,
                           uint32_t cmd_count,
                           VkCommandBuffer *cmd_bufs)
{
   VkCommandBufferAllocateInfo cmd_info;
   cmd_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
   cmd_info.pNext = NULL;
   cmd_info.commandPool = cmd_pool;
   cmd_info.level = level;
   cmd_info.commandBufferCount = cmd_count;

   VK_CHECK(vkAllocateCommandBuffers(ctx->device, &cmd_info, cmd_bufs));
}

void
vkdf_command_buffer_begin(VkCommandBuffer cmd_buf,
                          VkCommandBufferUsageFlags flags)
{
   VkCommandBufferBeginInfo cmd_buf_info;
   cmd_buf_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
   cmd_buf_info.pNext = NULL;
   cmd_buf_info.flags = flags;
   cmd_buf_info.pInheritanceInfo = NULL;

   VK_CHECK(vkBeginCommandBuffer(cmd_buf, &cmd_buf_info));
}

void
vkdf_command_buffer_begin_secondary(VkCommandBuffer cmd_buf,
                                    VkCommandBufferUsageFlags flags,
                                    VkCommandBufferInheritanceInfo *inheritance)
{
   VkCommandBufferBeginInfo cmd_buf_info;
   cmd_buf_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
   cmd_buf_info.pNext = NULL;
   cmd_buf_info.flags = flags;
   cmd_buf_info.pInheritanceInfo = inheritance;

   VK_CHECK(vkBeginCommandBuffer(cmd_buf, &cmd_buf_info));
}

void
vkdf_command_buffer_end(VkCommandBuffer cmd_buf)
{
   VK_CHECK(vkEndCommandBuffer(cmd_buf));
}

void
vkdf_command_buffer_execute(VkdfContext *ctx,
                            VkCommandBuffer cmd_buf,
                            VkPipelineStageFlags *pipeline_stage_flags,
                            uint32_t wait_sem_count,
                            VkSemaphore *wait_sem,
                            uint32_t signal_sem_count,
                            VkSemaphore *signal_sem)
{
   VkSubmitInfo submit_info = { };
   submit_info.pNext = NULL;
   submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
   submit_info.waitSemaphoreCount = wait_sem_count;
   submit_info.pWaitSemaphores = wait_sem;
   submit_info.signalSemaphoreCount = signal_sem_count;
   submit_info.pSignalSemaphores = signal_sem;
   submit_info.pWaitDstStageMask = pipeline_stage_flags;
   submit_info.commandBufferCount = 1;
   submit_info.pCommandBuffers = &cmd_buf;

   VK_CHECK(vkQueueSubmit(ctx->gfx_queue, 1, &submit_info, NULL));
}

void
vkdf_command_buffer_execute_with_fence(VkdfContext *ctx,
                                       VkCommandBuffer cmd_buf,
                                       VkPipelineStageFlags *pipeline_stage_flags,
                                       uint32_t wait_sem_count,
                                       VkSemaphore *wait_sem,
                                       uint32_t signal_sem_count,
                                       VkSemaphore *signal_sem,
                                       VkFence fence)
{
   VkSubmitInfo submit_info = { };
   submit_info.pNext = NULL;
   submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
   submit_info.waitSemaphoreCount = wait_sem_count;
   submit_info.pWaitSemaphores = wait_sem;
   submit_info.signalSemaphoreCount = signal_sem_count;
   submit_info.pSignalSemaphores = signal_sem;
   submit_info.pWaitDstStageMask = pipeline_stage_flags;
   submit_info.commandBufferCount = 1;
   submit_info.pCommandBuffers = &cmd_buf;

   VK_CHECK(vkQueueSubmit(ctx->gfx_queue, 1, &submit_info, fence));
}

void
vkdf_command_buffer_execute_many(VkdfContext *ctx,
                                 VkCommandBuffer *cmd_bufs,
                                 uint32_t count,
                                 VkPipelineStageFlags *pipeline_stage_flags,
                                 uint32_t wait_sem_count,
                                 VkSemaphore *wait_sem,
                                 uint32_t signal_sem_count,
                                 VkSemaphore *signal_sem)
{
   VkSubmitInfo submit_info = { };
   submit_info.pNext = NULL;
   submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
   submit_info.waitSemaphoreCount = wait_sem_count;
   submit_info.pWaitSemaphores = wait_sem;
   submit_info.signalSemaphoreCount = signal_sem_count;
   submit_info.pSignalSemaphores = signal_sem;
   submit_info.pWaitDstStageMask = pipeline_stage_flags;
   submit_info.commandBufferCount = count;
   submit_info.pCommandBuffers = cmd_bufs;

   VK_CHECK(vkQueueSubmit(ctx->gfx_queue, 1, &submit_info, NULL));
}

void
vkdf_command_buffer_execute_sync(VkdfContext *ctx,
                                 VkCommandBuffer cmd_buf,
                                 VkPipelineStageFlags pipeline_stage_flags)
{
   VkSubmitInfo submit_info = { };
   submit_info.pNext = NULL;
   submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
   submit_info.waitSemaphoreCount = 0;
   submit_info.pWaitSemaphores = NULL;
   submit_info.signalSemaphoreCount = 0;
   submit_info.pSignalSemaphores = NULL;
   submit_info.pWaitDstStageMask = &pipeline_stage_flags;
   submit_info.commandBufferCount = 1;
   submit_info.pCommandBuffers = &cmd_buf;

   VkFence fence;
   VkFenceCreateInfo fence_info = { };
   fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
   fence_info.pNext = NULL;
   fence_info.flags = 0;
   VK_CHECK(vkCreateFence(ctx->device, &fence_info, NULL, &fence));

   VK_CHECK(vkQueueSubmit(ctx->gfx_queue, 1, &submit_info, fence));
   VK_CHECK(vkWaitForFences(ctx->device, 1, &fence, true, 100000000000ull));

   vkDestroyFence(ctx->device, fence, NULL);
}

static void
present_commands(VkdfContext *ctx,
                 VkImage image,
                 VkCommandBuffer *cmd_bufs,
                 uint32_t index)
{
   // Transition presentation image to transfer destination layout and source
   // image transfer source layout
   VkImageSubresourceRange subresource_range =
      vkdf_create_image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1);

   VkImageMemoryBarrier src_barrier =
      vkdf_create_image_barrier(VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                VK_ACCESS_TRANSFER_READ_BIT,
                                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                image,
                                subresource_range);

   VkImageMemoryBarrier dst_barrier =
      vkdf_create_image_barrier(0,
                                VK_ACCESS_TRANSFER_WRITE_BIT,
                                VK_IMAGE_LAYOUT_UNDEFINED,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                ctx->swap_chain_images[index].image,
                                subresource_range);

   VkImageMemoryBarrier barriers[2] = {
      src_barrier,
      dst_barrier
   };

   vkCmdPipelineBarrier(cmd_bufs[index],
                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        0,
                        0, NULL,
                        0, NULL,
                        2, barriers);

   // Copy color image to presentation image
   VkImageSubresourceLayers subresource_layers =
      vkdf_create_image_subresource_layers(VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1);

   VkImageBlit region =
      vkdf_create_image_blit_region(subresource_layers,
                                    glm::uvec3(0, 0, 0),
                                    glm::uvec3(ctx->width, ctx->height, 1),
                                    subresource_layers,
                                    glm::uvec3(0, 0, 0),
                                    glm::uvec3(ctx->width, ctx->height, 1));

   vkCmdBlitImage(cmd_bufs[index],
                  image,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  ctx->swap_chain_images[index].image,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  1,
                  &region,
                  VK_FILTER_NEAREST);

   // Transition presentation image to presentation layout
   VkImageMemoryBarrier present_barrier =
      vkdf_create_image_barrier(VK_ACCESS_TRANSFER_WRITE_BIT,
                                VK_ACCESS_MEMORY_READ_BIT,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                ctx->swap_chain_images[index].image,
                                subresource_range);

   vkCmdPipelineBarrier(cmd_bufs[index],
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                        0,
                        0, NULL,
                        0, NULL,
                        1, &present_barrier);
}

/**
 * Creates required command buffers to present an image
 */
VkCommandBuffer *
vkdf_command_buffer_create_for_present(VkdfContext *ctx,
                                       VkCommandPool cmd_pool,
                                       VkImage image)
{
   VkCommandBuffer *cmd_bufs = g_new(VkCommandBuffer, ctx->swap_chain_length);
   vkdf_create_command_buffer(ctx,
                              cmd_pool,
                              VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                              ctx->swap_chain_length,
                              cmd_bufs);

   for (uint32_t i = 0; i < ctx->swap_chain_length; i++) {
      vkdf_command_buffer_begin(cmd_bufs[i],
                                VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT);
      present_commands(ctx, image, cmd_bufs, i);
      vkdf_command_buffer_end(cmd_bufs[i]);
   }

   return cmd_bufs;
}
