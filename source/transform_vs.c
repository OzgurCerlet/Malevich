#include "math.h"
#include "common_shader.h"

typedef v2f32 float2;
typedef v3f32 float3;
typedef v4f32 float4;
typedef uint32_t uint;
typedef m4x4f32 float4x4;

typedef struct Vs_Input {
	float3 POSITION;
	float3 NORMAL;
	float2 UV;
}Vs_Input;

typedef struct Vs_Output {
	float4 SV_POSITION;
	float3 COLOR;
	float  _pad;
}Vs_Output;

struct Transform_Vertex_Shader {
	uint32_t size_of_input_vertex;
	uint32_t size_of_output_vertex;
	void(*vs_main)(const void *p_vertex_input_data, void *p_vertex_output_data, const void **pp_contant_buffers, u32 vertex_id);
};

struct ConstantBuffer {
	float4x4 clip_from_world;
};

const uint cb_id = 0;
typedef struct ConstantBuffer ConstantBuffer;

static void vs_main(const void *p_vertex_input_data, void *p_vertex_output_data, const void **pp_contant_buffers, u32 vertex_id) {
	Vs_Input *p_in = ((Vs_Input*)p_vertex_input_data) + vertex_id;
	Vs_Output *p_out = ((Vs_Output*)p_vertex_output_data) + vertex_id;
	ConstantBuffer *p_cb = (ConstantBuffer*)(pp_contant_buffers[cb_id]);

	float4 pos_ws = { p_in->POSITION.x, p_in->POSITION.y, p_in->POSITION.z, 1.0 };
	float4 pos_cs = m4x4f32_mul_v4f32(&p_cb->clip_from_world, pos_ws);

	p_out->SV_POSITION = pos_cs;
	p_out->COLOR = (float3) { p_in->NORMAL.x, p_in->NORMAL.y, p_in->NORMAL.z };
}

VertexShader transform_vs = { sizeof(Vs_Input), sizeof(Vs_Output), vs_main };