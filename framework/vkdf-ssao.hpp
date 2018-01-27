#ifndef __VKDF_SSAO_H__
#define __VKDF_SSAO_H__

void
vkdf_ssao_gen_tangent_samples(uint32_t num_samples,
                              std::vector<glm::vec3> *samples);

void
vkdf_ssao_gen_noise_samples(uint32_t num_samples,
                            std::vector<glm::vec3> *samples);

#endif
