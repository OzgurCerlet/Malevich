#include "common_shader_core.h"

typedef struct Ps_Input {
	v4f256 SV_POSITION;
	v3f256 NORMAL;
	v2f256 UV;
} Ps_Input;

typedef struct Ps_Output {
	v4f256 SV_TARGET;
} Ps_Output;

static void ps_main(const void *p_fragment_input_data, void *p_fragment_output_data, const void **pp_shader_resource_views, i256 mask) {
	Ps_Input *p_in = (Ps_Input*)p_fragment_input_data;
	Ps_Output *p_out = (Ps_Output*)p_fragment_output_data;
	Texture2D env_tex = *((Texture2D*)pp_shader_resource_views[0]);

	v3f256 normal = v3f256_normalize(p_in->NORMAL);
	v3f256 color = sample_2D_latlon_x8(env_tex, normal).xyz;
	float exposure = 1;
	color = v3f256_pow(v3f256_sub_v3f256((v3f256) { _mm256_set1_ps(1.f), _mm256_set1_ps(1.f), _mm256_set1_ps(1.f) }, v3f256_exp(v3f256_mul_f256(color, _mm256_set1_ps(-exposure)))), _mm256_set1_ps(1.0 / 2.2));

	p_out->SV_TARGET.xyz = color;
}

struct PixelShader env_lighting_ps = { ps_main };