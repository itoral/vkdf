#include "vkdf.hpp"

VkdfBuffer
vkdf_create_buffer(VkdfContext *ctx,
                   VkBufferCreateFlags flags,
                   VkDeviceSize size,
                   VkBufferUsageFlags usage,
                   uint32_t mem_props)
{
   VkdfBuffer buffer;

   // Create buffer object
   VkBufferCreateInfo buf_info;
   buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
   buf_info.pNext = NULL;
   buf_info.usage = usage;
   buf_info.size = size;
   buf_info.queueFamilyIndexCount = 0;
   buf_info.pQueueFamilyIndices = NULL;
   buf_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
   buf_info.flags = flags;

   VK_CHECK(vkCreateBuffer(ctx->device, &buf_info, NULL, &buffer.buf));

   // Look for suitable memory heap
   vkGetBufferMemoryRequirements(ctx->device, buffer.buf, &buffer.mem_reqs);

   VkMemoryAllocateInfo alloc_info;
   alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
   alloc_info.pNext = NULL;
   alloc_info.allocationSize = buffer.mem_reqs.size;
   assert(vkdf_memory_type_from_properties(ctx, buffer.mem_reqs.memoryTypeBits,
                                           mem_props,
                                           &alloc_info.memoryTypeIndex));

   buffer.mem_props = mem_props;

   // Allocate and bind memory
   VK_CHECK(vkAllocateMemory(ctx->device, &alloc_info, NULL, &buffer.mem));
   VK_CHECK(vkBindBufferMemory(ctx->device, buffer.buf, buffer.mem, 0));

   return buffer;
}

void
vkdf_buffer_map_and_fill(VkdfContext *ctx,
                         VkdfBuffer buf,
                         VkDeviceSize offset,
                         VkDeviceSize size,
                         const void *data)
{
   assert(buf.mem_props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

   void *mapped_memory;
   VK_CHECK(vkMapMemory(ctx->device, buf.mem, offset, size, 0, &mapped_memory));

   assert(buf.mem_reqs.size >= size);
   memcpy(mapped_memory, data, size);

   if (!(buf.mem_props & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
      VkMappedMemoryRange range;
      range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
      range.pNext = NULL;
      range.memory = buf.mem;
      range.offset = offset;
      range.size = size;
      VK_CHECK(vkFlushMappedMemoryRanges(ctx->device, 1, &range));
   }

   vkUnmapMemory(ctx->device, buf.mem);
}

void
vkdf_buffer_map_and_fill_elements(VkdfContext *ctx,
                                  VkdfBuffer buf,
                                  VkDeviceSize offset,
                                  uint32_t num_elements,
                                  uint32_t element_size,
                                  uint32_t src_stride,
                                  uint32_t dst_stride,
                                  const void *data)
{
   assert(buf.mem_props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
   assert(dst_stride >= element_size);

   void *mapped_memory;
   VkDeviceSize size = num_elements * dst_stride;
   assert(buf.mem_reqs.size >= size);

   VK_CHECK(vkMapMemory(ctx->device, buf.mem, offset, size, 0, &mapped_memory));


   VkDeviceSize dst_offset = 0;
   VkDeviceSize src_offset = 0;
   for (uint32_t i = 0; i < num_elements; i++) {
      memcpy((uint8_t *)mapped_memory + dst_offset,
             (uint8_t *)data + src_offset, element_size);
      dst_offset += dst_stride;
      src_offset += src_stride;
   }

   if (!(buf.mem_props & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
      VkMappedMemoryRange range;
      range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
      range.pNext = NULL;
      range.memory = buf.mem;
      range.offset = offset;
      range.size = size;
      VK_CHECK(vkFlushMappedMemoryRanges(ctx->device, 1, &range));
   }

   vkUnmapMemory(ctx->device, buf.mem);
}

void
vkdf_destroy_buffer(VkdfContext *ctx, VkdfBuffer *buf)
{
   vkDestroyBuffer(ctx->device, buf->buf, NULL);
   vkFreeMemory(ctx->device, buf->mem, NULL);
}
