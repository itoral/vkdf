#include "vkdf.hpp"

/**
 * Computes rotation angles for origin to be looking at target
 */
glm::vec3
vkdf_compute_view_rotation(glm::vec3 origin, glm::vec3 target)
{
   glm::vec3 vt;
   float dist;
   float cosAngle, sinAngle, angle;
   glm::vec3 rot;

   /* Compute rotation angles */
   vt.x = target.x - origin.x;
   vt.y = target.y - origin.y;
   vt.z = target.z - origin.z;

   dist = vkdf_vec3_module(vt, 1, 0, 1);
   if (dist > 0.0f) {
      cosAngle = vt.x / dist;
      angle = acos(cosAngle);
      angle = RAD_TO_DEG(angle) - 90.0f;
      if (vt.z > 0.0f)
        angle += (90.0f - angle) * 2.0f;
      rot.y = angle;
   } else {
      rot.y = 0.0f;
   }

   dist = vkdf_vec3_module(vt, 1, 1, 1);
   if (dist > 0.0f) {
      sinAngle = vt.y / dist;
      angle = asin(sinAngle);
      angle = RAD_TO_DEG(angle);
      rot.x = angle;
   } else {
      rot.x = 0.0f;
   }

   // FIXME: support rotation in Z
   rot.z = 0.0f;
   return rot;
}

/**
 * Computes the view matrix for origin to be looking at target
 */
glm::mat4
vkdf_compute_view_matrix(glm::vec3 origin, glm::vec3 target)
{
   glm::vec3 rot = vkdf_compute_view_rotation(origin, target);
   float rx = DEG_TO_RAD(rot.x);
   float ry = DEG_TO_RAD(rot.y);
   float rz = DEG_TO_RAD(rot.z);

   glm::mat4 mat(1.0);
   mat = glm::rotate(mat, -rx, glm::vec3(1, 0, 0));
   mat = glm::rotate(mat, -ry, glm::vec3(0, 1, 0));
   mat = glm::rotate(mat, -rz, glm::vec3(0, 0, 1));
   mat = glm::translate(mat, -origin);

   return mat;
}

/**
 * Computes the view matrix for origin viewng in the direction
 * specified by rotation angles on each axis
 */
glm::mat4
vkdf_compute_view_matrix_for_rotation(glm::vec3 origin, glm::vec3 rot)
{
   glm::mat4 mat(1.0);
   float rx = DEG_TO_RAD(rot.x);
   float ry = DEG_TO_RAD(rot.y);
   float rz = DEG_TO_RAD(rot.z);
   mat = glm::rotate(mat, -rx, glm::vec3(1, 0, 0));
   mat = glm::rotate(mat, -ry, glm::vec3(0, 1, 0));
   mat = glm::rotate(mat, -rz, glm::vec3(0, 0, 1));
   mat = glm::translate(mat, -origin);
   return mat;
}

/**
 * Compute rotation matrix for a given rotation vector.
 */
glm::mat4
vkdf_compute_rotation_matrix(glm::vec3 rot)
{
   glm::mat4 mat(1.0);
   float rx = DEG_TO_RAD(rot.x);
   float ry = DEG_TO_RAD(rot.y);
   float rz = DEG_TO_RAD(rot.z);
   mat = glm::rotate(mat, rz, glm::vec3(0, 0, 1));
   mat = glm::rotate(mat, ry, glm::vec3(0, 1, 0));
   mat = glm::rotate(mat, rx, glm::vec3(1, 0, 0));
   return mat;
}

/**
 * Compute view vector from rotation angles
 */
glm::vec3
vkdf_compute_viewdir(glm::vec3 rot)
{
   glm::vec3 v1, v2;

   /* Rotate around Y-axis */
   float angle = DEG_TO_RAD(rot.y + 90.0);
   v1.x =  cos(angle);
   v1.z = -sin(angle);

   /* Rotate around X-axis */
   angle = DEG_TO_RAD(rot.x);
   float cosX = cos(angle);
   v2.x = v1.x * cosX;
   v2.z = v1.z * cosX;
   v2.y = sin(angle);

   /* FIXME: Rotate around Z-axis (not supportted!) */
   assert(rot.z == 0.0f);

   return v2;
}

void
vkdf_compute_frustum_vertices(glm::vec3 origin,
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
vkdf_compute_frustum_planes(glm::vec3 *f, VkdfPlane *p)
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
