#pragma once

typedef struct VertexShader {
	unsigned int in_vertex_size;
	unsigned int out_vertex_size;
	void(*vs_main)(const void *p_vertex_input_data, void *p_vertex_output_data, const void *p_constant_buffers, unsigned int vertex_id);
}VertexShader;

typedef struct PixelShader {
	void(*ps_main)(void *p_fragment_input_data, void *p_fragment_output_data);
} PixelShader;
