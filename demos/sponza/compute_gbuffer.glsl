   // light-space pos
   //
   // We skip eye-space position to save bandwidth. Shaders that need
   // this will have to reconstruct it from depth.
   //
   // We also skip light direction since this is constant for all fragments
   out_light_space_pos = in_light_space_pos;

   Material mat = Mat.materials[in_material_idx];

   // Eye-space normal and roughness
   if (mat.normal_tex_count > 0) {
      mat3 TBN = mat3(in_eye_tangent, in_eye_bitangent, in_eye_normal);
      vec3 tangent_normal = texture(tex_normal, in_uv).rgb * 2.0 - 1.0;
      out_eye_normal.xyz = normalize(TBN * tangent_normal);
   } else {
      out_eye_normal.xyz = in_eye_normal;
   }
   out_eye_normal.w = mat.roughness;

   // Diffuse and reflectiveness
   if (mat.diffuse_tex_count > 0)
      mat.diffuse = texture(tex_diffuse, in_uv);
   out_diffuse.xyz = mat.diffuse.xyz;
   out_diffuse.w = mat.reflectiveness;

   // Specular
   if (mat.specular_tex_count > 0)
      mat.specular = texture(tex_specular, in_uv);
   out_specular = mat.specular;

   // Shininess (encoded in UNORM format)
   out_specular.w = mat.shininess / 255.0;
