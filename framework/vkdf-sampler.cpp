#include "vkdf-sampler.hpp"
#include "vkdf-error.hpp"

VkSampler
vkdf_create_sampler(VkdfContext *ctx,
                    VkSamplerAddressMode address_mode,
                    VkFilter filter,
                    VkSamplerMipmapMode mipmap_mode,
                    float max_anisotropy)
{
   VkSampler sampler;
   VkResult result;

   if (max_anisotropy >= 1.0f) {
      const VkPhysicalDeviceLimits *limits = &ctx->phy_device_props.limits;
      if (!ctx->device_features.samplerAnisotropy) {
         max_anisotropy = 0.0f;  /* disabled */
         vkdf_error("sampler: ignoring request for anisotropic filtering. "
                    "Feature is not enabled or is unsupported.");
      }
      else if (max_anisotropy > limits->maxSamplerAnisotropy) {
         max_anisotropy = limits->maxSamplerAnisotropy;
         vkdf_info("sampler: clamped maxAnisotropy to %.1f.", max_anisotropy);
      }
   }

   VkSamplerCreateInfo sampler_info = {};
   sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
   sampler_info.addressModeU = address_mode;
   sampler_info.addressModeV = address_mode;
   sampler_info.addressModeW = address_mode;
   sampler_info.anisotropyEnable = max_anisotropy >= 1.0f;
   sampler_info.maxAnisotropy = max_anisotropy;
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

VkSampler
vkdf_create_shadow_sampler(VkdfContext *ctx,
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
   sampler_info.compareEnable = true;
   sampler_info.compareOp = VK_COMPARE_OP_LESS;
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

