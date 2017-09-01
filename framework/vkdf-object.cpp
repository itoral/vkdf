#include "vkdf.hpp"

inline static void
init_object(VkdfObject *obj, const glm::vec3 &pos)
{
   obj->pos = pos;
   obj->rot = glm::vec3(0.0f, 0.0f, 0.0f);
   obj->scale = glm::vec3(1.0f, 1.0f, 1.0f);

   obj->dirty = true;
   obj->dirty_model_matrix = true;
   obj->dirty_box = true;
}

VkdfObject *
vkdf_object_new_from_mesh(const glm::vec3 &pos, VkdfMesh *mesh)
{
   VkdfObject *obj = g_new0(VkdfObject, 1);

   init_object(obj, pos);

   obj->model = vkdf_model_new();
   vkdf_model_add_mesh(obj->model, mesh);
   vkdf_model_compute_size(obj->model);

   return obj;
}

VkdfObject *
vkdf_object_new_from_model(const glm::vec3 &pos,
                           VkdfModel *model)
{
   VkdfObject *obj = g_new0(VkdfObject, 1);

   init_object(obj, pos);

   obj->model = model;

   return obj;
}

VkdfObject *
vkdf_object_new(const glm::vec3 &pos, VkdfModel *model)
{
   VkdfObject *obj = g_new0(VkdfObject, 1);

   init_object(obj, pos);

   obj->model = model;

   return obj;
}

void
vkdf_object_free(VkdfObject *obj)
{
   // Models are not owned by the objects
   g_free(obj);
}

glm::mat4
vkdf_object_get_model_matrix(VkdfObject *obj)
{
   if (!obj->dirty_model_matrix)
      return obj->model_matrix;

   obj->model_matrix = glm::translate(glm::mat4(1.0f), obj->pos);

   if (obj->rot.x != 0.0f || obj->rot.y != 0.0f || obj->rot.z != 0.0f) {
      glm::vec3 rot = glm::vec3(DEG_TO_RAD(obj->rot.x),
                                DEG_TO_RAD(obj->rot.y),
                                DEG_TO_RAD(obj->rot.z));
      glm::tquat<float> quat = glm::quat(rot);
      glm::mat4 rot_matrix = glm::toMat4(quat);
      obj->model_matrix = obj->model_matrix * rot_matrix;
   }

   if (obj->scale.x != 1.0f || obj->scale.y != 1.0f || obj->scale.z != 1.0f)
      obj->model_matrix = glm::scale(obj->model_matrix, obj->scale);

   vkdf_object_set_dirty_model_matrix(obj, false);
   return obj->model_matrix;
}

glm::mat4
vkdf_object_get_model_matrix_for_box(VkdfObject *obj)
{
   /* The bounding box's position coordinate is already in world space and
    * it is already scaled so we only need to apply rotation here (around the
    * box's center)
    */
   glm::mat4 Model(1.0f);
   Model = glm::translate(Model, obj->pos);
   // FIXME: use quaternion
   if (obj->rot.x)
      Model = glm::rotate(Model, DEG_TO_RAD(obj->rot.x), glm::vec3(1, 0, 0));
   if (obj->rot.y)
      Model = glm::rotate(Model, DEG_TO_RAD(obj->rot.y), glm::vec3(0, 1, 0));
   if (obj->rot.z)
      Model = glm::rotate(Model, DEG_TO_RAD(obj->rot.z), glm::vec3(0, 0, 1));
   Model = glm::translate(Model, -obj->pos);

   return Model;
}

static void
compute_box(VkdfObject *obj)
{
   assert(obj->model);

   obj->box.w = vkdf_object_width(obj) / 2.0f;
   obj->box.d = vkdf_object_depth(obj) / 2.0f;
   obj->box.h = vkdf_object_height(obj) / 2.0f;
   obj->box.center = obj->pos;

   if (obj->rot.x != 0.0f || obj->rot.y != 0.0f || obj->rot.z != 0.0f) {
      glm::mat4 model = vkdf_object_get_model_matrix_for_box(obj);
      vkdf_box_transform(&obj->box, &model);
   }

   vkdf_object_set_dirty_box(obj, false);
}

VkdfBox *
vkdf_object_get_box(VkdfObject *obj)
{
   if (obj->dirty_box)
      compute_box(obj);
   return &obj->box;
}
