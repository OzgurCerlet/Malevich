#include "common_shader_core.h"
	
typedef struct Vs_Input {
	v4f256 POSITION;
	v3f256 COLOR;
	f256 _pad;
} Vs_Input;

typedef struct Vs_Output {
	v4f256 SV_POSITION;
	v3f256 COLOR;
	v2f256 UV;
	f256 _pad[3];
}Vs_Output;

static void vs_main(const void *p_vertex_input_data, void *p_vertex_output_data, const void *p_constant_buffers, u32 vertex_id) {
	Vs_Input *p_in = ((Vs_Input*)p_vertex_input_data);
	Vs_Output *p_out = ((Vs_Output*)p_vertex_output_data);
	
	p_out->SV_POSITION.x = _mm256_fmadd_ps(p_in->POSITION.x, _mm256_set1_ps(2.0f), _mm256_set1_ps(-1.0f));
	p_out->SV_POSITION.y = _mm256_fmadd_ps(p_in->POSITION.y, _mm256_set1_ps(2.0f), _mm256_set1_ps(-1.0f));
	p_out->SV_POSITION.z = p_in->POSITION.z;
	p_out->SV_POSITION.w = p_in->POSITION.w;
	p_out->COLOR = p_in->COLOR;
}

VertexShader passthrough_vs = { sizeof(Vs_Input), sizeof(Vs_Output),  vs_main };
