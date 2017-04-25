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

