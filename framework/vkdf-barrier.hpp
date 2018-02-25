#ifndef __VKDF_BARRIER_H__
#define __VKDF_BARRIER_H__

#include "vkdf-deps.hpp"

VkImageMemoryBarrier
vkdf_create_image_barrier(VkAccessFlags src_access_mask,
                          VkAccessFlags dst_access_mask,
                          VkImageLayout old_layout,
                          VkImageLayout new_layout,
                          VkImage image,
                          VkImageSubresourceRange subresource_range);

VkBufferMemoryBarrier
vkdf_create_buffer_barrier(VkAccessFlags src_access_mask,
                           VkAccessFlags dst_access_mask,
                           VkBuffer buf,
                           VkDeviceSize offset,
                           VkDeviceSize size);
#endif
