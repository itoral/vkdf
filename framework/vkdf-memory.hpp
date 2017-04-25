#ifndef __VKDF_MEMORY_H__
#define __VKDF_MEMORY_H__

bool vkdf_memory_type_from_properties(VkdfContext *ctx,
                                      uint32_t type_bits,
                                      VkFlags requirements_mask,
                                      uint32_t *type_index);

#endif
