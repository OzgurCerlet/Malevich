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
	tex_coord.x = tex_coord.x > 1.0 ? 1.0 : (tex_coord.x < 0.0 ? 0.0 : tex_coord.x);
	tex_coord.y = tex_coord.y > 1.0 ? 1.0 : (tex_coord.y < 0.0 ? 0.0 : tex_coord.y);

	int s = (int)(tex.width * tex_coord.x - 0.5);
	int t = (int)(tex.height * tex_coord.y - 0.5);
	//uint texel = *(((uint*)tex.p_data) + t * tex.width + s);
	//return decode_u32_as_color(texel);

	float4 texel = *(((float4*)tex.p_data) + t * tex.width + s);

	return texel;
}

inline v4f256 sample_2D_x8(Texture2D tex, v2f256 tex_coord, i256 mask) {
	v4f256 result;
	v4f32 a_texels[8];
	f32 a_u_s[8];
	f32 a_v_s[8];
	for(i32 i = 0; i < 8; ++i) {
		if(!mask.m256i_i32[i]) continue;
		_mm256_store_ps(a_u_s, tex_coord.x);
		_mm256_store_ps(a_v_s, tex_coord.y);
		if(a_u_s[i] < 0.0 || a_u_s[i] > 1.0) {
			a_u_s[i] = 0;
		}
		if(a_v_s[i] < 0.0 || a_v_s[i] > 1.0) {
			a_v_s[i] = 0;
		}

		int s = (int)(tex.width * a_u_s[i] - 0.5);
		int t = (int)(tex.height * a_v_s[i] - 0.5);
		//uint texel = *(((uint*)tex.p_data) + t * tex.width + s);
		//a_texels[i] = decode_u32_as_color(texel);	
		a_texels[i] = *(((float4*)tex.p_data) + t * tex.width + s);
	}
	
	const i256 offset = _mm256_set_epi32(
		sizeof(v4f32)*7, sizeof(v4f32) * 6, sizeof(v4f32) * 5, sizeof(v4f32) * 4, 
		sizeof(v4f32) * 3, sizeof(v4f32) * 2, sizeof(v4f32) * 1, 0);
	
	result.x = _mm256_i32gather_ps(((f32*)a_texels), offset, 1);
	result.y = _mm256_i32gather_ps(((f32*)a_texels) + 1, offset, 1);
	result.z = _mm256_i32gather_ps(((f32*)a_texels) + 2, offset, 1);
	result.w = _mm256_i32gather_ps(((f32*)a_texels) + 3, offset, 1);

	return result;

}

inline float4 sample_2D_latlon(Texture2D tex, float3 dir) {
	f32 cos_theta = v3f32_dot((float3) { 0, 0, 1 }, dir);
	if(abs(cos_theta) == 1) return sample_2D(tex, (float2) { 0, 0 });
	
	f32 cos_x = v3f32_dot((float3) { 1, 0, 0 }, v3f32_normalize((float3) { dir.x, dir.y, 0.0 }));
	f32 cos_y = v3f32_dot((float3) { 0, 1, 0 }, v3f32_normalize((float3) { dir.x, dir.y, 0.0 }));

	f32 uv_x;
	if(cos_y >= 0) {
		uv_x = acos(cos_x) / TAU;
	}
	else {
		uv_x = 1.0 - acos(cos_x) / TAU;
	}

	f32 uv_y = acos(cos_theta) / PI;
	return sample_2D(tex, (float2) { uv_x, uv_y });
}

inline v4f256 sample_2D_latlon_x8(Texture2D tex, v3f256 dir) {
	f256 cos_theta = v3f256_dot(v3f256_normalize((v3f256) { _mm256_set1_ps(0.0), _mm256_set1_ps(0.0), _mm256_set1_ps(1.0)}), dir);
	v3f256 cos_xy = v3f256_normalize((v3f256) { dir.x, dir.y, _mm256_set1_ps(0) });
	f256 cos_x = v3f256_dot(v3f256_normalize((v3f256) { _mm256_set1_ps(1.0), _mm256_set1_ps(0.0), _mm256_set1_ps(0.0) }), cos_xy);
	f256 cos_y = v3f256_dot(v3f256_normalize((v3f256) { _mm256_set1_ps(0.0), _mm256_set1_ps(1.0), _mm256_set1_ps(0.0) }), cos_xy);

	f256 acos_x_over_tau = _mm256_mul_ps(_mm256_acos_ps(cos_x), _mm256_set1_ps(1.0 / TAU));
	f256 uv_x = _mm256_blendv_ps(_mm256_sub_ps(_mm256_set1_ps(1.0), acos_x_over_tau), acos_x_over_tau, _mm256_cmp_ps(cos_y, _mm256_set1_ps(0.0), _CMP_GE_OQ));
	f256 uv_y = _mm256_mul_ps(_mm256_acos_ps(cos_theta), _mm256_set1_ps(1.0 / PI));
	
	f256 cond = _mm256_or_ps(_mm256_cmp_ps(cos_theta, _mm256_set1_ps(0.99), _CMP_GT_OQ), _mm256_cmp_ps(cos_theta, _mm256_set1_ps(-0.99), _CMP_LT_OQ));
	uv_x = _mm256_blendv_ps(uv_x, _mm256_set1_ps(0), cond);
	uv_y = _mm256_blendv_ps(uv_y, _mm256_set1_ps(0), cond);

	return sample_2D_x8(tex, (v2f256) { uv_x, uv_y }, _mm256_set1_epi32(0xFFFFFFFF));
}