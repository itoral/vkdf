#ifndef __VKDF_MEMORY_H__
#define __VKDF_MEMORY_H__

bool vkdf_memory_type_from_properties(VkdfContext *ctx,
                                      uint32_t type_bits,
                                      VkFlags requirements_mask,
                                      uint32_t *type_index);

inline void
vkdf_memory_map(VkdfContext *ctx,
                VkDeviceMemory mem,
                VkDeviceSize offset,
                VkDeviceSize size,
                void **ptr)
{
   VK_CHECK(vkMapMemory(ctx->device, mem, offset, size, 0, ptr));
}

inline void
vkdf_memory_unmap(VkdfContext *ctx,
                  VkDeviceMemory mem,
                  uint32_t mem_props,
                  VkDeviceSize offset,
                  VkDeviceSize size)
{
   if (!(mem_props & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
      VkMappedMemoryRange range;
      range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
      range.pNext = NULL;
      range.memory = mem;
      range.offset = offset;
      range.size = size;
      VK_CHECK(vkFlushMappedMemoryRanges(ctx->device, 1, &range));
   }

   vkUnmapMemory(ctx->device, mem);
}

#endif
