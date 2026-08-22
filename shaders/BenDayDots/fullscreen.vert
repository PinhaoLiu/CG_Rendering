#version 410

out vec2 uv;

void main()
{
	vec2 positions[3] = vec2[3](
		vec2(-1.0, -1.0),
		vec2( 3.0, -1.0),
		vec2(-1.0,  3.0));
	vec2 position = positions[gl_VertexID];
	uv = 0.5 * (position + vec2(1.0));
	gl_Position = vec4(position, 0.0, 1.0);
}
