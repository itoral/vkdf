#ifndef __VKDF_BUFFER_H__
#define __VKDF_BUFFER_H__

typedef struct {
   VkBuffer buf;
   VkMemoryRequirements mem_reqs;
   VkDeviceMemory mem;
   uint32_t mem_props;
} VkdfBuffer;

VkdfBuffer
vkdf_create_buffer(VkdfContext *ctx,
                   VkBufferCreateFlags flags,
                   VkDeviceSize size,
                   VkBufferUsageFlags usage,
                   uint32_t mem_props);

void
vkdf_buffer_map_and_fill(VkdfContext *ctx,
                         VkdfBuffer buf,
                         VkDeviceSize offset,
                         VkDeviceSize size,
                         const void *data);

void
vkdf_buffer_map_and_fill_elements(VkdfContext *ctx,
                                  VkdfBuffer buf,
                                  VkDeviceSize offset,
                                  uint32_t num_elements,
                                  uint32_t element_size,
                                  uint32_t src_stride,
                                  uint32_t dst_stride,
                                  const void *data);

void
vkdf_buffer_map_and_get(VkdfContext *ctx,
                        VkdfBuffer buf,
                        VkDeviceSize offset,
                        VkDeviceSize size,
                        void *data);
void
vkdf_destroy_buffer(VkdfContext *ctx, VkdfBuffer *buf);

#endif
