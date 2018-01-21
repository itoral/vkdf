#include "vkdf.hpp"

glm::vec3
vkdf_box_get_vertex(const VkdfBox *box, uint32_t index)
{
   switch (index) {
   case 0:
      return box->center + glm::vec3(box->w, box->h, box->d);
   case 1:
      return box->center + glm::vec3(-box->w, box->h, box->d);
   case 2:
      return box->center + glm::vec3(-box->w, -box->h, box->d);
   case 3:
      return box->center + glm::vec3(box->w, -box->h, box->d);
   case 4:
      return box->center + glm::vec3(box->w, box->h, -box->d);
   case 5:
      return box->center + glm::vec3(-box->w, box->h, -box->d);
   case 6:
      return box->center + glm::vec3(-box->w, -box->h, -box->d);
   case 7:
      return box->center + glm::vec3(box->w, -box->h, -box->d);
   default:
      assert(!"invalid box vertex index");
      return glm::vec3(0.0f, 0.0f, 0.0f);
   }
}

bool
vkdf_box_is_inside(const VkdfBox *box, glm::vec3 &p)
{
   float xmin = box->center.x - box->w;
   float xmax = box->center.x + box->w;
   float ymin = box->center.y - box->h;
   float ymax = box->center.y + box->h;
   float zmin = box->center.z - box->d;
   float zmax = box->center.z + box->d;

   return p.x >= xmin && p.x <= xmax &&
          p.y >= ymin && p.y <= ymax &&
          p.z >= zmin && p.z <= zmax;
}

bool
vkdf_box_collision(const VkdfBox *box1, const VkdfBox *box2)
{
   float xmin1 = box1->center.x - box1->w;
   float xmax1 = box1->center.x + box1->w;
   float xmin2 = box2->center.x - box2->w;
   float xmax2 = box2->center.x + box2->w;

   if (xmax1 < xmin2 || xmax2 < xmin1)
      return false;

   float ymin1 = box1->center.y - box1->h;
   float ymax1 = box1->center.y + box1->h;
   float ymin2 = box2->center.y - box2->h;
   float ymax2 = box2->center.y + box2->h;

   if (ymax1 < ymin2 || ymax2 < ymin1)
      return false;

   float zmin1 = box1->center.z - box1->d;
   float zmax1 = box1->center.z + box1->d;
   float zmin2 = box2->center.z - box2->d;
   float zmax2 = box2->center.z + box2->d;

   if (zmax1 < zmin2 || zmax2 < zmin1)
      return false;

   return true;
}

void
vkdf_box_transform(VkdfBox *box, glm::mat4 *transform)
{
   glm::vec3 vertices[8];

   /* Transform the box vertices */
   int n = 0;
   for (float w = -box->w; w <= box->w; w += 2.0f * box->w) {
      for (float h = -box->h; h <= box->h; h += 2.0f * box->h) {
         for (float d = -box->d; d <= box->d; d += 2.0f * box->d) {
            glm::vec4 v = vec4(box->center + glm::vec3(w, h, d), 1.0f);
            v = (*transform) * v;
            vertices[n++] = vec3(v);
         }
      }
   }

   /* Compute transformed box position and dimensions */
   float minX = vertices[0].x;
   float maxX = vertices[0].x;
   float minY = vertices[0].y;
   float maxY = vertices[0].y;
   float minZ = vertices[0].z;
   float maxZ = vertices[0].z;
   for (int i = 1; i < 8; i++) {
      if (vertices[i].x > maxX)
         maxX = vertices[i].x;
      else if (vertices[i].x < minX)
         minX = vertices[i].x;

      if (vertices[i].y > maxY)
         maxY = vertices[i].y;
      else if (vertices[i].y < minY)
         minY = vertices[i].y;

      if (vertices[i].z > maxZ)
         maxZ = vertices[i].z;
      else if (vertices[i].z < minZ)
         minZ = vertices[i].z;
   }

   box->center = glm::vec3((maxX + minX) / 2.0f,
                           (maxY + minY) / 2.0f,
                           (maxZ + minZ) / 2.0f);
   box->w = (maxX - minX) / 2.0f;
   box->h = (maxY - minY) / 2.0f;
   box->d = (maxZ - minZ) / 2.0f;
}

static uint32_t
box_is_in_frustum(const VkdfBox *box, const VkdfPlane *fplanes)
{
   uint32_t result = INSIDE;
   uint32_t in, out;

   for (uint32_t pl = 0; pl < 6; pl++) {
      out = in = 0;
      for (uint32_t bvi = 0; bvi < 8 && (in == 0 || out == 0); bvi++) {
         glm::vec3 p = vkdf_box_get_vertex(box, bvi);
         float dist = vkdf_plane_distance_from_point(&fplanes[pl], p);
         if (dist < 0)
            out++;
         else
            in++;
      }

      if (in == 0)
         return OUTSIDE;
      else if (out > 0)
         result = INTERSECT;
   }

   return result;
}

uint32_t
vkdf_box_is_in_frustum(const VkdfBox *box,
                       const VkdfBox *frustum_box,
                       const VkdfPlane *frustum_planes)
{
   if (frustum_box && !vkdf_box_collision(box, frustum_box))
      return OUTSIDE;

   if (frustum_planes)
      return box_is_in_frustum(box, frustum_planes);

   return INSIDE;
}

uint32_t
vkdf_box_is_in_cone(const VkdfBox *box,
                    glm::vec3 top, glm::vec3 dir, float cutoff)
{
   // Consider some errors margin to account for accumulated precission errors
   // in the computations and specially, CPU/GPU precission differences in
   // trigonometric functions, etc. This error margin is not perfect though,
   // the cosine is not a linear function its value varies more rapidly
   // for some angle ranges than others, so ideally, we would also want to
   // modulate this error margin in similar fasion.
   const float error_margin = 0.05f;

   vkdf_vec3_normalize(&dir);
   cutoff = fabs(cutoff);

   glm::vec3 xmin = box->center - box->w;
   glm::vec3 vmin = xmin - top;
   vkdf_vec3_normalize(&vmin);
   float cos_min = vkdf_vec3_dot(vmin, dir);

   glm::vec3 xmax = box->center + box->w;
   glm::vec3 vmax = xmax - top;
   vkdf_vec3_normalize(&vmax);
   float cos_max = vkdf_vec3_dot(vmax, dir);

   if (((vmin.x < 0) == (vmax.x < 0)) &&
       (fabs(cos_min) + error_margin < cutoff &&
        fabs(cos_max) + error_margin < cutoff))
      return OUTSIDE;

   glm::vec3 ymin = box->center - box->h;
   vmin = ymin - top;
   vkdf_vec3_normalize(&vmin);
   cos_min = vkdf_vec3_dot(vmin, dir);

   glm::vec3 ymax = box->center + box->h;
   vmax = ymax - top;
   vkdf_vec3_normalize(&vmax);
   cos_max = vkdf_vec3_dot(vmax, dir);

   if (((vmin.y < 0) == (vmax.y < 0)) &&
       (fabs(cos_min) + error_margin < cutoff &&
        fabs(cos_max) + error_margin < cutoff))
      return OUTSIDE;

   glm::vec3 zmin = box->center - box->d;
   vmin = zmin - top;
   vkdf_vec3_normalize(&vmin);
   cos_min = vkdf_vec3_dot(vmin, dir);

   glm::vec3 zmax = box->center + box->d;
   vmax = zmax - top;
   vkdf_vec3_normalize(&vmax);
   cos_max = vkdf_vec3_dot(vmax, dir);

   if (((vmin.z < 0) == (vmax.z < 0)) &&
       (fabs(cos_min) + error_margin < cutoff &&
        fabs(cos_max) + error_margin < cutoff))
      return OUTSIDE;

   return INSIDE;
}
