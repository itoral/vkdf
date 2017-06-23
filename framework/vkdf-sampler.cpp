#include "vkdf.hpp"

VkSampler
vkdf_create_sampler(VkdfContext *ctx,
                    VkSamplerAddressMode address_mode,
                    VkFilter filter,
                    VkSamplerMipmapMode mipmap_mode)
{
   VkSampler sampler;
   VkResult result;

   VkSamplerCreateInfo sampler_info = {};
   sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
   sampler_info.addressModeU = address_mode;
   sampler_info.addressModeV = address_mode;
   sampler_info.addressModeW = address_mode;
   sampler_info.anisotropyEnable = false;
   sampler_info.maxAnisotropy = 1.0f;
   sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
   sampler_info.unnormalizedCoordinates = false;
   sampler_info.compareEnable = false;
   sampler_info.compareOp = VK_COMPARE_OP_ALWAYS;
   sampler_info.magFilter = filter;
   sampler_info.minFilter = filter;
   sampler_info.mipmapMode = mipmap_mode;
   sampler_info.mipLodBias = 0.0f;
   sampler_info.minLod = 0.0f;
   sampler_info.maxLod = 100.0f;

   result = vkCreateSampler(ctx->device, &sampler_info, NULL, &sampler);
   if (result != VK_SUCCESS)
      vkdf_fatal("Failed to create sampler");

   return sampler;
}
