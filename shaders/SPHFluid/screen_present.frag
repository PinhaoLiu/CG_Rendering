#version 430

in vec2 uv;

layout (location = 0) out vec4 frag_color;

layout (binding = 0) uniform sampler2D uSceneColour;
layout (binding = 1) uniform sampler2D uRawDepth;
layout (binding = 2) uniform sampler2D uRawThickness;
layout (binding = 3) uniform sampler2D uSmoothDepth;
layout (binding = 4) uniform sampler2D uSmoothThickness;
layout (binding = 5) uniform sampler2D uNormal;
layout (binding = 6) uniform sampler2D uSceneDepth;
uniform mat4 uClipToView;
uniform mat4 uViewToClip;
uniform mat4 uViewToWorld;
uniform int uDisplayMode;
uniform float uDebugDepthScale;
uniform float uDebugThicknessScale;
uniform float uDepthSentinel;
uniform float uIndexOfRefraction;
uniform vec3 uAbsorptionPerMetre;
uniform vec3 uScatteringColour;
uniform float uRefractionScale;
uniform float uRoughness;
uniform bool uReflectionEnabled;
uniform bool uRefractionEnabled;
uniform bool uAbsorptionEnabled;

bool valid_depth(float depth)
{
	return !isnan(depth) && !isinf(depth) &&
		depth > 0.0 && depth < 0.5 * uDepthSentinel;
}

bool valid_normal(vec3 normal)
{
	return !any(isnan(normal)) && !any(isinf(normal)) &&
		dot(normal, normal) > 1.0e-8;
}

vec3 depth_colour(float normalized_depth)
{
	float value = clamp(normalized_depth, 0.0, 1.0);
	return clamp(vec3(
		1.5 - abs(4.0 * value - 3.0),
		1.5 - abs(4.0 * value - 2.0),
		1.5 - abs(4.0 * value - 1.0)), 0.0, 1.0);
}

vec3 view_position(float linear_depth)
{
	vec4 view_h = uClipToView * vec4(uv * 2.0 - 1.0, 1.0, 1.0);
	vec3 view_ray = view_h.xyz / view_h.w;
	return view_ray * (linear_depth / max(-view_ray.z, 1.0e-8));
}

float hardware_depth(vec3 position_view)
{
	vec4 clip = uViewToClip * vec4(position_view, 1.0);
	return clamp(0.5 * clip.z / clip.w + 0.5, 0.0, 1.0);
}

vec3 sky_colour(vec3 direction_world)
{
	float height = clamp(0.5 * direction_world.y + 0.5, 0.0, 1.0);
	vec3 ground = vec3(0.035, 0.045, 0.055);
	vec3 horizon = vec3(0.42, 0.58, 0.72);
	vec3 zenith = vec3(0.055, 0.20, 0.48);
	vec3 lower = mix(ground, horizon, smoothstep(0.0, 0.52, height));
	return mix(lower, zenith, smoothstep(0.52, 1.0, height));
}

void main()
{
	vec4 scene = texture(uSceneColour, uv);
	float scene_depth = texture(uSceneDepth, uv).r;
	gl_FragDepth = scene_depth;
	if (uDisplayMode == 0) {
		frag_color = scene;
		return;
	}

	if (uDisplayMode == 6 || uDisplayMode == 7) {
		vec3 normal = texture(uNormal, uv).xyz;
		frag_color = valid_normal(normal)
			? vec4(normalize(normal) * 0.5 + 0.5, 1.0)
			: vec4(0.0, 0.0, 0.0, 1.0);
		return;
	}

	bool thickness_mode = uDisplayMode == 3 || uDisplayMode == 5;
	bool scalar_debug = uDisplayMode == 2 || uDisplayMode == 3 ||
		uDisplayMode == 4 || uDisplayMode == 5;
	if (scalar_debug) {
		float value = uDisplayMode == 2 ? texture(uRawDepth, uv).r
			: uDisplayMode == 3 ? texture(uRawThickness, uv).r
			: uDisplayMode == 4 ? texture(uSmoothDepth, uv).r
			: texture(uSmoothThickness, uv).r;
		bool valid = !isnan(value) && !isinf(value) &&
			(thickness_mode ? value > 0.0 : valid_depth(value));
		if (!valid) {
			frag_color = vec4(0.0, 0.0, 0.0, 1.0);
			return;
		}
		float scale = thickness_mode ? uDebugThicknessScale : uDebugDepthScale;
		frag_color = vec4(depth_colour(value / scale), 1.0);
		return;
	}

	float depth = texture(uSmoothDepth, uv).r;
	float thickness = texture(uSmoothThickness, uv).r;
	vec3 normal_view = texture(uNormal, uv).xyz;
	bool fluid = valid_depth(depth) && thickness > 0.0 &&
		!isnan(thickness) && !isinf(thickness) && valid_normal(normal_view);
	if (!fluid) {
		frag_color = uDisplayMode == 1 ? scene : vec4(0.0, 0.0, 0.0, 1.0);
		return;
	}

	normal_view = normalize(normal_view);
	vec3 position_view = view_position(depth);
	vec3 to_camera_view = normalize(-position_view);
	float cosine = clamp(dot(normal_view, to_camera_view), 0.0, 1.0);
	float eta_term = (uIndexOfRefraction - 1.0) / (uIndexOfRefraction + 1.0);
	float f0 = eta_term * eta_term;
	float fresnel = clamp(f0 + (1.0 - f0) * pow(1.0 - cosine, 5.0), 0.0, 1.0);

	vec2 texel = 0.5 / vec2(textureSize(uSceneColour, 0));
	float distortion = uRefractionEnabled
		? uRefractionScale * thickness / max(depth, 0.25) : 0.0;
	vec2 refracted_uv = clamp(uv - normal_view.xy * distortion,
	                          texel, vec2(1.0) - texel);
	vec3 refracted_scene = texture(uSceneColour, refracted_uv).rgb;
	vec3 transmission = uAbsorptionEnabled
		? exp(-uAbsorptionPerMetre * thickness) : vec3(1.0);
	vec3 scattering = uAbsorptionEnabled
		? uScatteringColour * (vec3(1.0) - transmission) : vec3(0.0);
	vec3 refracted = refracted_scene * transmission + scattering;

	vec3 incident_view = -to_camera_view;
	vec3 reflected_view = reflect(incident_view, normal_view);
	vec3 reflected_world = normalize(mat3(uViewToWorld) * reflected_view);
	vec3 rough_direction = normalize(mix(reflected_world,
		vec3(0.0, 1.0, 0.0), 0.35 * uRoughness * uRoughness));
	vec3 reflection = uReflectionEnabled ? sky_colour(rough_direction) : vec3(0.0);

	gl_FragDepth = hardware_depth(position_view);
	if (uDisplayMode == 8)
		frag_color = vec4(reflection, 1.0);
	else if (uDisplayMode == 9)
		frag_color = vec4(refracted_scene, 1.0);
	else if (uDisplayMode == 10)
		frag_color = vec4(transmission, 1.0);
	else
		frag_color = vec4(mix(refracted, reflection,
			uReflectionEnabled ? fresnel : 0.0), 1.0);
}
