#version 430

layout (std430, binding = 0) readonly buffer PositionBuffer
{
	vec4 positions[];
};

uniform mat4 uWorldToView;
uniform mat4 uViewToClip;
uniform float uParticleRadius;

flat out vec3 sphere_center_view;

void main()
{
	vec2 corners[4] = vec2[4](
		vec2(-1.0, -1.0),
		vec2( 1.0, -1.0),
		vec2(-1.0,  1.0),
		vec2( 1.0,  1.0));

	vec3 center = (uWorldToView * positions[gl_InstanceID]).xyz;
	sphere_center_view = center;

	float distance_to_camera = -center.z;
	float radius_squared = uParticleRadius * uParticleRadius;
	float denominator = distance_to_camera * distance_to_camera - radius_squared;

	// A sphere intersecting the eye plane can cover the whole viewport.  The
	// fragment shader and the real near plane still reject invalid hits.
	if (denominator <= 1.0e-8) {
		gl_Position = vec4(corners[gl_VertexID], 0.0, 1.0);
		return;
	}

	// Exact tangent bounds of x / -z and y / -z.  This is conservative at
	// off-axis and near-camera positions, unlike a center-depth billboard.
	vec2 center_xy = center.xy;
	vec2 tangent_root = sqrt(max(
		vec2(distance_to_camera * distance_to_camera) + center_xy * center_xy
			- vec2(radius_squared),
		vec2(0.0)));
	vec2 ratio_min = (center_xy * distance_to_camera
		- vec2(uParticleRadius) * tangent_root) / denominator;
	vec2 ratio_max = (center_xy * distance_to_camera
		+ vec2(uParticleRadius) * tangent_root) / denominator;
	vec2 ratio = mix(ratio_min, ratio_max, 0.5 * (corners[gl_VertexID] + 1.0));
	vec2 ndc = vec2(uViewToClip[0][0], uViewToClip[1][1]) * ratio
		+ vec2(uViewToClip[2][0], uViewToClip[2][1]);
	gl_Position = vec4(ndc, 0.0, 1.0);
}
