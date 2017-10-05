#ifndef __VKDF_SAMPLER_H__
#define __VKDF_SAMPLER_H__

VkSampler
vkdf_create_sampler(VkdfContext *ctx,
                    VkSamplerAddressMode address_mode,
                    VkFilter filter,
                    VkSamplerMipmapMode mipmap_mode);

VkSampler
vkdf_create_shadow_sampler(VkdfContext *ctx,
                           VkSamplerAddressMode address_mode,
                           VkFilter filter,
                           VkSamplerMipmapMode mipmap_mode);

#endif
