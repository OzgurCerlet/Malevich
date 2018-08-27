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
	void(*vs_main)();
}VertexShader;

typedef struct PixelShader {
	void(*ps_main)();
} PixelShader;

typedef struct Texture2D {
	void *p_data;
	uint width;
	uint height;
} Texture2D;

inline uint get_texel_u(Texture2D tex, i32 s, i32 t) {
	return *(((uint*)tex.p_data) + MAX(MIN(t, tex.height - 1), 0) * tex.width + MAX(MIN(s, tex.width - 1), 0));
}

inline i256 get_texel_u_x8(Texture2D tex, i256 s, i256 t) {
	s = _mm256_max_epi32(_mm256_min_epi32(s, _mm256_set1_epi32(tex.width  - 1)), _mm256_set1_epi32(0));
	t = _mm256_max_epi32(_mm256_min_epi32(t, _mm256_set1_epi32(tex.height - 1)), _mm256_set1_epi32(0));
	s = _mm256_add_epi32(_mm256_mullo_epi32(t, _mm256_set1_epi32(tex.width)), s);
	i256 result = _mm256_i32gather_epi32(tex.p_data, s, 4);
	return result;
}

inline float4 get_texel_f(Texture2D tex, i32 s, i32 t) {
	return *(((float4*)tex.p_data) + MAX(MIN(t, tex.height - 1), 0) * tex.width + MAX(MIN(s, tex.width - 1), 0));
}

inline v4f256 get_texel_f_x8(Texture2D tex, i256 s, i256 t) {
	s = _mm256_max_epi32(_mm256_min_epi32(s, _mm256_set1_epi32(tex.width - 1)), _mm256_set1_epi32(0));
	t = _mm256_max_epi32(_mm256_min_epi32(t, _mm256_set1_epi32(tex.height - 1)), _mm256_set1_epi32(0));
	s = _mm256_add_epi32(_mm256_mullo_epi32(t, _mm256_set1_epi32(tex.width)), s);
	s = _mm256_mullo_epi32(s, _mm256_set1_epi32(4));
	v4f256 result;
	result.x = _mm256_i32gather_ps(tex.p_data, s, 4);
	result.y = _mm256_i32gather_ps(tex.p_data, _mm256_add_epi32(s, _mm256_set1_epi32(1)), 4);
	result.z = _mm256_i32gather_ps(tex.p_data, _mm256_add_epi32(s, _mm256_set1_epi32(2)), 4);
	result.w = _mm256_i32gather_ps(tex.p_data, _mm256_add_epi32(s, _mm256_set1_epi32(3)), 4);
	return result;
}

inline v4f32 point_u(Texture2D tex, f32 u, f32 v) {
	i32 s = (i32)(tex.width * u);
	i32 t = (i32)(tex.height * (1.0 - v));
	v4f32 texel = decode_u32_as_color(get_texel_u(tex, s, t));
	return texel;
}

inline v4f256 point_u_x8(Texture2D tex, f256 u, f256 v) {
	i256 s = _mm256_cvtps_epi32(_mm256_mul_ps(_mm256_set1_ps((f32)tex.width), u));
	i256 t = _mm256_cvtps_epi32(_mm256_mul_ps(_mm256_set1_ps((f32)tex.height), _mm256_sub_ps(_mm256_set1_ps(1.0), v)));
	v4f256 texel = decode_u32_as_color_x8(get_texel_u_x8(tex, s, t));
	return texel;
}

inline v4f32 point_f(Texture2D tex, f32 u, f32 v) {
	i32 s = (i32)(tex.width * u);
	i32 t = (i32)(tex.height * (1.0 - v));
	v4f32 texel = get_texel_f(tex, s, t);
	return texel;
}

inline v4f256 point_f_x8(Texture2D tex, f256 u, f256 v) {
	i256 s = _mm256_cvtps_epi32(_mm256_mul_ps(_mm256_set1_ps((f32)tex.width), u));
	i256 t = _mm256_cvtps_epi32(_mm256_mul_ps(_mm256_set1_ps((f32)tex.height), _mm256_sub_ps(_mm256_set1_ps(1.0), v)));
	v4f256 texel = get_texel_f_x8(tex, s, t);
	return texel;
}

inline v4f32 bilinear_u(Texture2D tex, f32 u, f32 v) {
	f32 s_f32 = tex.width * u - 0.5;
	f32 t_f32 = tex.height * (1.0 - v) - 0.5;
	i32 s = (int)floor(s_f32);
	i32 t = (int)floor(t_f32);
	f32 frac_s = s_f32 - (float)s;
	f32 frac_t = t_f32 - (float)t;

	v4f32 texel_00 = decode_u32_as_color(get_texel_u(tex, s, t));
	v4f32 texel_10 = decode_u32_as_color(get_texel_u(tex, s + 1, t));
	v4f32 texel_01 = decode_u32_as_color(get_texel_u(tex, s, t + 1));
	v4f32 texel_11 = decode_u32_as_color(get_texel_u(tex, s + 1, t + 1));

	v4f32 texel_0010 = v4f32_lerp(texel_00, texel_10, frac_s);
	v4f32 texel_0111 = v4f32_lerp(texel_01, texel_11, frac_s);

	v4f32 result = v4f32_lerp(texel_0010, texel_0111, frac_t);
	return result;
}

inline v4f256 bilinear_u_x8(Texture2D tex, f256 u, f256 v) {
	f256 s_f32 = _mm256_add_ps(_mm256_mul_ps(_mm256_set1_ps((f32)tex.width), u), _mm256_set1_ps(-0.5));
	f256 t_f32 = _mm256_add_ps(_mm256_mul_ps(_mm256_set1_ps((f32)tex.height), _mm256_sub_ps(_mm256_set1_ps(1.0),  v)), _mm256_set1_ps(-0.5));
	i256 s = _mm256_cvtps_epi32(_mm256_floor_ps(s_f32));
	i256 t = _mm256_cvtps_epi32(_mm256_floor_ps(t_f32));
	f256 frac_s = _mm256_sub_ps(s_f32, _mm256_cvtepi32_ps(s));
	f256 frac_t = _mm256_sub_ps(t_f32, _mm256_cvtepi32_ps(t));

	v4f256 texel_00 = decode_u32_as_color_x8(get_texel_u_x8(tex, s, t));
	v4f256 texel_10 = decode_u32_as_color_x8(get_texel_u_x8(tex, _mm256_add_epi32(s, _mm256_set1_epi32(1)), t));
	v4f256 texel_0010 = v4f256_lerp(texel_00, texel_10, frac_s);
	
	v4f256 texel_01 = decode_u32_as_color_x8(get_texel_u_x8(tex, s, _mm256_add_epi32(t, _mm256_set1_epi32(1))));
	v4f256 texel_11 = decode_u32_as_color_x8(get_texel_u_x8(tex, _mm256_add_epi32(s, _mm256_set1_epi32(1)), _mm256_add_epi32(t, _mm256_set1_epi32(1))));
	v4f256 texel_0111 = v4f256_lerp(texel_01, texel_11, frac_s);

	v4f256 result = v4f256_lerp(texel_0010, texel_0111, frac_t);
	return result;
}

inline v4f32 bilinear_f(Texture2D tex, f32 u, f32 v) {
	f32 s_f32 = tex.width * u - 0.5;
	f32 t_f32 = tex.height * (1.0 - v) - 0.5;
	i32 s = (int)floor(s_f32);
	i32 t = (int)floor(t_f32);
	f32 frac_s = s_f32 - (float)s;
	f32 frac_t = t_f32 - (float)t;

	v4f32 texel_00 = get_texel_f(tex, s, t);
	v4f32 texel_10 = get_texel_f(tex, s + 1, t);
	v4f32 texel_01 = get_texel_f(tex, s, t + 1);
	v4f32 texel_11 = get_texel_f(tex, s + 1, t + 1);

	v4f32 texel_0010 = v4f32_lerp(texel_00, texel_10, frac_s);
	v4f32 texel_0111 = v4f32_lerp(texel_01, texel_11, frac_s);

	v4f32 result = v4f32_lerp(texel_0010, texel_0111, frac_t);
	return result;
}

inline v4f256 bilinear_f_x8(Texture2D tex, f256 u, f256 v) {
	f256 s_f32 = _mm256_add_ps(_mm256_mul_ps(_mm256_set1_ps((f32)tex.width), u), _mm256_set1_ps(-0.5));
	f256 t_f32 = _mm256_add_ps(_mm256_mul_ps(_mm256_set1_ps((f32)tex.height), _mm256_sub_ps(_mm256_set1_ps(1.0), v)), _mm256_set1_ps(-0.5));
	i256 s = _mm256_cvtps_epi32(_mm256_floor_ps(s_f32));
	i256 t = _mm256_cvtps_epi32(_mm256_floor_ps(t_f32));
	f256 frac_s = _mm256_sub_ps(s_f32, _mm256_cvtepi32_ps(s));
	f256 frac_t = _mm256_sub_ps(t_f32, _mm256_cvtepi32_ps(t));

	v4f256 texel_00 = get_texel_f_x8(tex, s, t);
	v4f256 texel_10 = get_texel_f_x8(tex, _mm256_add_epi32(s, _mm256_set1_epi32(1)), t);
	v4f256 texel_0010 = v4f256_lerp(texel_00, texel_10, frac_s);

	v4f256 texel_01 = get_texel_f_x8(tex, s, _mm256_add_epi32(t, _mm256_set1_epi32(1)));
	v4f256 texel_11 = get_texel_f_x8(tex, _mm256_add_epi32(s, _mm256_set1_epi32(1)), _mm256_add_epi32(t, _mm256_set1_epi32(1)));
	v4f256 texel_0111 = v4f256_lerp(texel_01, texel_11, frac_s);

	v4f256 result = v4f256_lerp(texel_0010, texel_0111, frac_t);
	return result;
}

inline float4 sample_2D(Texture2D tex, float2 tex_coord) {
	float4 texel = get_texel_f(tex, tex_coord.x, tex_coord.y);
	return texel;
}

inline v4f256 sample_2D_x8_masked(Texture2D tex, v2f256 tex_coord, i256 mask) {
	v4f256 result;
	v4f32 a_texels[8];
	f32 a_u_s[8];
	f32 a_v_s[8];
	for(i32 i = 0; i < 8; ++i) {
		if(!mask.m256i_i32[i]) continue;
		_mm256_store_ps(a_u_s, tex_coord.x);
		_mm256_store_ps(a_v_s, tex_coord.y);

		//a_texels[i] = point_u(tex, a_u_s[i], a_v_s[i]);
		a_texels[i] = bilinear_u(tex, a_u_s[i], a_v_s[i]);
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

inline v4f256 sample_2D_u_x8(Texture2D tex, v2f256 tex_coord, i256 mask) {
	//v4f256 result = point_u_x8(tex, tex_coord.x, tex_coord.y);
	v4f256 result = bilinear_u_x8(tex, tex_coord.x, tex_coord.y);
	return result;
}

inline v4f256 sample_2D_f_x8(Texture2D tex, v2f256 tex_coord) {
	v4f256 result = point_f_x8(tex, tex_coord.x, tex_coord.y);
	//v4f256 result = bilinear_f_x8(tex, tex_coord.x, tex_coord.y);
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
	
	f256 pos_cond = _mm256_cmp_ps(cos_theta, _mm256_set1_ps(0.99), _CMP_GT_OQ);
	f256 neg_cond = _mm256_cmp_ps(cos_theta, _mm256_set1_ps(-0.99), _CMP_LT_OQ);
	uv_x = _mm256_blendv_ps(uv_x, _mm256_set1_ps(0.5), _mm256_or_ps(pos_cond, neg_cond));
	uv_y = _mm256_blendv_ps(uv_y, _mm256_set1_ps(0.0), pos_cond);
	uv_y = _mm256_blendv_ps(uv_y, _mm256_set1_ps(1.0), neg_cond);
	uv_y = _mm256_sub_ps(_mm256_set1_ps(1.0), uv_y);

	return sample_2D_f_x8(tex, (v2f256) { uv_x, uv_y });
}