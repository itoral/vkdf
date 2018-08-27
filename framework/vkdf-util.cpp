#include "vkdf-util.hpp"

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

glm::mat4
vkdf_compute_view_matrix_for_direction(glm::vec3 dir)
{
   glm::mat4 view(1.0f);

   vkdf_vec3_normalize(&dir);

   float pitch = acosf(vkdf_vec3_module(dir, 1, 0, 1));
   view = glm::rotate(view, pitch, glm::vec3(1.0f, 0.0f, 0.0f));

   float yaw = atanf(dir.x / dir.z);
   yaw = dir.z > 0 ? yaw - PI : yaw;
   view = glm::rotate(view, -yaw, glm::vec3(0.0f, 1.0f, 0.0f));

   return view;
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

/**
 * Computes a model matrix
 *
 * When 'rot_origin_offset' is the origin (default), the rotation applies to
 * the object's center. Otherwise, it applies around the object's center
 * plus this offset.
 */
glm::mat4
vkdf_compute_model_matrix(glm::vec3 pos, glm::vec3 rot, glm::vec3 scale,
                          glm::vec3 rot_origin_offset)
{
   glm::mat4 m = glm::mat4(1.0f);

   m = glm::translate(m, pos);

   if (rot_origin_offset != glm::vec3(0.0f))
      m = glm::translate(m, rot_origin_offset);

   if (rot.x != 0.0f || rot.y != 0.0f || rot.z != 0.0f) {
      glm::vec3 rot_rad = glm::vec3(DEG_TO_RAD(rot.x),
                                    DEG_TO_RAD(rot.y),
                                    DEG_TO_RAD(rot.z));
      glm::tquat<float> quat = glm::quat(rot_rad);
      glm::mat4 rot_matrix = glm::toMat4(quat);
      m = m * rot_matrix;
   }

   if (rot_origin_offset != glm::vec3(0.0f))
      m = glm::translate(m, -rot_origin_offset);

   if (scale.x != 1.0f || scale.y != 1.0f || scale.z != 1.0f)
      m = glm::scale(m, scale);

   return m;
}
