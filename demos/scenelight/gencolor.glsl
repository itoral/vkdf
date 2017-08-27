   // FIXME: instead of using two sampler bindings we should use an array of
   // samplers to simplify this. Unfortunately, Mesa fails to compile such
   // shader at present.
   out_color = vec4(0.0, 0.0, 0.0, 1.0);
   for (int i = 0; i < NUM_LIGHTS; i++) {
      Light light = L.lights[i];

      LightColor color;
      if (light.casts_shadows) {
         vec4 light_space_pos = in_light_space_pos[i];
         uint shadow_map_size = SMD.data[i].shadow_map_size;
         uint pfc_size = SMD.data[i].pfc_kernel_size;
         if (i == 0) {
            color = compute_lighting(light,
                                     in_world_pos.xyz,
                                     in_normal, in_view_dir,
                                     mat,
                                     bool(in_receives_shadows),
                                     light_space_pos, shadow_map0,
                                     shadow_map_size, pfc_size);
         } else {
            color = compute_lighting(light,
                                     in_world_pos.xyz,
                                     in_normal, in_view_dir,
                                     mat,
                                     bool(in_receives_shadows),
                                     light_space_pos, shadow_map1,
                                     shadow_map_size, pfc_size);
         }
      } else {
            color = compute_lighting(L.lights[i],
                                     in_world_pos.xyz,
                                     in_normal, in_view_dir,
                                     mat);
      }
      out_color.xyz += color.diffuse + color.ambient + color.specular;
   }
