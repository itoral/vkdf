#include "vkdf-barrier.hpp"

VkImageMemoryBarrier
vkdf_create_image_barrier(VkAccessFlags src_access_mask,
                          VkAccessFlags dst_access_mask,
                          VkImageLayout old_layout,
                          VkImageLayout new_layout,
                          VkImage image,
                          VkImageSubresourceRange subresource_range)
{
   VkImageMemoryBarrier barrier;
   barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
   barrier.pNext = NULL;
   barrier.srcAccessMask = src_access_mask;
   barrier.dstAccessMask = dst_access_mask;
   barrier.oldLayout = old_layout;
   barrier.newLayout = new_layout;
   barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
   barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
   barrier.image = image;
   barrier.subresourceRange = subresource_range;
   return barrier;
}


VkBufferMemoryBarrier
vkdf_create_buffer_barrier(VkAccessFlags src_access_mask,
                           VkAccessFlags dst_access_mask,
                           VkBuffer buf,
                           VkDeviceSize offset,
                           VkDeviceSize size)
{
   VkBufferMemoryBarrier barrier;
   barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;   
   barrier.pNext = NULL;
   barrier.srcAccessMask = src_access_mask;
   barrier.dstAccessMask = dst_access_mask;
   barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
   barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
   barrier.buffer = buf;
   barrier.offset = offset;
   barrier.size = size;
   return barrier;
}
