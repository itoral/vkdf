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

/**
 * Computes the terrain height at an arbitrary (x,z) location in
 * world space.
 */
float
vkdf_terrain_get_height_at(VkdfTerrain *t, float x, float z)
{
   VkdfMesh *mesh = t->obj->model->meshes[0];

   /* Translate world space coordinates to mesh space */
   glm::vec3 vloc = world_to_terrain_vertex_coords(t, glm::vec3(x, 0.0f, z));
   x = vloc.x;
   z = vloc.z;

   /* If the location is outside the terrain area, just return a very low
    * height.
    */
   if (x < 0.0f || z < 0.0f || x > t->num_verts_x - 1 || z > t->num_verts_z - 1)
      return -999999999.0f;

   /* Find offsets of the coords into a terrain quad */
   float offx = x - truncf(x);
   float offz = z - truncf(z);

   /* Compute the plane equation for the triangle we are in */
   glm::vec3 p1, p2, p3;
   float A, B, C, D;
   if (offx >= offz) {
      /* First triangle in the quad */
      p1.x = truncf(x) + 1.0f;
      p1.z = truncf(z);
      p1.y = terrain_vertex_height(t, mesh, p1.x, p1.z);

      p2.x = truncf(x);
      p2.z = truncf(z);
      p2.y = terrain_vertex_height(t, mesh, p2.x, p2.z);

      p3.x = truncf(x) + 1.0f;
      p3.z = truncf(z) + 1.0f;
      p3.y = terrain_vertex_height(t, mesh, p3.x, p3.z);
   } else {
      /* Second triangle in the quad */
      p1.x = truncf(x);
      p1.z = truncf(z);
      p1.y = terrain_vertex_height(t, mesh, p1.x, p1.z);

      p2.x = truncf(x) + 1.0f;
      p2.z = truncf(z) + 1.0f;
      p2.y = terrain_vertex_height(t, mesh, p2.x, p2.z);

      p3.x = truncf(x);
      p3.z = truncf(z) + 1.0f;
      p3.y = terrain_vertex_height(t, mesh, p3.x, p3.z);
   }


   /* FIXME: we probably want to pre-compute plane equations for each
    * triangle in the terrain rather than recomputing them all the time
    */
   A = (p2.y - p1.y) * (p3.z - p1.z) - (p3.y - p1.y) * (p2.z - p1.z);
   B = (p2.z - p1.z) * (p3.x - p1.x) - (p3.z - p1.z) * (p2.x - p1.x);
   C = (p2.x - p1.x) * (p3.y - p1.y) - (p3.x - p1.x) * (p2.y - p1.y);
   D = -(A * p1.x + B * p1.y + C * p1.z);

   /* Use the plane equation to find Y given (X,Z) */
   float y = (-D - C * z - A * x) / B;

   /* Return world-space height */
   return t->obj->pos.y + y * t->obj->scale.y;
}

/**
 * Retrieves the terrain height at vertex coordinates (x,z).
 */
float
vkdf_terrain_height_from_height_map(VkdfTerrain *t,
                                    uint32_t x, uint32_t z,
                                    void *data)
{
   assert(x >= 0 && x <= t->num_verts_x - 1);
   assert(z >= 0 && z <= t->num_verts_z - 1);

   if (t->initialized) {
      assert(t->obj && t->obj->model);
      return terrain_vertex_height(t, t->obj->model->meshes[0], x, z);
   }

   SDL_Surface *surf = (SDL_Surface *) data;
   return terrain_height_from_height_map(t, surf, x, z);
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

   /* Compute UVs */
   if (t->uv_scale_x > 0.0f && t->uv_scale_z > 0.0f) {
      for (uint32_t i = 0; i < mesh->vertices.size(); i++) {
         glm::vec3 v = mesh->vertices[i];
         glm::vec2 uv = glm::vec2(t->uv_scale_x * (0.5f + v.x * 0.5f),
                                  t->uv_scale_z * (0.5f + v.z * 0.5f));
         mesh->uvs.push_back(uv);
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
                 float uv_scale_x, float uv_scale_z,
                 VkdfTerrainHeightFunc hf, void *hf_data)
{
   assert(num_verts_x > 1 && num_verts_z > 1);

   VkdfTerrain *t = g_new0(VkdfTerrain, 1);
   t->max_height = -1.0f;
   t->num_verts_x = num_verts_x;
   t->num_verts_z = num_verts_z;
   t->uv_scale_x = uv_scale_x;
   t->uv_scale_z = uv_scale_z;
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
vkdf_terrain_free(VkdfContext *ctx, VkdfTerrain *t,
                  bool free_obj, bool free_materials)
{
   vkdf_model_free(ctx, t->obj->model, free_materials);
   if (free_obj)
      vkdf_object_free(t->obj);
   g_free(t);
}

bool
vkdf_terrain_check_collision(VkdfTerrain *t,
                             VkdfBox *box,
                             float *collision_height)
{
   /* Compute the X,Z area of the object box */
   float box_min_y = box->center.y - box->h;
   glm::vec3 box_bottom[4] = {
      vkdf_box_get_vertex(box, 2),
      vkdf_box_get_vertex(box, 3),
      vkdf_box_get_vertex(box, 6),
      vkdf_box_get_vertex(box, 7)
   };

   glm::vec3 min(box_bottom[0].x, 0.0f, box_bottom[0].z);
   glm::vec3 max(box_bottom[0].x, 0.0f, box_bottom[0].z);
   for (uint32_t i = 1; i < 4; i++) {
      if (box_bottom[i].x < min.x)
         min.x = box_bottom[i].x;
      else if (box_bottom[i].x > max.x)
         max.x = box_bottom[i].x;

      if (box_bottom[i].z < min.z)
         min.z = box_bottom[i].z;
      else if (box_bottom[i].z > max.z)
         max.z = box_bottom[i].z;
   }

   /* Check if any terrain vertex covered by the X,Z area of the object box
    * is not below the box (in which case some part of the object is below the
    * terrain).
    */
   float x_scale = (2.0f * t->obj->scale.x) / (t->num_verts_x - 1);
   float z_scale = (2.0f * t->obj->scale.z) / (t->num_verts_z - 1);
   bool has_collision = false;
   if (collision_height)
      *collision_height = box_bottom[0].y;
   for (float x = min.x; x <= max.x; x += x_scale)
   for (float z = min.z; z <= max.z; z += z_scale) {
      float h = vkdf_terrain_get_height_at(t, x, z);
      if (h >= box_bottom[0].y) {
         if (collision_height && *collision_height < h)
            *collision_height = h;
         has_collision = true;
      }
   }

   return has_collision;
}

