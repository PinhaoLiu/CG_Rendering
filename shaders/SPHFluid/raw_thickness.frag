#version 430

flat in vec3 sphere_center_view;

layout (location = 0) out float raw_thickness;

uniform mat4 uViewToClip;
uniform mat4 uClipToView;
uniform vec2 uViewportSize;
uniform float uParticleRadius;
uniform float uThicknessScale;

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
	float t_near = projected_center - root;
	float t_far = projected_center + root;
	if (t_far <= 0.0)
		discard;

	float visible_entry = max(t_near, 0.0);
	float chord_length = max(t_far - visible_entry, 0.0);
	float thickness = chord_length * uThicknessScale;
	if (!(thickness >= 0.0) || isnan(thickness) || isinf(thickness))
		discard;

	// Depth writes are disabled by the pass. This value is only used to test
	// the front of each sphere against opaque SceneDepth; fluid layers do not
	// occlude one another and therefore remain additive.
	float hardware_depth = 0.0;
	if (t_near > 0.0) {
		vec3 entry_view = ray_direction * t_near;
		vec4 entry_clip = uViewToClip * vec4(entry_view, 1.0);
		hardware_depth = 0.5 * (entry_clip.z / entry_clip.w) + 0.5;
	}
	if (hardware_depth < 0.0 || hardware_depth > 1.0)
		discard;

	gl_FragDepth = hardware_depth;
	raw_thickness = thickness;
}
