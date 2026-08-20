#version 430

in vec2 uv;

layout(location = 0) out vec4 fragment_colour;

void main()
{
	vec3 sky = mix(vec3(0.025, 0.055, 0.11), vec3(0.32, 0.58, 0.72), uv.y);
	vec2 cell = floor(uv * vec2(20.0, 12.0));
	float checker = mod(cell.x + cell.y, 2.0);
	vec3 colour = sky + mix(vec3(0.015), vec3(0.12), checker);

	float vertical = 1.0 - smoothstep(0.035, 0.065,
		min(fract(uv.x * 10.0), 1.0 - fract(uv.x * 10.0)));
	float horizontal = 1.0 - smoothstep(0.035, 0.065,
		min(fract(uv.y * 6.0), 1.0 - fract(uv.y * 6.0)));
	float grid = max(vertical, horizontal);
	colour = mix(colour, vec3(0.95, 0.43, 0.12), 0.45 * grid);

	fragment_colour = vec4(colour, 1.0);
}
