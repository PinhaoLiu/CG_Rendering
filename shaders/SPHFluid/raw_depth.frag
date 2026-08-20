#version 430

flat in vec3 sphere_center_view;

layout (location = 0) out float raw_linear_depth;

uniform mat4 uViewToClip;
uniform mat4 uClipToView;
uniform vec2 uViewportSize;
uniform float uParticleRadius;

void main()
{
	vec2 uv = gl_FragCoord.xy / uViewportSize;
	vec2 ndc = 2.0 * uv - vec2(1.0);
	vec4 near_view_h = uClipToView * vec4(ndc, -1.0, 1.0);
	vec3 ray_direction = normalize(near_view_h.xyz / near_view_h.w);

	float projected_center = dot(ray_direction, sphere_center_view);
	float center_term = dot(sphere_center_view, sphere_center_view)
		- uParticleRadius * uParticleRadius;
	float discriminant = projected_center * projected_center - center_term;
	if (discriminant < 0.0)
		discard;

	float root = sqrt(discriminant);
	float hit_distance = projected_center - root;
	if (hit_distance <= 0.0)
		hit_distance = projected_center + root;
	if (hit_distance <= 0.0)
		discard;

	vec3 hit_view = ray_direction * hit_distance;
	float linear_depth = -hit_view.z;
	if (!(linear_depth > 0.0) || isnan(linear_depth) || isinf(linear_depth))
		discard;

	// Hardware depth must come from the actual sphere hit.  Radial distance is
	// not interchangeable with projected Z.
	vec4 hit_clip = uViewToClip * vec4(hit_view, 1.0);
	float hardware_depth = 0.5 * (hit_clip.z / hit_clip.w) + 0.5;
	if (hardware_depth < 0.0 || hardware_depth > 1.0)
		discard;

	gl_FragDepth = hardware_depth;
	raw_linear_depth = linear_depth;
}
