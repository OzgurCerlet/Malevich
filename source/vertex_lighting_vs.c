#include "common_shader_core.h"

typedef struct Vs_Input {
	v3f256 POSITION;
	v3f256 NORMAL;
	v2f256 UV;
}Vs_Input;

typedef struct Vs_Output {
	v4f256 SV_POSITION;
	v3f256 COLOR;
	v2f256 UV;
	f256 _pad[3];
}Vs_Output;

typedef struct ConstantBuffer {
	float4x4 clip_from_world;
	float4x4 view_from_clip;
	float4x4 world_from_view;
} ConstantBuffer;

static void vs_main(const void *p_vertex_input_data, void *p_vertex_output_data, const void **pp_constant_buffers, const void **pp_shader_resource_views) {
	Vs_Input *p_in = ((Vs_Input*)p_vertex_input_data);
	Vs_Output *p_out = ((Vs_Output*)p_vertex_output_data);
	ConstantBuffer *p_cb = (ConstantBuffer*)(pp_constant_buffers[0]);
	Texture2D env_tex = *((Texture2D*)pp_shader_resource_views[0]);

	v4f256 pos_ws = { p_in->POSITION.x, p_in->POSITION.y, p_in->POSITION.z, _mm256_set1_ps(1.f) };
	v4f256 pos_cs = m4x4f32_mul_v4f256(&p_cb->clip_from_world, pos_ws);

	p_out->SV_POSITION = pos_cs;
	v3f256 normal = v3f256_normalize(p_in->NORMAL);
	v3f256 color = sample_2D_latlon_x8(env_tex, normal).xyz;
	float exposure = 1;
	color = v3f256_pow(v3f256_sub_v3f256((v3f256){ _mm256_set1_ps(1.f), _mm256_set1_ps(1.f), _mm256_set1_ps(1.f)}, v3f256_exp(v3f256_mul_f256(color, _mm256_set1_ps(-exposure)))), _mm256_set1_ps(1.0 / 2.2));

	p_out->COLOR = color;
	p_out->UV = (v2f256) { p_in->UV.x, p_in->UV.y };
}

VertexShader vertex_lighting_vs = { sizeof(Vs_Input), sizeof(Vs_Output), vs_main };