#ifndef __VKDF_PLANE_H__
#define __VKDF_PLANE_H__

typedef struct {
   float a;
   float b;
   float c;
   float d;
} VkdfPlane;

void
vkdf_plane_from_points(VkdfPlane *plane,
                       glm::vec3 p0, glm::vec3 p1, glm::vec3 p2);

inline float
vkdf_plane_distance_from_point(VkdfPlane *plane, glm::vec3 p)
{
   return plane->a * p.x + plane->b * p.y + plane->c * p.z + plane->d;
}

#endif
