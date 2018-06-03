#ifndef __VKDF_CAMERA_H__
#define __VKDF_CAMERA_H__

#include "vkdf-deps.hpp"
#include "vkdf-box.hpp"
#include "vkdf-frustum.hpp"
#include "vkdf-util.hpp"
#include "vkdf-object.hpp"
#include "vkdf-mesh.hpp"

typedef void (*VkdfCameraProgramSpecCB)(void *data);

enum {
   VKDF_CAMERA_DIRTY_PROJ        = (1 << 0),
   VKDF_CAMERA_DIRTY_POS         = (1 << 1),
   VKDF_CAMERA_DIRTY_VIEW_DIR    = (1 << 2),
};

enum {
   VKDF_CAMERA_CACHED_VIEW_DIR   = (1 << 0),
   VKDF_CAMERA_CACHED_VIEW_MAT   = (1 << 1),
   VKDF_CAMERA_CACHED_ROT_MAT    = (1 << 2),
   VKDF_CAMERA_CACHED_FRUSTUM    = (1 << 3),
};

typedef struct {
   struct {
      glm::vec3 start;
      glm::vec3 end;
      float speed;
   } pos;

   struct {
      glm::vec3 start;
      glm::vec3 end;
      float speed;
   } rot;

   uint32_t min_steps;
   uint32_t steps;

   VkdfCameraProgramSpecCB start_cb;
   VkdfCameraProgramSpecCB update_cb;
   VkdfCameraProgramSpecCB end_cb;
   void *callback_data;
} VkdfCameraProgramSpec;

typedef struct {
   struct {
      float fov;
      float near_plane;
      float far_plane;
      float aspect_ratio;
      glm::mat4 matrix;
   } proj;

   glm::vec3 pos;
   glm::vec3 rot;

   glm::vec3 viewdir;
   glm::mat4 view_matrix;
   glm::mat4 rot_matrix;

   VkdfFrustum frustum;

   /* We use a VkdfObject for collision testing */
   VkdfObject *collision_obj;

   uint32_t dirty;
   uint32_t cached;

   struct {
      VkdfCameraProgramSpec entries[16];
      uint32_t total;
      uint32_t current;
   } prog;
} VkdfCamera;

VkdfCamera *
vkdf_camera_new(float px, float py, float pz,
                float rx, float ry, float rz,
                float fov, float near, float far, float aspect_ratio);

void
vkdf_camera_free(VkdfCamera *cam);

void
vkdf_camera_set_projection(VkdfCamera *cam,
                           float fov,
                           float near_plane,
                           float far_plane,
                           float aspect_ratio);

inline glm::mat4 *
vkdf_camera_get_projection_ptr(VkdfCamera *cam)
{
   return &cam->proj.matrix;
}

glm::vec3
vkdf_camera_get_position(VkdfCamera *cam);

void
vkdf_camera_set_position(VkdfCamera *cam, float px, float py, float pz);

glm::vec3
vkdf_camera_get_rotation (VkdfCamera *cam);

void
vkdf_camera_set_rotation (VkdfCamera *cam, float rx, float ry, float rz);

void
vkdf_camera_move(VkdfCamera *cam, float dx, float dy, float dz);

void
vkdf_camera_rotate(VkdfCamera *cam, float rx, float ry, float rz);

void
vkdf_camera_step(VkdfCamera *cam, float d, int stepX, int stepY, int stepZ);
void
vkdf_camera_strafe(VkdfCamera *cam, float d);

glm::vec3
vkdf_camera_get_viewdir(VkdfCamera *cam);

void
vkdf_camera_look_at(VkdfCamera *cam, float x, float y, float z);

inline bool
vkdf_camera_is_dirty(VkdfCamera *cam)
{
   return (bool) cam->dirty;
}

inline bool
vkdf_camera_has_dirty_position(VkdfCamera *cam)
{
   return bitfield_get(cam->dirty, VKDF_CAMERA_DIRTY_POS);
}

inline bool
vkdf_camera_has_dirty_viewdir(VkdfCamera *cam)
{
   return bitfield_get(cam->dirty, VKDF_CAMERA_DIRTY_VIEW_DIR);
}

inline void
vkdf_camera_reset_dirty_state(VkdfCamera *cam)
{
   cam->dirty = 0;
}

glm::mat4
vkdf_camera_get_view_matrix(VkdfCamera *cam);

glm::mat4
vkdf_camera_get_rotation_matrix(VkdfCamera *cam);

const glm::vec3 *
vkdf_camera_get_frustum_vertices(VkdfCamera *cam);

const VkdfBox *
vkdf_camera_get_frustum_box(VkdfCamera *cam);

const VkdfPlane *
vkdf_camera_get_frustum_planes(VkdfCamera *cam);

void
vkdf_camera_set_collision_mesh(VkdfCamera *cam, VkdfMesh *mesh, glm::vec3 scale);

VkdfBox *
vkdf_camera_get_collision_box(VkdfCamera *cam);

inline void
vkdf_camera_add_program(VkdfCamera *cam, VkdfCameraProgramSpec *prog)
{
   cam->prog.entries[cam->prog.total++] = *prog;
}

inline bool
vkdf_camera_next_program(VkdfCamera *cam)
{
   assert(cam->prog.total > 0);
   cam->prog.current = (cam->prog.current + 1) % cam->prog.total;
   return cam->prog.current == 0;
}

void
vkdf_camera_program_reset(VkdfCamera *cam, bool pos, bool rot);

float
vkdf_camera_program_update(VkdfCamera *cam);

#endif
