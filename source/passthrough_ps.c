#include "common_shader_core.h"

typedef struct Ps_Input {
	v4f256 SV_POSITION;
	v3f256 COLOR;
	v2f256 UV;
} Ps_Input;

typedef struct Ps_Output {
	v4f256 SV_TARGET;
} Ps_Output;

static void ps_main(const void *p_fragment_input_data, void *p_fragment_output_data, const void **pp_shader_resource_views, i256 mask) {
	Ps_Input *p_in = (Ps_Input*)p_fragment_input_data;
	Ps_Output *p_out = (Ps_Output*)p_fragment_output_data;

	v3f256 color = { p_in->COLOR.x,  p_in->COLOR.y, p_in->COLOR.z };
	//color = v3f256_srgb_from_linear_approx(color);
	p_out->SV_TARGET.xyz = color;
}

struct PixelShader passthrough_ps = { ps_main };