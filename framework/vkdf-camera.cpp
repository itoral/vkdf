#include "vkdf.hpp"

VkdfCamera *
vkdf_camera_new(float px, float py, float pz,
               float rx, float ry, float rz)
{
   VkdfCamera *cam = g_new0(VkdfCamera, 1);
   vkdf_camera_set_position(cam, px, py, pz);
   vkdf_camera_set_rotation(cam, rx, ry, rz);
   return cam;
}

void
vkdf_camera_free(VkdfCamera *cam)
{
   g_free(cam);
}

void
vkdf_camera_set_projection(VkdfCamera *cam,
                           float fov,
                           float near_plane,
                           float far_plane,
                           float aspect_ratio)
{
   cam->proj.fov = fov;
   cam->proj.near_plane = near_plane;
   cam->proj.far_plane = far_plane;
   cam->proj.aspect_ratio = aspect_ratio;

   const glm::mat4 clip = glm::mat4(1.0f,  0.0f, 0.0f, 0.0f,
                                    0.0f, -1.0f, 0.0f, 0.0f,
                                    0.0f,  0.0f, 0.5f, 0.0f,
                                    0.0f,  0.0f, 0.5f, 1.0f);

   cam->proj.matrix = clip * glm::perspective(glm::radians(cam->proj.fov),
                                              cam->proj.aspect_ratio,
                                              cam->proj.near_plane,
                                              cam->proj.far_plane);
   cam->dirty = true;
}

glm::vec3
vkdf_camera_get_position(VkdfCamera *cam)
{
   return cam->pos;
}

void
vkdf_camera_set_position(VkdfCamera *cam, float px, float py, float pz)
{
   cam->pos.x = px;
   cam->pos.y = py;
   cam->pos.z = pz;
   cam->dirty = true;
}

glm::vec3
vkdf_camera_get_rotation(VkdfCamera *cam)
{
   return cam->rot;
}

void
vkdf_camera_set_rotation(VkdfCamera *cam, float rx, float ry, float rz)
{
   cam->rot.x = rx;
   cam->rot.y = ry;
   cam->rot.z = rz;
   cam->dirty = true;
}

void
vkdf_camera_move(VkdfCamera *cam, float dx, float dy, float dz)
{
   cam->pos.x += dx;
   cam->pos.y += dy;
   cam->pos.z += dz;
   cam->dirty = true;
}

void
vkdf_camera_rotate(VkdfCamera *cam, float rx, float ry, float rz)
{
   cam->rot.x += rx;
   cam->rot.y += ry;
   cam->rot.z += rz;

   if (cam->rot.x >= 360.0f)
      cam->rot.x -= 360.0f;
   else if (cam->rot.x <= -360.0f)
      cam->rot.x += 360.0f;

   if (cam->rot.y > 360.0f)
      cam->rot.y -= 360.0f;
   else if (cam->rot.y <= -360.0f)
      cam->rot.y += 360.0f;

   if (cam->rot.z > 360.0f)
      cam->rot.z -= 360.0f;
   else if (cam->rot.z <= -360.0f)
      cam->rot.z += 360.0f;

   cam->dirty = true;
}

/**
 * Move camera along the camera viewing direction
 * StepX, StepY, StepZ enable (1) or disable (0) movement along specific axis.
 */
void
vkdf_camera_step(VkdfCamera *cam, float d, int stepX, int stepY, int stepZ)
{
   glm::vec3 view;
   view = vkdf_camera_get_viewdir(cam);
   if (!stepX)
      view.x = 0.0f;
   if (!stepY)
      view.y = 0.0f;
   if (!stepZ)
      view.z = 0.0f;
   cam->pos.x += view.x * d;
   cam->pos.y += view.y * d;
   cam->pos.z += view.z * d;
   cam->dirty = true;
}

/**
 * Strafe camera (only X-Z plane supported)
 */
void
vkdf_camera_strafe(VkdfCamera *cam, float d)
{
   glm::vec3 view, strafe;
   view = vkdf_camera_get_viewdir(cam);
   strafe.x = view.z;
   strafe.y = 0.0f;
   strafe.z = -view.x;
   cam->pos.x += strafe.x * d;
   cam->pos.y += strafe.y * d;
   cam->pos.z += strafe.z * d;
   cam->dirty = true;
}

/**
 * Sets the camera to look at a specific point in space
 */
void
vkdf_camera_look_at(VkdfCamera *cam, float x, float y, float z)
{
   cam->rot = vkdf_compute_view_rotation(cam->pos, glm::vec3(x, y, z));
   cam->dirty = true;
}

glm::mat4
vkdf_camera_get_rotation_matrix(VkdfCamera *cam)
{
   glm::mat4 mat(1.0);
   float rx = DEG_TO_RAD(cam->rot.x);
   float ry = DEG_TO_RAD(cam->rot.y);
   float rz = DEG_TO_RAD(cam->rot.z);
   mat = glm::rotate(mat, rz, glm::vec3(0, 0, 1));
   mat = glm::rotate(mat, ry, glm::vec3(0, 1, 0));
   mat = glm::rotate(mat, rx, glm::vec3(1, 0, 0));
   return mat;
}
