#include "common_shader_core.h"
	
typedef struct Vs_Input {
	v4f256 POSITION;
	v3f256 VIEW_DIR;
	f256 _pad;
} Vs_Input;

typedef struct Vs_Output {
	v4f256 SV_POSITION;
	v3f256 VIEW_DIR;
	v2f256 UV;
	f256 _pad[3];
}Vs_Output;

typedef struct ConstantBuffer{
	float4x4 clip_from_world;
	float4x4 view_from_clip;
	float4x4 world_from_view;
} ConstantBuffer;

static void vs_main(const void *p_vertex_input_data, void *p_vertex_output_data, const void **pp_constant_buffers) {
	Vs_Input *p_in = ((Vs_Input*)p_vertex_input_data);
	Vs_Output *p_out = ((Vs_Output*)p_vertex_output_data);
	ConstantBuffer *p_cb = (ConstantBuffer*)(pp_constant_buffers[0]);
	
	v4f256 pos_cs = {
		_mm256_fmadd_ps(p_in->POSITION.x, _mm256_set1_ps(2.0f), _mm256_set1_ps(-1.0f)),
		_mm256_fmadd_ps(p_in->POSITION.y, _mm256_set1_ps(2.0f), _mm256_set1_ps(-1.0f)),
		p_in->POSITION.z,
		p_in->POSITION.w
	};

	v4f256 dir_vs = m4x4f32_mul_v4f256(&p_cb->view_from_clip, pos_cs);
	v4f256 dir_ws = m4x4f32_mul_v4f256(&p_cb->world_from_view, (v4f256) { dir_vs.x, dir_vs.y, dir_vs.z, _mm256_set1_ps(0.0)});

	p_out->SV_POSITION = pos_cs;
	p_out->VIEW_DIR = dir_ws.xyz;
}

VertexShader fullscreen_vs = { sizeof(Vs_Input), sizeof(Vs_Output),  vs_main };
