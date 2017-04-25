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


