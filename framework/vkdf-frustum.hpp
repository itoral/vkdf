#ifndef __VKDF_FRUSTUM_H__
#define __VKDF_FRUSTUM_H__

/* Frustum vertices: Far|Near, Top|Bottom, Left|Right */
enum {
  FRUSTUM_FTR = 0,
  FRUSTUM_FTL,
  FRUSTUM_FBR,
  FRUSTUM_FBL,
  FRUSTUM_NTR,
  FRUSTUM_NTL,
  FRUSTUM_NBR,
  FRUSTUM_NBL,
};

/* Frustum planes */
enum {
   FRUSTUM_FAR = 0,
   FRUSTUM_NEAR,
   FRUSTUM_LEFT,
   FRUSTUM_RIGHT,
   FRUSTUM_TOP,
   FRUSTUM_BOTTOM,
};

typedef struct {
   glm::vec3 vertices[8];

   bool has_planes;
   VkdfPlane planes[6];

   bool has_box;
   VkdfBox box;
} VkdfFrustum;

void
vkdf_frustum_compute(VkdfFrustum *f,
                     bool compute_planes,
                     bool compute_box,
                     const glm::vec3 &origin,
                     const glm::vec3 &rot,
                     float near_dist,
                     float far_dist,
                     float fov,
                     float aspect_ratio);

void
vkdf_frustum_compute_planes(VkdfFrustum *f);

void
vkdf_frustum_compute_box(VkdfFrustum *f);

inline const glm::vec3 *
vkdf_frustum_get_vertices(const VkdfFrustum *f)
{
   return f->vertices;
}

inline const VkdfPlane *
vkdf_frustum_get_planes(const VkdfFrustum *f)
{
   assert(f->has_planes);
   return f->planes;
}

inline const VkdfBox *
vkdf_frustum_get_box(const VkdfFrustum *f)
{
   assert(f->has_box);
   return &f->box;
}

#endif
