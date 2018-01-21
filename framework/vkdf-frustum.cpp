#include "vkdf.hpp"

void
vkdf_frustum_compute_vertices(glm::vec3 origin,
                              glm::vec3 rot,
                              float near_dist,
                              float far_dist,
                              float fov,
                              float aspect_ratio,
                              glm::vec3 *f)
{
   /* Vulkan camera looks at -Z */
   glm::mat4 rot_matrix = vkdf_compute_rotation_matrix(rot);
   glm::vec3 forward_vector =
      vec3(rot_matrix * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f));

   glm::vec3 to_far = forward_vector * far_dist;
   glm::vec3 to_near = forward_vector * near_dist;
   glm::vec3 center_far = origin + to_far;
   glm::vec3 center_near = origin + to_near;

   glm::vec3 up_vector = vec3(rot_matrix * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
   glm::vec3 right_vector = glm::cross(forward_vector, up_vector);
   vkdf_vec3_normalize(&up_vector);
   vkdf_vec3_normalize(&right_vector);

   float t = tanf(DEG_TO_RAD(fov / 2.0f));
   float far_height = far_dist * t;
   float far_width = far_height * aspect_ratio;
   float near_width = near_dist * t;
   float near_height = near_width / aspect_ratio;

   glm::vec3 far_top = center_far + up_vector * far_height;
   glm::vec3 far_bottom = center_far + up_vector * (-far_height);
   glm::vec3 near_top = center_near + up_vector * near_height;
   glm::vec3 near_bottom = center_near + up_vector * (-near_height);

   f[FRUSTUM_FTR] = far_top + right_vector * far_width;
   f[FRUSTUM_FTL] = far_top + right_vector * (-far_width);
   f[FRUSTUM_FBR] = far_bottom + right_vector * far_width;
   f[FRUSTUM_FBL] = far_bottom + right_vector * (-far_width);

   f[FRUSTUM_NTR] = near_top + right_vector * near_width;
   f[FRUSTUM_NTL] = near_top + right_vector * (-near_width);
   f[FRUSTUM_NBR] = near_bottom + right_vector * near_width;
   f[FRUSTUM_NBL] = near_bottom + right_vector * (-near_width);
}

void
vkdf_frustum_compute_planes(glm::vec3 *f, VkdfPlane *p)
{
   vkdf_plane_from_points(&p[FRUSTUM_FAR],
                          f[FRUSTUM_FTL], f[FRUSTUM_FTR], f[FRUSTUM_FBR]);

   vkdf_plane_from_points(&p[FRUSTUM_NEAR],
                          f[FRUSTUM_NTL], f[FRUSTUM_NBR], f[FRUSTUM_NTR]);

   vkdf_plane_from_points(&p[FRUSTUM_LEFT],
                          f[FRUSTUM_NTL], f[FRUSTUM_FTL], f[FRUSTUM_FBL]);

   vkdf_plane_from_points(&p[FRUSTUM_RIGHT],
                          f[FRUSTUM_NTR], f[FRUSTUM_FBR], f[FRUSTUM_FTR]);

   vkdf_plane_from_points(&p[FRUSTUM_TOP],
                          f[FRUSTUM_NTL], f[FRUSTUM_FTR], f[FRUSTUM_FTL]);

   vkdf_plane_from_points(&p[FRUSTUM_BOTTOM],
                          f[FRUSTUM_NBL], f[FRUSTUM_FBL], f[FRUSTUM_FBR]);
}

void
vkdf_frustum_compute_clip_box(glm::vec3 *f, VkdfBox *box)
{
   glm::vec3 box_min = glm::vec3(f[0].x, f[0].y, f[0].z);
   glm::vec3 box_max = glm::vec3(f[0].x, f[0].y, f[0].z);

   for (int i = 1; i < 8; i++) {
      glm::vec3 *v = &f[i];
      if (v->x < box_min.x)
         box_min.x = v->x;
      else if (v->x > box_max.x)
         box_max.x = v->x;

      if (v->y < box_min.y)
         box_min.y = v->y;
      else if (v->y > box_max.y)
         box_max.y = v->y;

      if (v->z < box_min.z)
         box_min.z = v->z;
      else if (v->z > box_max.z)
         box_max.z = v->z;
   }

   box->w = (box_max.x - box_min.x) / 2.0f;
   box->h = (box_max.y - box_min.y) / 2.0f;
   box->d = (box_max.z - box_min.z) / 2.0f;
   box->center = glm::vec3(box_min.x + box->w,
                           box_min.y + box->h,
                           box_min.z + box->d);
}
