#include "vkdf.hpp"

/**
 * Generates a SSAO kernel sample with 'num_samples' samples in tangent space
 * covering the unit hemisphere in the positive Z direction.
 */
void
vkdf_ssao_gen_tangent_samples(uint32_t num_samples,
                              std::vector<glm::vec3> *samples)
{
   for (uint32_t i = 0; i < num_samples; i++) {
      glm::vec3 sample;
      do {
         /* Gen a random sample direction in the positive hemisphere */
         sample = glm::vec3(rand_float(-1.0f, 1.0f),
                            rand_float(-1.0f, 1.0f),
                            rand_float( 0.0f, 1.0f));

         vkdf_vec3_normalize(&sample);

         /* Discard this sample if its vector is too close to being parallel to
          * the surface (orthogonal to the normal). This is to prevent artifacts
          * with these samples by incorrectly detecting that the surface
          * incorrectly occludes them due to depth precission limitations.
          */
      } while (fabs(vkdf_vec3_dot(sample, glm::vec3(0.0f, 0.0f, 1.0f))) < 0.05f);

      /* Put the sample somewhere in that direction, in a unit hemisphere */
      sample *= rand_float(0.0f, 1.0f);

      /* Make the distribution have more samples closer to the origin */
      float scale = (float) i / (float) num_samples;
      scale = lerp(0.1f, 1.0f, scale * scale);
      sample *= scale;

      samples->push_back(sample);
   }
}

/**
 * Generates 'num_samples' noise vector samples that can be used to rotate
 * around the Z axis. These are used to rotate the fixed kernel of tangent
 * space samples in view space for every pixel, introducing variability
 * to avoid banding artifacts during the base SSAO pass.
 */
void
vkdf_ssao_gen_noise_samples(uint32_t num_samples,
                            std::vector<glm::vec3> *samples)
{
   for (uint32_t i = 0; i < num_samples; i++) {
      glm::vec3 sample = glm::vec3(rand_float(-1.0f, 1.0f),
                                   rand_float(-1.0f, 1.0f),
                                   0.0f);
      vkdf_vec3_normalize(&sample);
      sample *= rand_float(0.0f, 1.0f);
      samples->push_back(sample);
   }
}
