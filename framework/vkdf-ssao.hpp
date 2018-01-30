#ifndef __VKDF_SSAO_H__
#define __VKDF_SSAO_H__

void
vkdf_ssao_gen_tangent_samples(uint32_t num_samples,
                              std::vector<glm::vec3> *samples);

void
vkdf_ssao_gen_noise_samples(uint32_t num_samples,
                            std::vector<glm::vec3> *samples);

void
vkdf_ssao_gen_noise_image(VkdfContext *ctx,
                          VkCommandPool pool,
                          uint32_t width,
                          uint32_t height,
                          const std::vector<glm::vec3> *samples,
                          VkdfImage *image);

inline VkSampler
vkdf_ssao_create_noise_sampler(VkdfContext *ctx)
{
   return vkdf_create_sampler(ctx,
                              VK_SAMPLER_ADDRESS_MODE_REPEAT,
                              VK_FILTER_NEAREST,
                              VK_SAMPLER_MIPMAP_MODE_NEAREST,
                              0.0f);
}

inline VkSampler
vkdf_ssao_create_ssao_sampler(VkdfContext *ctx, VkFilter filter)
{
   return vkdf_create_sampler(ctx,
                              VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                              filter,
                              VK_SAMPLER_MIPMAP_MODE_NEAREST,
                              0.0f);
}

inline VkSampler
vkdf_ssao_create_gbuffer_sampler(VkdfContext *ctx)
{
   return vkdf_create_sampler(ctx,
                              VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                              VK_FILTER_NEAREST,
                              VK_SAMPLER_MIPMAP_MODE_NEAREST,
                              0.0f);
}

#endif
