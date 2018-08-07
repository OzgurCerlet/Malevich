#include "math.h"
#include "common_shader.h"

typedef v2f32 float2;
typedef v3f32 float3;
typedef v4f32 float4;
	
struct Vs_Input {
	float4 POSITION;
	float3 COLOR;
} vs_input;

struct Vs_Output {
	float4 SV_POSITION;
	float3 COLOR;
	float  _pad;
} vs_output;

typedef struct Vs_Input Vs_Input;
typedef struct Vs_Output Vs_Output;

static void vs_main(const void *p_vertex_input_data, void *p_vertex_output_data, const void *p_constant_buffers, u32 vertex_id) {
	Vs_Input *p_in = (Vs_Input*)(p_vertex_input_data) + vertex_id;
	Vs_Output *p_out = (Vs_Output*)p_vertex_output_data + vertex_id;
	
	p_out->SV_POSITION = (float4){ p_in->POSITION.x * 2.0f - 1.0f, p_in->POSITION.y *2.0f - 1.0f, p_in->POSITION.z, p_in->POSITION.w};
	p_out->COLOR = p_in->COLOR;
}

VertexShader passthrough_vs = { sizeof(Vs_Input), sizeof(Vs_Output),  vs_main };
