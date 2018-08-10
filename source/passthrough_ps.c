#include "common_shader.h"

struct Ps_Input {
	float4 SV_POSITION;
	float3 COLOR;
	float2 UV;
	float _pad[3];
} ps_input;

struct Ps_Output {
	float4 SV_TARGET;
} ps_output;

typedef struct Ps_Input Ps_Input;
typedef struct Ps_Output Ps_Output;

const uint tex_id = 0;

void ps_main(const void *p_fragment_input_data, void *p_fragment_output_data, const void **pp_shader_resource_views) {
	Ps_Input *p_in = (Ps_Input*)p_fragment_input_data;
	Ps_Output *p_out = (Ps_Output*)p_fragment_output_data;
	Texture2D tex = *((Texture2D*)pp_shader_resource_views[tex_id]);

	p_out->SV_TARGET = sample_2D(tex, p_in->UV);
}

struct PixelShader passthrough_ps = { ps_main };