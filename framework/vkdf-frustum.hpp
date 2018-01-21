#ifndef __VKDF_FRUSTUM_H__
#define __VKDF_FRUSTUM_H__

void
vkdf_frustum_compute_vertices(glm::vec3 origin,
                              glm::vec3 rot,
                              float near_dist,
                              float far_dist,
                              float fov,
                              float aspect_ratio,
                              glm::vec3 *f);

void
vkdf_frustum_compute_planes(glm::vec3 *f, VkdfPlane *p);

void
vkdf_frustum_compute_clip_box(glm::vec3 *f, VkdfBox *box);

#endif
