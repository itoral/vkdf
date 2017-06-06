#ifndef __VKDF_DESCRIPTOR_H__
#define __VKDF_DESCRIPTOR_H__

VkDescriptorPool
vkdf_create_descriptor_pool(VkdfContext *ctx,
                            VkDescriptorType type,
                            uint32_t count);

VkDescriptorSetLayout
vkdf_create_buffer_descriptor_set_layout(VkdfContext *ctx,
                                         uint32_t binding,
                                         uint32_t count,
                                         VkShaderStageFlags stages,
                                         VkDescriptorType type);

VkDescriptorSetLayout
vkdf_create_sampler_descriptor_set_layout(VkdfContext *ctx,
                                          uint32_t binding,
                                          uint32_t count,
                                          VkShaderStageFlags stages);

void
vkdf_descriptor_set_buffer_update(VkdfContext *ctx,
                                  VkDescriptorSet descriptor,
                                  VkBuffer buffer,
                                  uint32_t binding,
                                  uint32_t count,
                                  VkDeviceSize *offsets,
                                  VkDeviceSize *ranges,
                                  VkDescriptorType type);
void
vkdf_descriptor_set_sampler_update(VkdfContext *ctx,
                                   VkDescriptorSet descriptor,
                                   VkSampler sampler,
                                   VkImageView view,
                                   VkImageLayout layout);

#endif
