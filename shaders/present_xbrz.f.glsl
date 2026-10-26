/* Copyright (C) 2026 RQ_modding
 *
 * Edge-aware pixel upscale (xBR-style, labeled "xbrz"). Fits the GL present path.
 */

uniform sampler2D tex;
uniform vec2 tex_size;

in vec2 tex_coord;
out vec4 frag_color;

vec3 texel_at(vec2 p)
{
	return texture(tex, (p + vec2(0.5)) / tex_size).rgb;
}

float dist(vec3 a, vec3 b)
{
	vec3 d = a - b;
	return dot(d, d);
}

void main()
{
	vec2 pixel = tex_coord * tex_size;
	vec2 p = floor(pixel);
	vec2 f = fract(pixel);

	vec3 a = texel_at(p + vec2(-1.0, -1.0));
	vec3 b = texel_at(p + vec2( 0.0, -1.0));
	vec3 c = texel_at(p + vec2( 1.0, -1.0));
	vec3 d = texel_at(p + vec2(-1.0,  0.0));
	vec3 e = texel_at(p);
	vec3 g = texel_at(p + vec2( 1.0,  0.0));
	vec3 h = texel_at(p + vec2(-1.0,  1.0));
	vec3 i = texel_at(p + vec2( 0.0,  1.0));
	vec3 j = texel_at(p + vec2( 1.0,  1.0));

	vec3 color = e;
	if (f.x < 0.5 && f.y < 0.5) {
		if (dist(d, b) < dist(e, a) && dist(d, b) < 0.08)
			color = mix(e, mix(d, b, 0.5), 0.55);
	} else if (f.x >= 0.5 && f.y < 0.5) {
		if (dist(g, b) < dist(e, c) && dist(g, b) < 0.08)
			color = mix(e, mix(g, b, 0.5), 0.55);
	} else if (f.x < 0.5 && f.y >= 0.5) {
		if (dist(d, i) < dist(e, h) && dist(d, i) < 0.08)
			color = mix(e, mix(d, i, 0.5), 0.55);
	} else {
		if (dist(g, i) < dist(e, j) && dist(g, i) < 0.08)
			color = mix(e, mix(g, i, 0.5), 0.55);
	}

	float a_tex = texture(tex, tex_coord).a;
	frag_color = vec4(color, a_tex);
}
