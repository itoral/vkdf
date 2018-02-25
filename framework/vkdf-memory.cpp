#include "vkdf-memory.hpp"

bool
vkdf_memory_type_from_properties(VkdfContext *ctx,
                                 uint32_t allowed_mem_types,
                                 VkFlags requirements_mask,
                                 uint32_t *mem_type_index)
{
   for (uint32_t i = 0; i < ctx->phy_device_mem_props.memoryTypeCount; i++) {
      if ((allowed_mem_types & 1) == 1) {
         if ((ctx->phy_device_mem_props.memoryTypes[i].propertyFlags &
             requirements_mask) == requirements_mask) {
            *mem_type_index = i;
            return true;
         }
      }
      allowed_mem_types >>= 1;
   }

   return false;
}
