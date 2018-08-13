#include "common_shader.h"

struct Ps_Input {
	float4 SV_POSITION;
	float3 NORMAL;
	float2 UV;
	float _pad[3];
} ps_input;

struct Ps_Output {
	float4 SV_TARGET;
} ps_output;

typedef struct Ps_Input Ps_Input;
typedef struct Ps_Output Ps_Output;

const uint scene_tex_id = 0;
const uint env_tex_id = 1;

void ps_main(const void *p_fragment_input_data, void *p_fragment_output_data, const void **pp_shader_resource_views) {
	Ps_Input *p_in = (Ps_Input*)p_fragment_input_data;
	Ps_Output *p_out = (Ps_Output*)p_fragment_output_data;
	Texture2D scene_tex = *((Texture2D*)pp_shader_resource_views[scene_tex_id]);
	Texture2D env_tex = *((Texture2D*)pp_shader_resource_views[env_tex_id]);

	float3 normal = v3f32_normalize(p_in->NORMAL);
	float4 tex_color = sample_2D(scene_tex, p_in->UV);
	float4 irradiance = sample_2D_latlon(env_tex, normal);
	
	p_out->SV_TARGET = v4f32_mul_f32(v4f32_mul_v4f32(irradiance,tex_color),1.0/PI * 2);
	//p_out->SV_TARGET = (float4) {normal.x, normal.y, normal.z, 1.0};
}

struct PixelShader passthrough_ps = { ps_main };