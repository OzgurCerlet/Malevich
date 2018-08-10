#pragma once
#include "math.h"

typedef v2f32 float2;
typedef v3f32 float3;
typedef v4f32 float4;
typedef uint32_t uint;
typedef m4x4f32 float4x4;

typedef struct VertexShader {
	unsigned int in_vertex_size;
	unsigned int out_vertex_size;
	void(*vs_main)(const void *p_vertex_input_data, void *p_vertex_output_data, const void *p_constant_buffers, unsigned int vertex_id);
}VertexShader;

typedef struct PixelShader {
	void(*ps_main)(void *p_fragment_input_data, void *p_fragment_output_data, const void *p_shader_resource_views);
} PixelShader;

typedef struct Texture2D {
	void *p_data;
	uint width;
	uint height;
} Texture2D;

inline float4 sample_2D(Texture2D tex, float2 tex_coord) {
	int s = (int)(tex.width * tex_coord.x - 0.5);
	int t = (int)(tex.height * tex_coord.y - 0.5);
	uint texel = *(((uint*)tex.p_data) + s * tex.width + t);
	return decode_u32_as_color(texel);

	return (float4) { tex_coord.x, tex_coord.y, 0.0,0.0 };
}