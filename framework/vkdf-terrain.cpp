#include "vkdf-terrain.hpp"
#include "vkdf-util.hpp"

/**
 * Height of the vertex at coordinates (x, z). It just returns the Y component
 * of the corresponding vertex in the mesh.
 */
static inline float
terrain_vertex_height(VkdfTerrain *t, VkdfMesh *mesh, uint32_t x, uint32_t z)
{
   /* Wrap around if needed. This is useful when we need to compute normals,
    * since we need to access vertex neighbours.
    */
   x = x % t->num_verts_x;
   z = z % t->num_verts_z;

   uint32_t vidx = x * t->num_verts_z + z;
   return mesh->vertices[vidx].y;
}

static float
terrain_height_from_height_map(VkdfTerrain *t,
                               SDL_Surface *surf,
                               float x, float z)
{
   /* Translate coordinates from vertex mesh space to surface pixels space */
   uint8_t *pixels = (uint8_t *) surf->pixels;
   float scale_x = ((float) surf->w) / (t->num_verts_x - 1);
   float scale_z = ((float) surf->h) / (t->num_verts_z - 1);
   x *= scale_x;
   z *= scale_z;
   uint32_t img_x = (uint32_t) MIN2(roundf(x), surf->w - 1);
   uint32_t img_y = (uint32_t) MIN2(roundf(z), surf->h - 1);

   /* Normalize height to [-1, 1] */
   float h = pixels[img_y * surf->pitch + img_x * 4];
   h = h / 127.5f - 1.0f;
   return h;
}

/**
 * Computes height at vertex coordinates (x,z). The coordinates can have
 * fractional part, which can be used to calculate height at any position
 * in between actual terrain vertices.
 */
float
vkdf_terrain_height_from_height_map(VkdfTerrain *t,
                                    float x, float z,
                                    void *data)
{
   assert(x >= 0.0f && x <= t->num_verts_x - 1);
   assert(z >= 0.0f && z <= t->num_verts_z - 1);

   /* Fast path if we are querying the height at an exact vertex coordinate,
    * which we can take directly from the mesh vertex.
    */
   if (t->initialized && x == truncf(x) && z == truncf(z)) {
      assert(t->obj && t->obj->model);
      return terrain_vertex_height(t, t->obj->model->meshes[0], x, z);
   }

   /* The location is not an exact vertex or we still haven't computed terrain
    * heights for mesh vertices, fallback to computing height manually.
    */
   SDL_Surface *surf = (SDL_Surface *) data;

   /* Compute offset into the quad of vertices where this coordinate lives:
    *
    * (vx,vz+1)  (vx+1,vz+1)
    *    x--------x
    *    |        |
    *    | x------|----> (x,z)
    *    |        |
    *    x--------x
    * (vx,vz)    (vx+1,vz)
    */
   float x_offset = x - truncf(x);
   float z_offset = z - truncf(z);
   assert(x_offset < 1.0f && z_offset < 1.0f);

   float one_x_offset = 1.0f - x_offset;
   float one_z_offset = 1.0f - z_offset;

   /* For each quad vertex, compute its height and its distance to (x,z) */
   float x0 = truncf(x);
   float z0 = truncf(z);
   float d0 = sqrtf(x_offset * x_offset + z_offset * z_offset);
   float h0 = terrain_height_from_height_map(t, surf, x0, z0);

   float x1 = x0 + 1.0f;
   float d1 = sqrtf(one_x_offset * one_x_offset + z_offset * z_offset);
   float h1 = terrain_height_from_height_map(t, surf, x1, z0);

   float z1 = z0 + 1.0f;
   float d2 = sqrtf(x_offset * x_offset + one_z_offset * one_z_offset);
   float h2 = terrain_height_from_height_map(t, surf, x0, z1);

   float d3 = sqrtf(one_x_offset * one_x_offset + one_z_offset * one_z_offset);
   float h3 = terrain_height_from_height_map(t, surf, x1, z1);

   /* Compute height at (x,z) by interpolating the heights from the quad */
   float sum_dist = d0 + d1 + d2 + d3;
   return (h0 * (sum_dist - d0) + h1 * (sum_dist - d1) +
           h2 * (sum_dist - d2) + h3 * (sum_dist - d3)) /
          (3.0f * sum_dist);
}

static glm::vec3
calculate_vertex_normal(VkdfTerrain *t, VkdfMesh *mesh, uint32_t x, uint32_t z)
{
   assert(x >= 0 && x < t->num_verts_x);
   assert(z >= 0 && z < t->num_verts_z);

   float hl = terrain_vertex_height(t, mesh, x - 1, z);
   float hr = terrain_vertex_height(t, mesh, x + 1, z);
   float hb = terrain_vertex_height(t, mesh, x, z - 1);
   float hf = terrain_vertex_height(t, mesh, x, z + 1);

   glm::vec3 n = glm::vec3(hl - hr, 2.0f, hb - hf);
   vkdf_vec3_normalize(&n);
   return n;
}

static void
terrain_gen_mesh(VkdfContext *ctx, VkdfTerrain *t)
{
   VkdfMesh *mesh = vkdf_mesh_new(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
   mesh->material_idx = 0;

   /* Generate vertices covering the range [-1, 1] in both X and Z */
   float delta_x = 2.0f / (t->num_verts_x - 1);
   float delta_z = 2.0f / (t->num_verts_z - 1);
   for (uint32_t x = 0; x < t->num_verts_x; x++) {
      for (uint32_t z = 0; z < t->num_verts_z; z++) {
         float h = t->hf(t, x, z, t->hf_data);
         glm::vec3 v = glm::vec3(-1.0f + x * delta_x,
                                 h,
                                 -1.0f + z * delta_z);

         if (h > t->max_height)
            t->max_height = h;

         mesh->vertices.push_back(v);
      }
   }

   /* Generate indices for rendering with a single triangle strip using
    * degenerate triangles to join individual strips.
    */
   for (uint32_t x = 0; x < t->num_verts_x - 1; x++) {
      for (uint32_t z = 0; z < t->num_verts_z; z++) {
         uint32_t v0_idx = x * t->num_verts_z + z;
         uint32_t v1_idx = v0_idx + t->num_verts_z;

         /* If starting new strip after the first, link to the previous strip
          * with a degenerate by replicating the first index in this strip
          * before we start.
          */
         if (x > 0 && z == 0)
            mesh->indices.push_back(v1_idx);

         mesh->indices.push_back(v1_idx);
         mesh->indices.push_back(v0_idx);

         /* If ending a strip before the last, link to the next strip
          * with a degenerate by replicating the last index in this strip
          * before we end.
          */
         if (x < t->num_verts_x - 2 && z == t->num_verts_z - 1)
            mesh->indices.push_back(v0_idx);
      }
   }

   /* Compute normals */
   for (uint32_t x = 0; x < t->num_verts_x; x++) {
      for (uint32_t z = 0; z < t->num_verts_z; z++) {
         glm::vec3 n =
            calculate_vertex_normal(t, mesh, x, z);
         mesh->normals.push_back(n);
      }
   }

   vkdf_mesh_compute_box(mesh);

   t->obj = vkdf_object_new_from_mesh(glm::vec3(0.0f), mesh);
   vkdf_model_fill_vertex_buffers(ctx, t->obj->model, true);
   vkdf_model_compute_box(t->obj->model);
}

VkdfTerrain *
vkdf_terrain_new(VkdfContext *ctx,
                 uint32_t num_verts_x, uint32_t num_verts_z,
                 VkdfTerrainHeightFunc hf, void *hf_data)
{
   assert(num_verts_x > 1 && num_verts_z > 1);

   VkdfTerrain *t = g_new0(VkdfTerrain, 1);
   t->max_height = -1.0f;
   t->num_verts_x = num_verts_x;
   t->num_verts_z = num_verts_z;
   t->hf = hf;
   t->hf_data = hf_data;

   terrain_gen_mesh(ctx, t);

   t->initialized = true;

   return t;
}

/* If we have put the terrain as an object in a scene, the scene will take
 * ownership of the object, in which case callers should set 'free_obj' to
 * False.
 */
void
vkdf_terrain_free(VkdfContext *ctx, VkdfTerrain *t, bool free_obj)
{
   vkdf_model_free(ctx, t->obj->model);
   if (free_obj)
      vkdf_object_free(t->obj);
   g_free(t);
}

static inline glm::vec3
world_to_terrain_vertex_coords(VkdfTerrain *t, glm::vec3 p)
{
   /* Normalize to [0,1] */
   p -= t->obj->pos;
   glm::vec3 p_norm = 0.5f + (p / t->obj->scale) * 0.5f;

   /* Trnaslate to [0, num_verts - 1] */
   return glm::vec3(p_norm.x * (t->num_verts_x - 1),
                    2.0f * p_norm.y - 1.0f,  // [-1, 1]
                    p_norm.z * (t->num_verts_z - 1));
}

bool
vkdf_terrain_check_collision(VkdfTerrain *t,
                             VkdfBox *box,
                             float *collision_height)
{
   VkdfBox *t_box = vkdf_object_get_box(t->obj);
   if (!vkdf_box_collision(box, t_box))
      return false;

   float box_min_y = box->center.y - box->h;
   glm::vec3 box_bottom[4] = {
      vkdf_box_get_vertex(box, 2),
      vkdf_box_get_vertex(box, 3),
      vkdf_box_get_vertex(box, 6),
      vkdf_box_get_vertex(box, 7)
   };

   for (uint32_t i = 0; i < 4; i++) {
      glm::vec3 loc = world_to_terrain_vertex_coords(t, box_bottom[i]);
      /* If the vertex is outside the terrain area, then there is no collision */
      if (loc.x <  0.0  || loc.x > t->num_verts_x - 1 ||
          loc.y < -1.0f || loc.y > t->max_height      ||
          loc.z <  0.0f || loc.z > t->num_verts_z - 1) {
         continue;
      }

      /* Otherwise, check the Y component of this box vertex against the
       * terrain height at the same location.
       */
      float h_norm = t->hf(t, loc.x, loc.z, t->hf_data);
      float h = t->obj->pos.y + t->obj->scale.y * h_norm;
      if (h >= box_bottom[i].y) {
         if (collision_height)
            *collision_height = h;
         return true;
      }
   }

   return false;
}

