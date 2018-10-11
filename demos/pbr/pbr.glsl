vec2
translate_cube_uv_to_spherical_map_uv(vec3 v)
{
   const vec2 inv_atan = vec2(0.1591, 0.3183);

   vec2 uv = vec2(atan(v.z, v.x), asin(v.y));
   return uv * inv_atan + vec2(0.5);
}

vec3
compute_kD_from_kS(vec3 kS, float metallic)
{
   return (1.0 - kS) * (1.0 - metallic);
}

vec3
fresnelSchlick(float cosTheta, vec3 F0)
{
   return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

vec3
fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
   return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - cosTheta, 5.0);
}

float
DistributionGGX(vec3 N, vec3 H, float roughness)
{
   const float PI = 3.14159265359;

   float a      = roughness * roughness;
   float a2     = a * a;
   float NdotH  = max(dot(N, H), 0.0);
   float NdotH2 = NdotH * NdotH;

   float num    = a2;
   float denom  = NdotH2 * (a2 - 1.0) + 1.0;
   denom        = PI * denom * denom;

   return num / denom;
}

float
GeometrySchlickGGX(float NdotV, float roughness)
{
   float r = roughness + 1.0;
   float k = r * r / 8.0;

   float num   = NdotV;
   float denom = NdotV * (1.0 - k) + k;

   return num / denom;
}

float
GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
   float NdotV = max(dot(N, V), 0.0);
   float NdotL = max(dot(N, L), 0.0);
   float ggx2  = GeometrySchlickGGX(NdotV, roughness);
   float ggx1  = GeometrySchlickGGX(NdotL, roughness);

   return ggx1 * ggx2;
}

vec3
compute_lighting_directional_pbr(Light light,
                                 vec3 pos,
                                 vec3 N, /* Normalize normal */
                                 vec3 V, /* Normalize view dir */
                                 vec3 albedo,
                                 float metallic,
                                 float roughness)
{
   const float PI = 3.14159265359;

   vec3 F0 = vec3(0.04);
   F0 = mix(F0, albedo, metallic);

   vec3 Lo = vec3(0.0);
   vec3 L = normalize(light.pos.xyz - pos);
   vec3 H = normalize(V + L);

   // Light radiance at position
   float distance    = length(light.pos.xyz - pos);
   float attenuation = 1.0 / (distance * distance);
   vec3 radiance     = light.diffuse.rgb * attenuation;

   // Material: specular component
   float NDF = DistributionGGX(N, H, roughness);
   float G   = GeometrySmith(N, V, L, roughness);
   vec3 F    = fresnelSchlick(max(dot(H, V), 0.0), F0);

   vec3 kS = F;
   vec3 numerator    = NDF * G * F;
   float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0);
   vec3 specular     = numerator / max(denominator, 0.001);

   // Material: refraction component
   vec3 kD = compute_kD_from_kS(kS, metallic);
   vec3 diffuse = kD * albedo / PI;

   // Incidence angle
   float NdotL = max(dot(N, L), 0.0);

   // Output radiance
   Lo = (diffuse + specular) * radiance * NdotL;
   return Lo;
}

vec3
compute_ambient_ibl(vec3 N, vec3 V,
                    vec3 albedo, float metallic, float roughness,
                    sampler2D irradiance_map)
{
   vec3 F0 = vec3(0.04);
   F0 = mix(F0, albedo, metallic);
   vec3 kS = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
   vec3 kD = compute_kD_from_kS(kS, metallic);

   vec2 irradiance_uv = translate_cube_uv_to_spherical_map_uv(N);
   vec3 irradiance = texture(irradiance_map, irradiance_uv).rgb;
   vec3 diffuse = irradiance * albedo;
   return kD * diffuse;
}
