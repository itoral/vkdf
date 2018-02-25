#ifndef __VKDF_SAMPLER_H__
#define __VKDF_SAMPLER_H__

#include "vkdf-deps.hpp"
#include "vkdf-init.hpp"

VkSampler
vkdf_create_sampler(VkdfContext *ctx,
                    VkSamplerAddressMode address_mode,
                    VkFilter filter,
                    VkSamplerMipmapMode mipmap_mode,
                    float max_anisotropy);

VkSampler
vkdf_create_shadow_sampler(VkdfContext *ctx,
                           VkSamplerAddressMode address_mode,
                           VkFilter filter,
                           VkSamplerMipmapMode mipmap_mode);

#endif
