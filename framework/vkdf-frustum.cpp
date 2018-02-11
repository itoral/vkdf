#include "vkdf.hpp"

static void
frustum_compute_vertices(VkdfFrustum *f,
                         const glm::vec3 &origin,
                         const glm::vec3 &rot,
                         float near_dist,
                         float far_dist,
                         float fov,
                         float aspect_ratio)
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

   f->vertices[FRUSTUM_FTR] = far_top + right_vector * far_width;
   f->vertices[FRUSTUM_FTL] = far_top + right_vector * (-far_width);
   f->vertices[FRUSTUM_FBR] = far_bottom + right_vector * far_width;
   f->vertices[FRUSTUM_FBL] = far_bottom + right_vector * (-far_width);

   f->vertices[FRUSTUM_NTR] = near_top + right_vector * near_width;
   f->vertices[FRUSTUM_NTL] = near_top + right_vector * (-near_width);
   f->vertices[FRUSTUM_NBR] = near_bottom + right_vector * near_width;
   f->vertices[FRUSTUM_NBL] = near_bottom + right_vector * (-near_width);
}

void
vkdf_frustum_compute_planes(VkdfFrustum *f)
{
   glm::vec3 *v = f->vertices;
   VkdfPlane *p = f->planes;

   vkdf_plane_from_points(&p[FRUSTUM_FAR],
                          v[FRUSTUM_FTL], v[FRUSTUM_FTR], v[FRUSTUM_FBR]);

   vkdf_plane_from_points(&p[FRUSTUM_NEAR],
                          v[FRUSTUM_NTL], v[FRUSTUM_NBR], v[FRUSTUM_NTR]);

   vkdf_plane_from_points(&p[FRUSTUM_LEFT],
                          v[FRUSTUM_NTL], v[FRUSTUM_FTL], v[FRUSTUM_FBL]);

   vkdf_plane_from_points(&p[FRUSTUM_RIGHT],
                          v[FRUSTUM_NTR], v[FRUSTUM_FBR], v[FRUSTUM_FTR]);

   vkdf_plane_from_points(&p[FRUSTUM_TOP],
                          v[FRUSTUM_NTL], v[FRUSTUM_FTR], v[FRUSTUM_FTL]);

   vkdf_plane_from_points(&p[FRUSTUM_BOTTOM],
                          v[FRUSTUM_NBL], v[FRUSTUM_FBL], v[FRUSTUM_FBR]);

   f->has_planes = true;
}

void
vkdf_frustum_compute_box(VkdfFrustum *f)
{
   glm::vec3 box_min = f->vertices[0];
   glm::vec3 box_max = f->vertices[0];

   for (int i = 1; i < 8; i++) {
      glm::vec3 *v = &f->vertices[i];
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

   f->box.w = (box_max.x - box_min.x) / 2.0f;
   f->box.h = (box_max.y - box_min.y) / 2.0f;
   f->box.d = (box_max.z - box_min.z) / 2.0f;
   f->box.center = glm::vec3(box_min.x + f->box.w,
                             box_min.y + f->box.h,
                             box_min.z + f->box.d);

   f->has_box = true;
}

void
vkdf_frustum_compute(VkdfFrustum *f,
                     bool compute_planes,
                     bool compute_box,
                     const glm::vec3 &origin,
                     const glm::vec3 &rot,
                     float near_dist,
                     float far_dist,
                     float fov,
                     float aspect_ratio)
{
   frustum_compute_vertices(f, origin, rot,
                            near_dist, far_dist, fov, aspect_ratio);

   if (compute_planes)
      vkdf_frustum_compute_planes(f);

   if (compute_box)
      vkdf_frustum_compute_box(f);
}
