#include "common_shader.h"

struct Ps_Input {
	v4f256 SV_POSITION;
	v3f256 NORMAL;
	v2f256 UV;
} ps_input;

struct Ps_Output {
	v4f256 SV_TARGET;
} ps_output;

typedef struct Ps_Input Ps_Input;
typedef struct Ps_Output Ps_Output;

const uint scene_tex_id = 0;
const uint env_tex_id = 1;

void ps_main(const void *p_fragment_input_data, void *p_fragment_output_data, const void **pp_shader_resource_views, i256 mask) {
	Ps_Input *p_in = (Ps_Input*)p_fragment_input_data;
	Ps_Output *p_out = (Ps_Output*)p_fragment_output_data;
	Texture2D scene_tex = *((Texture2D*)pp_shader_resource_views[scene_tex_id]);

	//v3f256 normal = v3f256_normalize(p_in->NORMAL);
	//v3f256 color = (v3f256) { p_in->UV.x, p_in->UV.x, _mm256_set1_ps(0) };
	v3f256 color = sample_2D_x8(scene_tex, p_in->UV, mask).xyz;
	color = v3f256_srgb_from_linear_approx(color);

	p_out->SV_TARGET.xyz = color;
}

struct PixelShader passthrough_ps = { ps_main };