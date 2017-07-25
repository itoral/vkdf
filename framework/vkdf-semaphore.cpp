#include "vkdf.hpp"

VkSemaphore
vkdf_create_semaphore(VkdfContext *ctx)
{
   VkSemaphore sem;

   VkSemaphoreCreateInfo sem_info;
   sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
   sem_info.pNext = NULL;
   sem_info.flags = 0;
   VK_CHECK(vkCreateSemaphore(ctx->device, &sem_info, NULL, &sem));

   return sem;
}

VkFence
vkdf_create_fence(VkdfContext *ctx)
{
   VkFence fence;

   VkFenceCreateInfo fence_info;
   fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
   fence_info.pNext = NULL;
   fence_info.flags = 0;
   VK_CHECK(vkCreateFence(ctx->device, &fence_info, NULL, &fence));

   return fence;
}
