   // This pixel was never rendered in the gbuffer pass so use the clear
   // color
   vec4 eye_normal = texture(tex_eye_normal, in_uv);
   if (eye_normal == vec4(0.0)) {
      out_color = vec4(0.2, 0.4, 0.8, 1.0);
   } else {
      Light light = L.sun;
      light.pos = LESD.eye_pos;

      // Reconstruct eye-space position from depth buffer
      float eye_position_z = compute_eye_z_from_depth(tex_depth, in_uv, PCB.Proj);
      float eye_position_x = in_view_ray.x * eye_position_z;
      float eye_position_y = in_view_ray.y * eye_position_z;
      vec4 eye_position = vec4(eye_position_x,
                               eye_position_y,
                               eye_position_z,
                               1.0);
      vec4 eye_view_dir = -eye_position;

      vec4 light_space_position = texture(tex_light_space_position, in_uv);

      Material mat;
      mat.diffuse = texture(tex_diffuse, in_uv);
      mat.ambient = mat.diffuse * ambient_occlusion;
      mat.specular = texture(tex_specular, in_uv);
      mat.shininess = mat.specular.w * 255.0; /* decode from UNORM */

      // Compute lighting
      LightColor color =
         compute_lighting_directional(light,
                                      eye_position.xyz,
                                      eye_normal.xyz,
                                      eye_view_dir.xyz,
                                      mat,
                                      light.casts_shadows,
                                      light_space_position,
                                      shadow_map,
                                      SMD.shadow_map_data.shadow_map_size,
                                      SHADOW_MAP_PCF_KERNEL_SIZE);

      out_color = vec4(color.diffuse + color.ambient + color.specular,
                       mat.diffuse.a);
   }
