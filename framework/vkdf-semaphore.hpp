#ifndef __VKDF_SEMAPHORE_H__
#define __VKDF_SEMAPHORE_H__

#include "vkdf-deps.hpp"
#include "vkdf-init.hpp"

VkSemaphore
vkdf_create_semaphore(VkdfContext *ctx);

VkFence
vkdf_create_fence(VkdfContext *ctx);

#endif
