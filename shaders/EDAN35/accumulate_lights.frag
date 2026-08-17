#version 410

struct ViewProjTransforms
{
	mat4 view_projection;
	mat4 view_projection_inverse;
};

layout (std140) uniform CameraViewProjTransforms
{
	ViewProjTransforms camera;
};

layout (std140) uniform LightViewProjTransforms
{
	ViewProjTransforms lights[4];
};

uniform int light_index;

uniform sampler2D depth_texture;
uniform sampler2D normal_texture;
uniform sampler2D shadow_texture;

uniform vec2 inverse_screen_resolution;

uniform vec3 camera_position;

uniform vec3 light_color;
uniform vec3 light_position;
uniform vec3 light_direction;
uniform float light_intensity;
uniform float light_angle_falloff;

layout (location = 0) out vec4 light_diffuse_contribution;
layout (location = 1) out vec4 light_specular_contribution;


void main()
{
	light_diffuse_contribution  = vec4(0.0, 0.0, 0.0, 1.0);
	light_specular_contribution = vec4(0.0, 0.0, 0.0, 1.0);

	// Recover the G-buffer sample belonging to this framebuffer fragment.
	vec2 screen_texcoord = gl_FragCoord.xy * inverse_screen_resolution;
	float depth = texture(depth_texture, screen_texcoord).r;
	if (depth >= 1.0)
		return;

	vec3 normal = normalize(texture(normal_texture, screen_texcoord).xyz * 2.0 - 1.0);

	// OpenGL depth is stored in [0, 1], whereas clip-space coordinates are
	// in [-1, 1].  Undo the camera projection to obtain a world-space point.
	vec4 clip_position = vec4(screen_texcoord * 2.0 - 1.0,
	                          depth * 2.0 - 1.0,
	                          1.0);
	vec4 world_position_h = camera.view_projection_inverse * clip_position;
	if (abs(world_position_h.w) < 0.000001)
		return;
	vec3 world_position = world_position_h.xyz / world_position_h.w;

	vec3 point_to_light = light_position - world_position;
	float squared_distance = max(dot(point_to_light, point_to_light), 0.0001);
	vec3 to_light = point_to_light * inversesqrt(squared_distance);
	float diffuse_factor = max(dot(normal, to_light), 0.0);
	if (diffuse_factor <= 0.0)
		return;

	// Smooth spotlight falloff: use the supplied 37 degree angle as the outer
	// cutoff (therefore reaching zero before the required 45 degrees).
	vec3 light_to_point = -to_light;
	float cos_light_angle = dot(normalize(light_direction), light_to_point);
	float inner_cosine = cos(0.8 * light_angle_falloff);
	float outer_cosine = cos(light_angle_falloff);
	float angular_falloff = smoothstep(outer_cosine, inner_cosine, cos_light_angle);
	if (angular_falloff <= 0.0)
		return;

	// Transform the shaded point into the current light's clip space and use
	// the resulting depth to test visibility in the shadow map.
	vec4 shadow_clip_position = lights[light_index].view_projection
	                           * vec4(world_position, 1.0);
	if (shadow_clip_position.w <= 0.0)
		return;

	vec3 shadow_ndc = shadow_clip_position.xyz / shadow_clip_position.w;
	if (any(lessThan(shadow_ndc, vec3(-1.0)))
	 || any(greaterThan(shadow_ndc, vec3(1.0))))
		return;

	vec2 shadow_texcoord = shadow_ndc.xy * 0.5 + 0.5;
	float fragment_light_depth = shadow_ndc.z * 0.5 + 0.5;
	const float depth_bias = 0.00001;
	vec2 shadowmap_texel_size = vec2(1.0) / vec2(textureSize(shadow_texture, 0));

	// Percentage-closer filtering over a regular 3x3 neighbourhood.  Samples
	// outside the map represent space outside the shadow camera and are lit.
	float visibility = 0.0;
	for (int y = -1; y <= 1; ++y) {
		for (int x = -1; x <= 1; ++x) {
			vec2 sample_texcoord = shadow_texcoord
			                     + vec2(float(x), float(y)) * shadowmap_texel_size;
			if (any(lessThan(sample_texcoord, vec2(0.0)))
			 || any(greaterThan(sample_texcoord, vec2(1.0)))) {
				visibility += 1.0;
			} else {
				float shadow_depth = texture(shadow_texture, sample_texcoord).r;
				visibility += fragment_light_depth - depth_bias <= shadow_depth ? 1.0 : 0.0;
			}
		}
	}
	visibility /= 9.0;

	float distance_falloff = light_intensity / squared_distance;
	vec3 radiance = light_color * distance_falloff * angular_falloff * visibility;

	vec3 to_camera = normalize(camera_position - world_position);
	vec3 reflected_light = reflect(-to_light, normal);
	float specular_factor = pow(max(dot(reflected_light, to_camera), 0.0), 32.0);

	// Material diffuse/specular colours and the ambient term are applied in
	// resolve_deferred.frag; these buffers store only each light's contribution.
	light_diffuse_contribution  = vec4(radiance * diffuse_factor, 1.0);
	light_specular_contribution = vec4(radiance * specular_factor, 1.0);
}
