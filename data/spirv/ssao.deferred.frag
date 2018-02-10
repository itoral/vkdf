#version 450

#extension GL_ARB_separate_shader_objects : enable

layout(push_constant) uniform pcb {
   mat4 Proj;
   vec2 noise_scale;
   int num_samples;
   float radius;
   float bias;
   float intensity;
   float aspect_ratio;
   float tan_half_fov;
} PCB;

const int MAX_SAMPLES = 64;
layout(std140, set = 0, binding = 0) uniform SamplesUBO {
   vec3 samples[MAX_SAMPLES];
} S;

layout(set = 1, binding = 0) uniform sampler2D tex_depth;
layout(set = 1, binding = 1) uniform sampler2D tex_normal;
layout(set = 1, binding = 2) uniform sampler2D tex_noise;

layout(location = 0) in vec2 in_uv;
layout(location = 1) in vec2 in_view_ray;

layout(location = 0) out float out_ssao;

/**
 * Computes eye-space Z for the pixel's depth taken from the depth buffer
 * at the given texture-space coordinates.
 *
 * Notice that the formula here is Vulkan-specific due to the differences
 * between OpenGL and Vulkan coordinate systems.
 */
float
compute_eye_z_from_depth(vec2 coord)
{
   float depth = texture(tex_depth, coord).x;
   return -PCB.Proj[3][2] / (PCB.Proj[2][2] + depth);
}

void main()
{
   /* Retrieve eye-space position of the current fragment reconstructing
    * it from the depth buffer.
    */
   float position_z = compute_eye_z_from_depth(in_uv);
   float position_x = in_view_ray.x * position_z;
   float position_y = in_view_ray.y * position_z;
   vec3 position = vec3(position_x, position_y, position_z);

   /* Retrieve eye-space normal of the current fragment from the GBuffer */
   vec3 normal = texture(tex_normal, in_uv).xyz;

   /* Compute a rotated TBN transform based on the noise vector */
   vec3 noise = texture(tex_noise, in_uv * PCB.noise_scale).xyz;
   vec3 tangent = normalize(noise - normal * dot(noise, normal));
   vec3 bitangent = cross(normal, tangent);
   mat3 TBN = mat3(tangent, bitangent, normal);

   /* Accumulate occlusion for each kernel sample */
   float occlusion = 0.0;
   for(int i = 0; i < PCB.num_samples; ++i) {
      /* Compute sample position in view space */
      vec3 sample_i = TBN * S.samples[i];
      sample_i = position + sample_i * PCB.radius;

      /* Compute sample XY coordinates in NDC space */
      vec4 sample_i_clip = PCB.Proj * vec4(sample_i, 1.0);
      vec2 sample_i_ndc = sample_i_clip.xy / sample_i_clip.w;

      /* Convert sample XY to texture coordinate space and sample from the
       * Position texture to obtain the scene depth at that XY coordinate
       */
      vec2 sample_i_uv = sample_i_ndc * 0.5 + 0.5;
      float ref_depth = compute_eye_z_from_depth(sample_i_uv.xy);

      /* If the depth for that XY position in the scene is larger than
       * the sample's, then the sample is occluded by scene's geometry and
       * contributes to the occlussion factor.
       */
      if (ref_depth >= sample_i.z + PCB.bias)
         occlusion +=
            smoothstep(0.0, 1.0, PCB.radius / abs(position.z - ref_depth));
   }

   /* We output ambient intensity in the range [0,1] */
   out_ssao = pow(1.0 - (occlusion / PCB.num_samples), PCB.intensity);
}
