#ifndef __VKDF_UTIL_H__
#define __VKDF_UTIL_H__

inline glm::vec3
vec3(glm::vec4 v)
{
   return glm::vec3(v.x, v.y, v.z);
}

inline glm::vec4
vec4(glm::vec3 v, float w)
{
   return glm::vec4(v.x, v.y, v.z, w);
}

static inline float
vkdf_vec3_module(glm::vec3 p, int xaxis, int yaxis, int zaxis)
{
   return sqrtf(p.x * p.x * xaxis + p.y * p.y * yaxis + p.z * p.z * zaxis);
}

glm::vec3
vkdf_compute_view_rotation(glm::vec3 origin, glm::vec3 target);

glm::mat4
vkdf_compute_view_matrix(glm::vec3 origin, glm::vec3 target);

glm::mat4
vkdf_compute_view_matrix_for_rotation(glm::vec3 origin, glm::vec3 rot);

glm::vec3
vkdf_compute_viewdir(glm::vec3 rot);

#endif
