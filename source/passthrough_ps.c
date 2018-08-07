#include "math.h"

typedef v2f32 float2;
typedef v3f32 float3;
typedef v4f32 float4;

struct Passthrough_Pixel_Shader {
	struct Ps_Input {
		float4 SV_POSITION;
		float3 COLOR;
		float  _pad;
	} ps_input;

	struct Ps_Output {
		float4 SV_TARGET;
	} ps_output;

	void(*ps_main)(void *p_fragment_input_data, void *p_fragment_output_data);
};

typedef struct Ps_Input Ps_Input;
typedef struct Ps_Output Ps_Output;

void ps_main(void *p_fragment_input_data, void *p_fragment_output_data) {
	Ps_Input *p_in = (Ps_Input*)p_fragment_input_data;
	Ps_Output *p_out = (Ps_Output*)p_fragment_output_data;

	p_out->SV_TARGET = (float4) { p_in->COLOR.x, p_in->COLOR.y, p_in->COLOR.z, 1.f };
}

static struct Passthrough_Pixel_Shader passthrough_ps = { { 0.f,0.f, },{ 0.f,0.f,0.f,0.f }, ps_main };