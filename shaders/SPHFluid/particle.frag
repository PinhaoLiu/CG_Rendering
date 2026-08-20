#version 410

in float view_depth;

layout (location = 0) out vec4 frag_color;

void main()
{
	vec2 centered = 2.0 * gl_PointCoord - vec2(1.0);
	float radius_squared = dot(centered, centered);
	if (radius_squared > 1.0)
		discard;

	float edge = 1.0 - smoothstep(0.72, 1.0, radius_squared);
	float depth_tint = clamp(0.75 - 0.15 * view_depth, 0.45, 0.95);
	vec3 water_colour = vec3(0.08, 0.48, 0.95) * depth_tint;
	frag_color = vec4(water_colour, edge);
}
