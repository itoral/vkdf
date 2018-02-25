#include "vkdf-deps.hpp"
#include "vkdf-plane.hpp"
#include "vkdf-util.hpp"

void
vkdf_plane_from_points(VkdfPlane *plane,
                       glm::vec3 p0, glm::vec3 p1, glm::vec3 p2)
{
   glm::vec3 v = p1 - p0;
   glm::vec3 u = p2 - p0;
   glm::vec3 n = vkdf_vec3_cross(u, v);
   vkdf_vec3_normalize(&n);

   plane->a = n.x;
   plane->b = n.y;
   plane->c = n.z;
   plane->d = -vkdf_vec3_dot(n, p0);
}

