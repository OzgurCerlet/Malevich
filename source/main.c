#define _CRT_SECURE_NO_WARNINGS

#define LEAN_AND_MEAN
#include <windows.h>
#include <omp.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>

#include "math.h"
#include "common_shader.h"
#include "external/Remotery/Remotery.h"
#include "external/octarine/octarine_mesh.h"
typedef int DXGI_FORMAT;
#include "external/octarine/octarine_image.h"

#pragma comment(lib, "octarine_mesh.lib")
#pragma comment(lib, "octarine_image.lib")

#define WIDTH	1024 //820;
#define HEIGHT	512 //1000;
#define STRECTH_FACTOR 1

#define TILE_WIDTH	8 
#define TILE_HEIGHT 8 

#define VECTOR_WIDTH 8
#define TRIANGLE_COUNT_FACTOR 1
#define MAX_TRIANGLE_COUNT_PER_BIN 256

#define WIDTH_IN_TILES	(WIDTH/TILE_WIDTH)
#define HEIGHT_IN_TILES (HEIGHT/TILE_HEIGHT)
#define NUM_BINS (WIDTH_IN_TILES*HEIGHT_IN_TILES)

const int frame_width = WIDTH;
const int frame_height = HEIGHT;

u32 frame_buffer[WIDTH][HEIGHT];
f32 depth_buffer[WIDTH][HEIGHT];

#define MAX_NUM_CLIP_VERTICES 16
#define NUM_SUB_PIXEL_PRECISION_BITS 4
#define PIXEL_SHADER_INPUT_REGISTER_COUNT 4
#define COMMONSHADER_CONSTANT_BUFFER_HW_SLOT_COUNT 16
#define COMMONSHADER_INPUT_RESOURCE_REGISTER_COUNT 128

extern VertexShader transform_vs;
extern VertexShader transform_vs_simd;
extern VertexShader passthrough_vs;
extern PixelShader passthrough_ps;

typedef struct MeshHeader {
	uint32_t size;
	uint32_t vertex_count;
	uint32_t index_count;
} MeshHeader;

typedef struct Mesh {
	MeshHeader header;
	void *p_vertex_buffer;
	u32 *p_index_buffer;
} Mesh;

typedef enum PrimitiveTopology {
	PRIMITIVE_TOPOLOGY_UNDEFINED = 0,
	PRIMITIVE_TOPOLOGY_TRIANGLELIST = 1
} PrimitiveTopology;

typedef struct IA {
	u32 *p_index_buffer;// TODO(cerlet): 16-bit index buffers!
	void *p_vertex_buffer;
	u32 input_layout;
	PrimitiveTopology primitive_topology;
} IA;

typedef struct VS {
	void (*shader)(const void *p_vertex_input_data, void *p_vertex_output_data, const void *p_constant_buffers, const void *p_shader_resource_views);
	u8 output_register_count;
	void *p_constant_buffers[COMMONSHADER_CONSTANT_BUFFER_HW_SLOT_COUNT];
	void *p_shader_resource_views[COMMONSHADER_INPUT_RESOURCE_REGISTER_COUNT];
} VS;

typedef struct Viewport {
	f32 top_left_x;
	f32 top_left_y;
	f32 width;
	f32 height;
	f32 min_depth;
	f32 max_depth;
} Viewport;

typedef struct RS {
	Viewport viewport;
} RS;

typedef struct PS {
	void(*shader)(void *p_pixel_input_data, void *p_pixel_output_data, const void *p_shader_resource_views);
	void *p_shader_resource_views[COMMONSHADER_INPUT_RESOURCE_REGISTER_COUNT];
} PS;

typedef struct OM {
	u32 *p_colors;
	f32 *p_depth;
	//u8 num_render_targets;
} OM;

typedef struct Pipeline {
	IA ia;
	VS vs;
	RS rs;
	PS ps;
	OM om;
} Pipeline;

Pipeline graphics_pipeline;

typedef struct SuprematistVertex {
	v4f32 pos;
	v3f32 color;
} SuprematistVertex;

typedef struct Vertex {
	v4f32 a_attributes[PIXEL_SHADER_INPUT_REGISTER_COUNT];
}Vertex;

typedef struct EdgeFunction{
	i32 a;
	i32 b;
	i32 c;
} EdgeFunction;

typedef struct Setup {
	EdgeFunction a_edge_functions[3];
	f32 a_reciprocal_ws[3];
	f32 one_over_area;
}Setup;

typedef struct Triangle {
	v4f32 *p_attributes;
	v2i32 min_bounds;
	v2i32 max_bounds;
	Setup setup;
} Triangle;

typedef struct Bin {
	u32 num_triangles_self;
	u32 num_triangles_upto;
} Bin;

typedef struct CompactedBin{
	u32 num_triangles_self;
	u32 num_triangles_upto;
	u32 bin_index;
} CompactedBin;

typedef struct Fragment {
	v4f32 *p_attributes;
	v2i32 coordinates;
	v2f32 barycentric_coords;
	v2f32 perspective_barycentric_coords;
} Fragment;

typedef struct TileInfo {
	u32 triangle_id;
	u64 fragment_mask;
} TileInfo;

typedef struct Tile {
	u32 a_colors[64];
	f32 a_depths[64];
} Tile;

typedef struct PerFrameCB {
	m4x4f32 clip_from_world;
}PerFrameCB;

typedef struct Camera {
	m4x4f32 clip_from_view;
	m4x4f32 view_from_world;
	v3f32 pos;
	f32 yaw_rad;
	f32 pitch_rad;
	f32 fov_y_angle_deg;
	f32 near_plane;
	f32 far_plane;
} Camera;

typedef struct Input {
	v2f32 last_mouse_pos;
	v2f32 mouse_pos;
	bool is_right_mouse_button_pressed;
	bool is_a_pressed;
	bool is_d_pressed;
	bool is_e_pressed;
	bool is_q_pressed;
	bool is_s_pressed;
	bool is_w_pressed;
} Input;

Mesh test_mesh;
Texture2D scene_tex;
Texture2D env_tex;
PerFrameCB per_frame_cb;
Camera camera;
Input input;
Bin	a_bins[NUM_BINS];
CompactedBin *p_compacted_bins;
u32  num_compacted_bins;

LRESULT CALLBACK window_proc(HWND h_window, UINT msg, WPARAM w_param, LPARAM l_param)
{
	PAINTSTRUCT paint_struct;
	HDC h_device_context;
	switch(msg) {
		case WM_PAINT:
			h_device_context = BeginPaint(h_window, &paint_struct);
			BITMAPINFO info = { 0 };
			ZeroMemory(&info, sizeof(BITMAPINFO));
			info.bmiHeader.biBitCount = 32;
			info.bmiHeader.biWidth = frame_width;
			info.bmiHeader.biHeight = -(i32)(frame_height); // for top_down images we need to negate the height
			info.bmiHeader.biPlanes = 1;
			info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			info.bmiHeader.biCompression = BI_RGB;
			StretchDIBits(h_device_context, 0, 0, frame_width*STRECTH_FACTOR, frame_height*STRECTH_FACTOR, 0, 0, frame_width, frame_height, frame_buffer, &info, DIB_RGB_COLORS, SRCCOPY);
			EndPaint(h_window, &paint_struct);
			break;
		case WM_KEYDOWN: {
			switch(w_param){
				case 'A': input.is_a_pressed = true; break;
				case 'D': input.is_d_pressed = true; break;
				case 'E': input.is_e_pressed = true; break;
				case 'Q': input.is_q_pressed = true; break;
				case 'S': input.is_s_pressed = true; break;
				case 'W': input.is_w_pressed = true; break;
			}
		} break;
		case WM_KEYUP: {
			switch(w_param) {
				case 'A': input.is_a_pressed = false; break;
				case 'D': input.is_d_pressed = false; break;
				case 'E': input.is_e_pressed = false; break;
				case 'Q': input.is_q_pressed = false; break;
				case 'S': input.is_s_pressed = false; break;
				case 'W': input.is_w_pressed = false; break;
			}
		} break;
		case WM_MOUSEMOVE: {
			input.mouse_pos.x = (signed short)(l_param);
			input.mouse_pos.y = (signed short)(l_param >> 16);
		} break;
		case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: {
			input.is_right_mouse_button_pressed = true;
		} break;
		case WM_RBUTTONUP: {
			input.is_right_mouse_button_pressed = false;
		} break;
		case WM_DESTROY:
			PostQuitMessage(0);
			break;
		default:
			return DefWindowProc(h_window, msg, w_param, l_param);
	}

	return 0;
}

void error_win32(const char* func_name, DWORD last_error) {
	void* error_msg = NULL;
	char display_msg[256] = { 0 };

	FormatMessageA(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		last_error,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(char*)&error_msg,
		0,
		NULL);

	strcat(display_msg, func_name);
	strcat(display_msg, " failed with error: ");
	strcat(display_msg, (const char*)error_msg);

	MessageBoxA(NULL, display_msg, NULL, MB_OK | MB_ICONERROR);
	LocalFree(error_msg);
}

inline void set_edge_function(EdgeFunction *p_edge, i32 signed_area, i32 x0, i32 y0, i32 x1, i32 y1) {
	i32 a = y0 - y1;
	i32 b = x1 - x0;
	if(signed_area < 0) {
		a = -a;
		b = -b;
	}
	i32 c = -a * x0 - b * y0;

	p_edge->a = a;
	p_edge->b = b;
	p_edge->c = c;
}

inline bool is_inside_edge(Setup *p_setup, const u32 edge_index, const v2i32 frag_coord) {
	//i32 a = p_setup->a_edge_functions[edge_index].a;
	//i32 b = p_setup->a_edge_functions[edge_index].b;
	//i32 c = p_setup->a_edge_functions[edge_index].c;
	//// Need to lift a and be to c's precision level
	//i32 signed_distance = ((a * frag_coord.x) << NUM_SUB_PIXEL_PRECISION_BITS) + ((b * frag_coord.y) << NUM_SUB_PIXEL_PRECISION_BITS) + c;
	//p_setup->a_signed_distances[edge_index] = signed_distance;
	//if(signed_distance > 0) return true;
	//if(signed_distance < 0) return false;
	//if(a > 0) return true;
	//if(a < 0) return false;
	//if(b > 0) return true;
	//return false;
}

inline bool is_inside_triangle(Setup *p_setup, v2i32 frag_coord) {
	bool w0 = is_inside_edge(p_setup, 0, frag_coord);
	bool w1 = is_inside_edge(p_setup, 1, frag_coord);
	bool w2 = is_inside_edge(p_setup, 2, frag_coord);
	return w0 && w1 && w2;
}

// In this context, barycentric coords (u,v,w) = areal coordinates (A1/A,A2/A,A0/A) => u+v+w = 1;
inline v2f32 compute_barycentric_coords(const Setup *p_setup) {
	//v2f32 barycentric_coords;
	//barycentric_coords.x = (float)(p_setup->a_signed_distances[1] >> (NUM_SUB_PIXEL_PRECISION_BITS * 2)) * p_setup->one_over_area;
	//barycentric_coords.y = (float)(p_setup->a_signed_distances[2] >> (NUM_SUB_PIXEL_PRECISION_BITS * 2)) * p_setup->one_over_area;

	//return barycentric_coords;
}

inline v2f32 compute_perspective_barycentric_coords(const Setup *p_setup, v2f32 barycentric_coordinates) {
	v2f32 perspective_barycentric_coords;
	float u_bary = barycentric_coordinates.x;
	float v_bary = barycentric_coordinates.y;

	float denom = (1.0 - u_bary - v_bary) * p_setup->a_reciprocal_ws[0] + u_bary * p_setup->a_reciprocal_ws[1] + v_bary * p_setup->a_reciprocal_ws[2];
	perspective_barycentric_coords.x = u_bary * p_setup->a_reciprocal_ws[1] / denom;
	perspective_barycentric_coords.y = v_bary * p_setup->a_reciprocal_ws[2] / denom;

	return perspective_barycentric_coords;
}

__forceinline v4f32 interpolate_attribute(const v4f32 *p_attributes, v2f32 barycentric_coords, u8 num_attributes_per_vertex) {
	v4f32 v0_attribute = *(p_attributes );
	v4f32 v1_attribute = *(p_attributes + num_attributes_per_vertex);
	v4f32 v2_attribute = *(p_attributes + num_attributes_per_vertex * 2);

	return v0_attribute =
		v4f32_add_v4f32(v0_attribute,
			v4f32_add_v4f32(
				v4f32_mul_f32(
					v4f32_subtract_v4f32(v1_attribute, v0_attribute), barycentric_coords.x
				),
				v4f32_mul_f32(
					v4f32_subtract_v4f32(v2_attribute, v0_attribute), barycentric_coords.y
				)
			)
		);
}

SuprematistVertex suprematist_vertex_buffer[] = {
	{ { 0.31219, 0.15,  0.5,  1.0}, { 0.08627, 0.07450, 0.07058 } },
	{ { 0.92804, 0.142, 0.5,  1.0}, { 0.08627, 0.07450, 0.07058 } },
	{ { 0.92682, 0.883, 0.5,  1.0}, { 0.08627, 0.07450, 0.07058 } },
	{ { 0.32195, 0.889, 0.5,  1.0}, { 0.08627, 0.07450, 0.07058 } },
	{ { 0.07317, 0.41,  0.25, 1.0}, { 0.17647, 0.17254, 0.32549 } },
	{ { 0.66463, 0.614, 0.25, 1.0}, { 0.17647, 0.17254, 0.32549 } },
	{ { 0.08170, 0.887, 0.25, 1.0}, { 0.17647, 0.17254, 0.32549 } },
	{ { 0.0, 0.0, 0.75, 1.0},{ 0.89019, 0.87450, 0.84705 } },
	{ { 1.0, 0.0, 0.75, 1.0 },{ 0.89019, 0.87450, 0.84705 } },
	{ { 1.0, 1.0, 0.75, 1.0 },{ 0.89019, 0.87450, 0.84705 } },
	{ { 0.0, 1.0, 0.75, 1.0 },{ 0.89019, 0.87450, 0.84705 } },
};

u32 suprematist_index_buffer[] = {
	0, 1, 2,
	2, 3, 0,
	4, 6, 5,
	7, 8, 9,
	9, 10, 7,
};

// canvas size  820 x 1000 
//					0			1		2			2		3			0
// black square {{256,150},{761,142},{760,883}},{{760,883},{264,889},{256,150}} color {22,19,18}
// blue triangle {{60,410},{545,614},{67,887}} color {45,44,83}
// background color {227, 223, 216}

void run_input_assembler_stage_omp_simd(u32 index_count, void **pp_vertex_input_data) {
	rmt_BeginCPUSample(input_assambler_stage, 0);

	// Input Assembler
	assert(graphics_pipeline.ia.primitive_topology == PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// ASSUMPTION(cerlet): In Direct3D, index buffers are bounds checked!, we assume our index buffers are properly bounded.
	// TODO(cerlet): Implement some kind of post-transform vertex cache.
	assert((index_count & 0b111) == 0); // ASSUMPTION(cerlet): index_count is divisible by 8

	u32 vertex_count = index_count;
	u32 per_vertex_input_data_size = graphics_pipeline.ia.input_layout;
	void *p_vertex_input_data = malloc(vertex_count*per_vertex_input_data_size);
	f256 *p_vertex = p_vertex_input_data;
	
	#pragma omp parallel for schedule(dynamic, 128)
	for(u32 index_index = 0; index_index < index_count; index_index += 8) {
		f256 *p_vertex = ((f256*)p_vertex_input_data) + index_index;
		i256 index = _mm256_set_epi32(index_index + 7, index_index + 6, index_index + 5, index_index + 4, index_index + 3, index_index + 2, index_index + 1, index_index);
		//u32 vertex_index = graphics_pipeline.ia.p_index_buffer[index_index];
		i256 vertex_index = _mm256_i32gather_epi32((i32*)graphics_pipeline.ia.p_index_buffer, index, 4);
		//i256 mask = _mm256_cmpgt_epi32(_mm256_set1_epi32(index_count), index);
		//i256 vertex_index = _mm256_mask_i32gather_epi32(_mm256_set1_epi32(0), (i32*)graphics_pipeline.ia.p_index_buffer, index, mask, 4);
		//u32 vertex_offset = vertex_index * per_vertex_input_data_size;
		i256 vertex_offset = _mm256_mullo_epi32(vertex_index, _mm256_set1_epi32(per_vertex_input_data_size));
		//memcpy(p_vertex, ((u8*)graphics_pipeline.ia.p_vertex_buffer) + vertex_offset, per_vertex_input_data_size);
		p_vertex[0] = _mm256_i32gather_ps(((f32*)graphics_pipeline.ia.p_vertex_buffer) + 0, vertex_offset, 1);
		p_vertex[1] = _mm256_i32gather_ps(((f32*)graphics_pipeline.ia.p_vertex_buffer) + 1, vertex_offset, 1);
		p_vertex[2] = _mm256_i32gather_ps(((f32*)graphics_pipeline.ia.p_vertex_buffer) + 2, vertex_offset, 1);
		p_vertex[3] = _mm256_i32gather_ps(((f32*)graphics_pipeline.ia.p_vertex_buffer) + 3, vertex_offset, 1);
		p_vertex[4] = _mm256_i32gather_ps(((f32*)graphics_pipeline.ia.p_vertex_buffer) + 4, vertex_offset, 1);
		p_vertex[5] = _mm256_i32gather_ps(((f32*)graphics_pipeline.ia.p_vertex_buffer) + 5, vertex_offset, 1);
		p_vertex[6] = _mm256_i32gather_ps(((f32*)graphics_pipeline.ia.p_vertex_buffer) + 6, vertex_offset, 1);
		p_vertex[7] = _mm256_i32gather_ps(((f32*)graphics_pipeline.ia.p_vertex_buffer) + 7, vertex_offset, 1);
	}
	*pp_vertex_input_data = p_vertex_input_data;

	rmt_EndCPUSample();
}

void run_input_assembler_stage_simd(u32 index_count, void **pp_vertex_input_data) {
	rmt_BeginCPUSample(input_assambler_stage, 0);
	
	// Input Assembler
	assert(graphics_pipeline.ia.primitive_topology == PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// ASSUMPTION(cerlet): In Direct3D, index buffers are bounds checked!, we assume our index buffers are properly bounded.
	// TODO(cerlet): Implement some kind of post-transform vertex cache.
	assert((index_count & 0b111) == 0); // ASSUMPTION(cerlet): index_count is divisible by 8
	
	u32 vertex_count = index_count;
	u32 per_vertex_input_data_size = graphics_pipeline.ia.input_layout;
	void *p_vertex_input_data = malloc(vertex_count*per_vertex_input_data_size);
	f256 *p_vertex = p_vertex_input_data;
	for(u32 index_index = 0; index_index < index_count; index_index+=8) {
		i256 index = _mm256_set_epi32(index_index + 7, index_index + 6, index_index + 5, index_index + 4, index_index+ 3, index_index + 2, index_index + 1, index_index);
		//u32 vertex_index = graphics_pipeline.ia.p_index_buffer[index_index];
		i256 vertex_index = _mm256_i32gather_epi32((i32*)graphics_pipeline.ia.p_index_buffer, index, 4);
		//u32 vertex_offset = vertex_index * per_vertex_input_data_size;
		i256 vertex_offset = _mm256_mullo_epi32(vertex_index, _mm256_set1_epi32(per_vertex_input_data_size));
		//memcpy(p_vertex, ((u8*)graphics_pipeline.ia.p_vertex_buffer) + vertex_offset, per_vertex_input_data_size);
		*p_vertex++ = _mm256_i32gather_ps(((f32*)graphics_pipeline.ia.p_vertex_buffer) + 0, vertex_offset, 1);
		*p_vertex++ = _mm256_i32gather_ps(((f32*)graphics_pipeline.ia.p_vertex_buffer) + 1, vertex_offset, 1);
		*p_vertex++ = _mm256_i32gather_ps(((f32*)graphics_pipeline.ia.p_vertex_buffer) + 2, vertex_offset, 1);
		*p_vertex++ = _mm256_i32gather_ps(((f32*)graphics_pipeline.ia.p_vertex_buffer) + 3, vertex_offset, 1);
		*p_vertex++ = _mm256_i32gather_ps(((f32*)graphics_pipeline.ia.p_vertex_buffer) + 4, vertex_offset, 1);
		*p_vertex++ = _mm256_i32gather_ps(((f32*)graphics_pipeline.ia.p_vertex_buffer) + 5, vertex_offset, 1);
		*p_vertex++ = _mm256_i32gather_ps(((f32*)graphics_pipeline.ia.p_vertex_buffer) + 6, vertex_offset, 1);
		*p_vertex++ = _mm256_i32gather_ps(((f32*)graphics_pipeline.ia.p_vertex_buffer) + 7, vertex_offset, 1);
	}
	*pp_vertex_input_data = p_vertex_input_data;
	
	rmt_EndCPUSample();
}

void run_vertex_shader_stage_omp(u32 vertex_count, const void * p_vertex_input_data, u32 *p_per_vertex_output_data_size, void **pp_vertex_output_data) {
	rmt_BeginCPUSample(vertex_shader_stage, 0);
	
	// Vertex Shader
	u32 per_vertex_input_data_size = graphics_pipeline.ia.input_layout;
	u32 per_vertex_output_data_size = graphics_pipeline.vs.output_register_count * sizeof(v4f32);
	void **p_constant_buffers = graphics_pipeline.vs.p_constant_buffers;
	void *p_vertex_output_data = malloc(vertex_count*per_vertex_output_data_size);
	#pragma omp parallel for schedule(dynamic)
	for(u32 vertex_id = 0; vertex_id < vertex_count; ++vertex_id) {
		void *p_vertex_input = ((u8*)p_vertex_input_data) + vertex_id * per_vertex_input_data_size;
		void *p_vertex_output = ((u8*)p_vertex_output_data) + vertex_id * per_vertex_output_data_size;
		graphics_pipeline.vs.shader(p_vertex_input, p_vertex_output, p_constant_buffers, graphics_pipeline.vs.p_shader_resource_views);
	}
	*p_per_vertex_output_data_size = per_vertex_output_data_size;
	*pp_vertex_output_data = p_vertex_output_data;
	
	rmt_EndCPUSample();
}

void run_vertex_shader_stage_omp_simd(u32 vertex_count, const void * p_vertex_input_data, u32 *p_per_vertex_output_data_size, void **pp_vertex_output_data) {
	rmt_BeginCPUSample(vertex_shader_stage, 0);
	
	// Vertex Shader
	u32 per_vertex_input_data_size = graphics_pipeline.ia.input_layout;
	u32 per_vertex_output_data_size = graphics_pipeline.vs.output_register_count * sizeof(v4f32);
	void **p_constant_buffers = graphics_pipeline.vs.p_constant_buffers;
	void *p_vertex_output_data = malloc(vertex_count*per_vertex_output_data_size);
	
	#pragma omp parallel for schedule(dynamic, 128)
	for(u32 vertex_id = 0; vertex_id < vertex_count; vertex_id +=8 ) {
		u8 *p_vertex_input = (u8*)p_vertex_input_data + vertex_id * per_vertex_input_data_size;
		f32 *p_vertex_output = (f32*)((u8*)p_vertex_output_data + vertex_id * per_vertex_output_data_size);
		f256 vertex_output[12];
		graphics_pipeline.vs.shader(p_vertex_input, vertex_output, p_constant_buffers, graphics_pipeline.vs.p_shader_resource_views);

		for(int i = 0; i < 8; i++) {
			*(p_vertex_output++) = vertex_output[0].m256_f32[i];
			*(p_vertex_output++) = vertex_output[1].m256_f32[i];
			*(p_vertex_output++) = vertex_output[2].m256_f32[i];
			*(p_vertex_output++) = vertex_output[3].m256_f32[i];
			*(p_vertex_output++) = vertex_output[4].m256_f32[i];
			*(p_vertex_output++) = vertex_output[5].m256_f32[i];
			*(p_vertex_output++) = vertex_output[6].m256_f32[i];
			*(p_vertex_output++) = vertex_output[7].m256_f32[i];
			*(p_vertex_output++) = vertex_output[8].m256_f32[i];
			*(p_vertex_output++) = vertex_output[9].m256_f32[i];
			*(p_vertex_output++) = vertex_output[10].m256_f32[i];
			*(p_vertex_output++) = vertex_output[11].m256_f32[i];
		}
	}

	*p_per_vertex_output_data_size = per_vertex_output_data_size;
	*pp_vertex_output_data = p_vertex_output_data;
	
	rmt_EndCPUSample();
}

void clip_by_plane(Vertex *p_clipped_vertices, v4f32 plane_normal, f32 plane_d, i32 *p_num_vertices ) {

	u32 num_out_vertices = 0;
	u32 num_vertices = *p_num_vertices;
	u32 num_attributes = graphics_pipeline.vs.output_register_count;
	static u32 num_generated_clipped_vertices = 0;
	Vertex a_result_vertices[MAX_NUM_CLIP_VERTICES];

	f32 current_dot = v4f32_dot(plane_normal, (p_clipped_vertices)[0].a_attributes[0]);
	bool is_current_inside = current_dot > -plane_d;

	for(int i = 0; i < num_vertices; i++) {
		assert(num_out_vertices < MAX_NUM_CLIP_VERTICES);

		int next = (i + 1) % num_vertices;
		if(is_current_inside) {
			a_result_vertices[num_out_vertices++] = p_clipped_vertices[i];
		}

		float next_dot = v4f32_dot(plane_normal, p_clipped_vertices[next].a_attributes[0]);
		bool is_next_inside = next_dot > -plane_d;
		if(is_current_inside != is_next_inside) {
			assert(num_generated_clipped_vertices < MAX_NUM_CLIP_VERTICES);
			f32 t = (plane_d + current_dot) / (current_dot - next_dot);
			for(u32 attribute_index = 0; attribute_index < num_attributes; ++attribute_index) {
				a_result_vertices[num_out_vertices].a_attributes[attribute_index] = v4f32_add_v4f32(
					v4f32_mul_f32(p_clipped_vertices[i].a_attributes[attribute_index], (1.f - t)),
					v4f32_mul_f32(p_clipped_vertices[next].a_attributes[attribute_index], t));
			}
			num_out_vertices++;
		}

		current_dot = next_dot;
		is_current_inside = is_next_inside;
	}

	*p_num_vertices = num_out_vertices;
	memcpy(p_clipped_vertices, a_result_vertices, sizeof(Vertex)*num_out_vertices);
}

void run_clipper(Vertex *p_clipped_vertices, i32 *p_num_clipped_vertices) {
	rmt_BeginCPUSample(clipper, RMTSF_Aggregate);

	clip_by_plane(p_clipped_vertices, v4f32_normalize((v4f32) { 1, 0, 0, 1 }), 0, p_num_clipped_vertices);	// -w <= x <==> 0 <= x + w
	clip_by_plane(p_clipped_vertices, v4f32_normalize((v4f32) {-1, 0, 0, 1 }), 0, p_num_clipped_vertices);	//  x <= w <==> 0 <= w - x
	clip_by_plane(p_clipped_vertices, v4f32_normalize((v4f32) { 0, 1, 0, 1 }), 0, p_num_clipped_vertices);	// -w <= y <==> 0 <= y + w
	clip_by_plane(p_clipped_vertices, v4f32_normalize((v4f32) { 0,-1, 0, 1 }), 0, p_num_clipped_vertices);	//  y <= w <==> 0 <= w - y
	clip_by_plane(p_clipped_vertices, v4f32_normalize((v4f32) { 0, 0, 1, 1 }), 0, p_num_clipped_vertices);	// -w <= z <==> 0 <= z + w
	clip_by_plane(p_clipped_vertices, v4f32_normalize((v4f32) { 0, 0,-1, 1 }), 0, p_num_clipped_vertices);	//  z <= w <==> 0 <= w - z

	rmt_EndCPUSample();
}

void run_primitive_assembly_stage(u32 in_triangle_count, const void* p_vertex_output_data, u32 *p_out_triangle_count, Triangle **pp_triangles, v4f32 **pp_attributes) {
	rmt_BeginCPUSample(primitive_assembly_stage, 0);
	// Primitive Assembly
	const u32 max_clipper_generated_triangle_count = max(in_triangle_count * 2, 512);
	const u32 out_triangle_count = in_triangle_count + max_clipper_generated_triangle_count;
	const u32 num_attributes = graphics_pipeline.vs.output_register_count;
	const u32 per_vertex_offset = num_attributes * sizeof(v4f32);
	const u32 triangle_data_size = per_vertex_offset * 3;
	
	*pp_triangles = malloc(sizeof(Triangle) * out_triangle_count);
	*pp_attributes = malloc(triangle_data_size * out_triangle_count);
	
	u32 shared_out_triangle_index = 0;

	#pragma omp parallel for schedule(dynamic,128)
	for(u32 in_triangle_index = 0; in_triangle_index < in_triangle_count; ++in_triangle_index) {

		v4f32 a_vertex_positions[3];
		a_vertex_positions[0] = *((v4f32*)((u8*)p_vertex_output_data + in_triangle_index * triangle_data_size));
		a_vertex_positions[1] = *((v4f32*)((u8*)p_vertex_output_data + in_triangle_index * triangle_data_size + per_vertex_offset));
		a_vertex_positions[2] = *((v4f32*)((u8*)p_vertex_output_data + in_triangle_index * triangle_data_size + per_vertex_offset * 2));

		// viewport culling
		if(a_vertex_positions[0].w == 0 || a_vertex_positions[1].w == 0 || a_vertex_positions[2].w == 0) {
			continue;// degenerate triangle
		}

		// clip space culling
		if(
			(a_vertex_positions[0].x < -a_vertex_positions[0].w && a_vertex_positions[1].x < -a_vertex_positions[1].w && a_vertex_positions[2].x < -a_vertex_positions[2].w) ||
			(a_vertex_positions[0].x > +a_vertex_positions[0].w && a_vertex_positions[1].x > +a_vertex_positions[1].w && a_vertex_positions[2].x > +a_vertex_positions[2].w) ||
			(a_vertex_positions[0].y < -a_vertex_positions[0].w && a_vertex_positions[1].y < -a_vertex_positions[1].w && a_vertex_positions[2].y < -a_vertex_positions[2].w) ||
			(a_vertex_positions[0].y > +a_vertex_positions[0].w && a_vertex_positions[1].y > +a_vertex_positions[1].w && a_vertex_positions[2].y > +a_vertex_positions[2].w) ||
			(a_vertex_positions[0].z < 0.f						&& a_vertex_positions[1].z < 0.f					  && a_vertex_positions[2].z < 0.f) ||
			(a_vertex_positions[0].z > +a_vertex_positions[0].w	&& a_vertex_positions[1].z > +a_vertex_positions[1].w && a_vertex_positions[2].z > +a_vertex_positions[2].w)) {
			continue;
		}

		// clipping
		bool is_clipping_needed = !(
			(a_vertex_positions[0].x >= -a_vertex_positions[0].w && a_vertex_positions[1].x >= -a_vertex_positions[1].w && a_vertex_positions[2].x >= -a_vertex_positions[2].w) &&
			(a_vertex_positions[0].x <= +a_vertex_positions[0].w && a_vertex_positions[1].x <= +a_vertex_positions[1].w && a_vertex_positions[2].x <= +a_vertex_positions[2].w) &&
			(a_vertex_positions[0].y >= -a_vertex_positions[0].w && a_vertex_positions[1].y >= -a_vertex_positions[1].w && a_vertex_positions[2].y >= -a_vertex_positions[2].w) &&
			(a_vertex_positions[0].y <= +a_vertex_positions[0].w && a_vertex_positions[1].y <= +a_vertex_positions[1].w && a_vertex_positions[2].y <= +a_vertex_positions[2].w) &&
			(a_vertex_positions[0].z >= 0.f						 && a_vertex_positions[1].z >= 0.f					    && a_vertex_positions[2].z >= 0.f) &&
			(a_vertex_positions[0].z <= +a_vertex_positions[0].w && a_vertex_positions[1].z <= +a_vertex_positions[1].w && a_vertex_positions[2].z <= +a_vertex_positions[2].w));

		Vertex a_clipped_vertices[MAX_NUM_CLIP_VERTICES];
		i32 clipped_vertex_count = 3;
		u32 num_attributes = graphics_pipeline.vs.output_register_count;
		u32 vertex_size = num_attributes * sizeof(v4f32);

		// In order to have the same code path for non-clipped triangles with clipped triangles, initialize clipped vertices array with the original vertex data
		memcpy(a_clipped_vertices, ((v4f32*)p_vertex_output_data) + in_triangle_index * num_attributes * 3, per_vertex_offset);
		memcpy(a_clipped_vertices + 1, ((v4f32*)p_vertex_output_data) + in_triangle_index * num_attributes * 3 + num_attributes, per_vertex_offset);
		memcpy(a_clipped_vertices + 2, ((v4f32*)p_vertex_output_data) + in_triangle_index * num_attributes * 3 + num_attributes * 2, per_vertex_offset);

		if(is_clipping_needed) {
			run_clipper(&a_clipped_vertices, &clipped_vertex_count);
		}

		for(i32 clipped_vertex_index = 1; clipped_vertex_index < clipped_vertex_count - 1; ++clipped_vertex_index) {
			a_vertex_positions[0] = a_clipped_vertices[0].a_attributes[0];
			a_vertex_positions[1] = a_clipped_vertices[clipped_vertex_index].a_attributes[0];
			a_vertex_positions[2] = a_clipped_vertices[clipped_vertex_index + 1].a_attributes[0];

			// projection : Clip Space --> NDC Space
			f32 a_reciprocal_ws[3];
			a_reciprocal_ws[0] = 1.0 / a_vertex_positions[0].w;
			a_vertex_positions[0].x *= a_reciprocal_ws[0];
			a_vertex_positions[0].y *= a_reciprocal_ws[0];
			a_vertex_positions[0].z *= a_reciprocal_ws[0];
			a_vertex_positions[0].w *= a_reciprocal_ws[0];

			a_reciprocal_ws[1] = 1.0 / a_vertex_positions[1].w;
			a_vertex_positions[1].x *= a_reciprocal_ws[1];
			a_vertex_positions[1].y *= a_reciprocal_ws[1];
			a_vertex_positions[1].z *= a_reciprocal_ws[1];
			a_vertex_positions[1].w *= a_reciprocal_ws[1];

			a_reciprocal_ws[2] = 1.0 / a_vertex_positions[2].w;
			a_vertex_positions[2].x *= a_reciprocal_ws[2];
			a_vertex_positions[2].y *= a_reciprocal_ws[2];
			a_vertex_positions[2].z *= a_reciprocal_ws[2];
			a_vertex_positions[2].w *= a_reciprocal_ws[2];

			// viewport transformation : NDC Space --> Screen Space
			Viewport viewport = graphics_pipeline.rs.viewport;
			v4f32 vertex_pos_ss;
			m4x4f32 screen_from_ndc = {
				viewport.width*0.5, 0, 0, viewport.width*0.5 + viewport.top_left_x,
				0, -viewport.height*0.5, 0, viewport.height*0.5 + viewport.top_left_y,
				0, 0, viewport.max_depth - viewport.min_depth, viewport.min_depth,
				0,	0,	0,	1
			};

			vertex_pos_ss = m4x4f32_mul_v4f32(&screen_from_ndc, a_vertex_positions[0]);
			a_vertex_positions[0] = vertex_pos_ss;

			vertex_pos_ss = m4x4f32_mul_v4f32(&screen_from_ndc, a_vertex_positions[1]);
			a_vertex_positions[1] = vertex_pos_ss;

			vertex_pos_ss = m4x4f32_mul_v4f32(&screen_from_ndc, a_vertex_positions[2]);
			a_vertex_positions[2] = vertex_pos_ss;

			// convert ss positions to fixed-point representation and snap
			i32 x[3], y[3], signed_area;
			x[0] = floor(a_vertex_positions[0].x * (1 << NUM_SUB_PIXEL_PRECISION_BITS) + 0.5);
			x[1] = floor(a_vertex_positions[1].x * (1 << NUM_SUB_PIXEL_PRECISION_BITS) + 0.5);
			x[2] = floor(a_vertex_positions[2].x * (1 << NUM_SUB_PIXEL_PRECISION_BITS) + 0.5);
			y[0] = floor(a_vertex_positions[0].y * (1 << NUM_SUB_PIXEL_PRECISION_BITS) + 0.5);
			y[1] = floor(a_vertex_positions[1].y * (1 << NUM_SUB_PIXEL_PRECISION_BITS) + 0.5);
			y[2] = floor(a_vertex_positions[2].y * (1 << NUM_SUB_PIXEL_PRECISION_BITS) + 0.5);

			// triangle setup
			signed_area = ((x[1] - x[0]) * (y[2] - y[0])) - ((x[2] - x[0]) * (y[1] - y[0]));
			if(signed_area == 0) { continue; } // degenerate triangle 
			if(abs(signed_area) < (1 << (NUM_SUB_PIXEL_PRECISION_BITS * 2))) { continue; }; // degenerate triangle ?

			// face culling with winding order
			if(signed_area > 0) { continue; }; // ASSUMPTION(cerlet): Default back-face culling

			Setup setup;
			set_edge_function(&setup.a_edge_functions[2], signed_area, x[0], y[0], x[1], y[1]);
			set_edge_function(&setup.a_edge_functions[0], signed_area, x[1], y[1], x[2], y[2]);
			set_edge_function(&setup.a_edge_functions[1], signed_area, x[2], y[2], x[0], y[0]);

			f32 signed_area_f32 = (f32)(signed_area >> (NUM_SUB_PIXEL_PRECISION_BITS * 2));
			if(signed_area_f32 == 0) {
				DebugBreak();
			}
			setup.one_over_area = fabs(1.f / signed_area_f32);
			setup.a_reciprocal_ws[0] = a_reciprocal_ws[0];
			setup.a_reciprocal_ws[1] = a_reciprocal_ws[1];
			setup.a_reciprocal_ws[2] = a_reciprocal_ws[2];

			u32 out_triangle_index;
			#pragma omp atomic capture
			{ out_triangle_index = shared_out_triangle_index; shared_out_triangle_index += 1; }

			memcpy((*pp_attributes) + out_triangle_index * num_attributes * 3, &a_clipped_vertices[0], per_vertex_offset);
			memcpy((*pp_attributes) + out_triangle_index * num_attributes * 3 + num_attributes, &a_clipped_vertices[clipped_vertex_index], per_vertex_offset);
			memcpy((*pp_attributes) + out_triangle_index * num_attributes * 3 + num_attributes * 2, &a_clipped_vertices[clipped_vertex_index + 1], per_vertex_offset);
			
			*((*pp_attributes) + out_triangle_index * num_attributes*3) = a_vertex_positions[0];
			*((*pp_attributes) + out_triangle_index * num_attributes*3 + num_attributes) = a_vertex_positions[1];
			*((*pp_attributes) + out_triangle_index * num_attributes*3 + num_attributes * 2) = a_vertex_positions[2];

			Triangle *p_current_triangle = (*pp_triangles) + out_triangle_index;
			p_current_triangle->setup = setup;

			v2i32 min_bounds;
			v2i32 max_bounds;
			min_bounds.x = MIN3(x[0], x[1], x[2]) >> NUM_SUB_PIXEL_PRECISION_BITS;
			min_bounds.y = MIN3(y[0], y[1], y[2]) >> NUM_SUB_PIXEL_PRECISION_BITS;
			min_bounds.x = MIN(MAX(min_bounds.x, 0), (i32)viewport.width - 1); // prevent negative coords
			min_bounds.y = MIN(MAX(min_bounds.y, 0), (i32)viewport.height - 1);
			// max corner
			max_bounds.x = MAX3(x[0], x[1], x[2]) >> NUM_SUB_PIXEL_PRECISION_BITS;
			max_bounds.y = MAX3(y[0], y[1], y[2]) >> NUM_SUB_PIXEL_PRECISION_BITS;
			max_bounds.x = MIN(max_bounds.x + 1, (i32)viewport.width - 1);	// prevent too large coords
			max_bounds.y = MIN(max_bounds.y + 1, (i32)viewport.height - 1);

			//int current_bounds_size = ((max_bounds.x - min_bounds.x + 1) * (max_bounds.y - min_bounds.y + 1));
			//assert(current_bounds_size > 0);
			//max_possible_fragment_count += current_bounds_size;
			p_current_triangle->min_bounds = min_bounds;
			p_current_triangle->max_bounds = max_bounds;

			p_current_triangle->p_attributes = (*pp_attributes) + out_triangle_index * 3 * num_attributes;
		}	
	}

	*p_out_triangle_count = shared_out_triangle_index;

	rmt_EndCPUSample();
}

void run_binner(u32 assembled_triangle_count, const Triangle *p_triangles, u32 **pp_triangle_ids, u32* p_total_triangle_count ) {
	rmt_BeginCPUSample(binner, 0);
	u32 current_counts[NUM_BINS];
	for(u32 bin_index = 0; bin_index < NUM_BINS; ++bin_index) {
		a_bins[bin_index].num_triangles_self = 0;
		a_bins[bin_index].num_triangles_upto = 0;
		current_counts[bin_index] = 0;
	}

	for(u32 triangle_index = 0; triangle_index < assembled_triangle_count; ++triangle_index) {
		Triangle tri = p_triangles[triangle_index];

		v2i32 min_bounds_in_tiles = { MAX(tri.min_bounds.x / TILE_WIDTH, 0), MAX(tri.min_bounds.y / TILE_HEIGHT, 0) };
		v2i32 max_bounds_in_tiles = { MIN(tri.max_bounds.x / TILE_WIDTH, WIDTH_IN_TILES -1), MIN(tri.max_bounds.y / TILE_HEIGHT, HEIGHT_IN_TILES-1) };

		for(i32 y = min_bounds_in_tiles.y; y <= max_bounds_in_tiles.y; ++y) {
			for(i32 x = min_bounds_in_tiles.x; x <= max_bounds_in_tiles.x; ++x) {
				u32 bin_index = y * WIDTH_IN_TILES + x;
				a_bins[bin_index].num_triangles_self++;
			}
		}
	}
	u32 num_bins_with_tris = 0;
	for(u32 bin_index = 1; bin_index < NUM_BINS; ++bin_index) {
		u32 curr_num_tris = a_bins[bin_index - 1].num_triangles_self;
		if(curr_num_tris) num_bins_with_tris++;
		a_bins[bin_index].num_triangles_upto = curr_num_tris + a_bins[bin_index - 1].num_triangles_upto;
	}

	u32 total_num_triangles_in_bins = a_bins[NUM_BINS - 1].num_triangles_upto + a_bins[NUM_BINS - 1].num_triangles_self;
	*pp_triangle_ids = malloc(sizeof(u32) * total_num_triangles_in_bins);
	u32 *p_curr_id = *pp_triangle_ids;

	for(u32 triangle_index = 0; triangle_index < assembled_triangle_count; ++triangle_index) {
		Triangle tri = p_triangles[triangle_index];

		v2i32 min_bounds_in_tiles = { MAX(tri.min_bounds.x / TILE_WIDTH, 0), MAX(tri.min_bounds.y / TILE_HEIGHT, 0) };
		v2i32 max_bounds_in_tiles = { MIN(tri.max_bounds.x / TILE_WIDTH, WIDTH_IN_TILES - 1), MIN(tri.max_bounds.y / TILE_HEIGHT, HEIGHT_IN_TILES - 1) };

		for(i32 y = min_bounds_in_tiles.y; y <= max_bounds_in_tiles.y; ++y) {
			for(i32 x = min_bounds_in_tiles.x; x <= max_bounds_in_tiles.x; ++x) {
				u32 bin_index = y * WIDTH_IN_TILES + x;
				(*pp_triangle_ids)[a_bins[bin_index].num_triangles_upto + current_counts[bin_index]++ ] = triangle_index;
			}
		}
	}

	u32 curr_compacted_bin_index = 0;
	p_compacted_bins = malloc(sizeof(CompactedBin)*num_bins_with_tris);
	memset(p_compacted_bins, 0, sizeof(CompactedBin)*num_bins_with_tris);
	for(u32 bin_index = 0; bin_index < NUM_BINS; ++bin_index) {
		u32 curr_num_tris = a_bins[bin_index].num_triangles_self;
		if(!curr_num_tris) continue;
		p_compacted_bins[curr_compacted_bin_index].num_triangles_self = curr_num_tris;
		p_compacted_bins[curr_compacted_bin_index].num_triangles_upto = a_bins[bin_index].num_triangles_upto;
		p_compacted_bins[curr_compacted_bin_index].bin_index = bin_index;
		curr_compacted_bin_index++;
	}
	
	num_compacted_bins = num_bins_with_tris;
	*p_total_triangle_count = total_num_triangles_in_bins;
	
	rmt_EndCPUSample();
}

void run_rasterizer(u32 max_possible_fragment_count, u32 assembled_triangle_count, const Triangle *p_triangles, const u32 *p_triangle_ids, u32 *p_num_fragments, Fragment **pp_fragments) {
	rmt_BeginCPUSample(rasterizer_stage, 0);
	Viewport viewport = graphics_pipeline.rs.viewport;
	u8 num_attibutes = graphics_pipeline.vs.output_register_count;
	
	*pp_fragments = malloc(max_possible_fragment_count * sizeof(Fragment));

	for(u32 bin_index = 0; bin_index < NUM_BINS; ++bin_index) {
		v2i32 min_bounds = { TILE_WIDTH * (bin_index % WIDTH_IN_TILES), TILE_HEIGHT * (bin_index / WIDTH_IN_TILES) };
		v2i32 max_bounds = v2i32_add_v2i32(min_bounds, (v2i32) { TILE_WIDTH-1, TILE_HEIGHT-1});
		Bin *p_bin = &a_bins[bin_index];
		u32 num_triangles_of_current_bin = p_bin->num_triangles_self;
		u32 total_num_triangles_upto_current_bin = p_bin->num_triangles_upto;
		u32 current_fragment_index = 0;
		for(u32 triangle_index = 0; triangle_index < num_triangles_of_current_bin; ++triangle_index) {
			Triangle* p_tri = p_triangles + *(p_triangle_ids + bin_index * assembled_triangle_count + triangle_index);
			v4f32* p_attributes = p_tri->p_attributes;
			for(i32 y = min_bounds.y; y <= max_bounds.y; ++y) {
				for(i32 x = min_bounds.x; x <= max_bounds.x; ++x) {
					v2i32 frag_coords = { x,y };
					if(is_inside_triangle(&p_tri->setup, frag_coords)) {
						v2f32 barycentric_coords = compute_barycentric_coords(&p_tri->setup);
						v2f32 perspective_barycentric_coords = compute_perspective_barycentric_coords(&p_tri->setup, barycentric_coords);

						Fragment *p_fragment = ((*pp_fragments) + (total_num_triangles_upto_current_bin * TILE_WIDTH * TILE_HEIGHT) + current_fragment_index++);
						p_fragment->coordinates = frag_coords;
						//p_fragment->p_attributes = p_attributes;
						//p_fragment->barycentric_coords = barycentric_coords;
						//p_fragment->perspective_barycentric_coords = perspective_barycentric_coords;
					}
				}
			}
		}
	}

	rmt_EndCPUSample();
}

void run_rasterizer_omp(u32 total_triangle_count_in_bins, const Triangle *p_triangles, const u32 *p_triangle_ids, TileInfo **pp_tile_infos) {
	rmt_BeginCPUSample(rasterizer_stage, 0);
	
	*pp_tile_infos = malloc(total_triangle_count_in_bins * sizeof(TileInfo));

	#pragma omp parallel for schedule(dynamic, 128)
	for(u32 bin_index = 0; bin_index < num_compacted_bins; ++bin_index) {
		CompactedBin bin = p_compacted_bins[bin_index];
		v2i32 min_bounds = { TILE_WIDTH * (bin.bin_index % WIDTH_IN_TILES), TILE_HEIGHT * (bin.bin_index / WIDTH_IN_TILES) };
		v2i32 max_bounds = v2i32_add_v2i32(min_bounds, (v2i32) { TILE_WIDTH - 1, TILE_HEIGHT - 1 });
		
		u32 num_triangles_of_current_bin = bin.num_triangles_self;
		for(u32 triangle_index = 0; triangle_index < num_triangles_of_current_bin; ++triangle_index) {
			u32 triangle_id = p_triangle_ids[bin.num_triangles_upto + triangle_index];
			TileInfo tile_info;
			Triangle tri = p_triangles[triangle_id];
			tile_info.triangle_id = triangle_id;
			u64 fragment_mask = 0;
			for(i32 y = min_bounds.y; y <= max_bounds.y; ++y) {
				for(i32 x = min_bounds.x; x <= max_bounds.x; ++x) {
					i32 alpha =	(tri.setup.a_edge_functions[0].a * x + tri.setup.a_edge_functions[0].b * y) * (1 << NUM_SUB_PIXEL_PRECISION_BITS) + tri.setup.a_edge_functions[0].c;
					i32 beta =	(tri.setup.a_edge_functions[1].a * x + tri.setup.a_edge_functions[1].b * y) * (1 << NUM_SUB_PIXEL_PRECISION_BITS) + tri.setup.a_edge_functions[1].c;
					i32 gamma =	(tri.setup.a_edge_functions[2].a * x + tri.setup.a_edge_functions[2].b * y) * (1 << NUM_SUB_PIXEL_PRECISION_BITS) + tri.setup.a_edge_functions[2].c;
					i32 mask = 0 < (alpha | beta | gamma) ? 1 : 0;
					fragment_mask += (((u64)1) << (((y - min_bounds.y) << 3) + x - min_bounds.x)) * mask;
				}
			}
			tile_info.fragment_mask = fragment_mask;
			(*pp_tile_infos)[bin.num_triangles_upto + triangle_index] = tile_info;
		}
	}

	rmt_EndCPUSample();
}

void run_pixel_shader_stage(const Fragment* p_fragments, u32 num_fragments) {
	//rmt_BeginCPUSample(pixel_shader_stage, 0);
	//u8 num_attibutes = graphics_pipeline.vs.output_register_count;
	//for(u32 bin_index = 0; bin_index < NUM_BINS; ++bin_index) {
	//	Bin bin = a_bins[bin_index];
	//	for(u32 frag_index = 0; frag_index < bin.num_fragments; ++frag_index) {
	//		Fragment *p_fragment = p_fragments + (bin.num_triangles_upto * TILE_WIDTH * TILE_HEIGHT) + frag_index;
	//		v4f32 a_fragment_attributes[PIXEL_SHADER_INPUT_REGISTER_COUNT];
	//		for(i32 attribute_index = 0; attribute_index < num_attibutes; ++attribute_index) {
	//			a_fragment_attributes[attribute_index] = interpolate_attribute(p_fragment->p_attributes + attribute_index, attribute_index ? p_fragment->perspective_barycentric_coords : p_fragment->barycentric_coords, num_attibutes);
	//		}

	//		// Early-Z Test
	//		// ASSUMPTION(Cerlet): Pixel shader does not change the depth of the fragment! 
	//		f32 fragment_z = a_fragment_attributes[0].z;
	//		const fragment_linear_coordinate = p_fragment->coordinates.y*(i32)graphics_pipeline.rs.viewport.width + p_fragment->coordinates.x;
	//		f32 *p_depth = &graphics_pipeline.om.p_depth[fragment_linear_coordinate];
	//		if(*p_depth > fragment_z) {
	//			continue;
	//		}
	//		*p_depth = fragment_z;

	//		// Pixel Shader
	//		v4f32 fragment_out_color;
	//		graphics_pipeline.ps.shader(a_fragment_attributes, (void*)&fragment_out_color, graphics_pipeline.ps.p_shader_resource_views);
	//		// Output Merger
	//		graphics_pipeline.om.p_colors[p_fragment->coordinates.y*(i32)graphics_pipeline.rs.viewport.width + p_fragment->coordinates.x] = encode_color_as_u32(fragment_out_color.xyz);
	//	}
	//}
	//rmt_EndCPUSample();
}

void run_pixel_shader_stage_omp(const TileInfo* p_fragments, const Triangle *p_triangles, const u32 num_triangles) {
	rmt_BeginCPUSample(pixel_shader_stage, 0);
	
	u8 num_attibutes = graphics_pipeline.vs.output_register_count;

	
	#pragma omp parallel for schedule(dynamic)
	for(u32 bin_index = 0; bin_index < NUM_BINS; ++bin_index) {
		Bin bin = a_bins[bin_index];
		u32 a_tile_colors[64];
		f32 a_tile_depths[64];
		memset(&a_tile_colors, 0, sizeof(u32) * 64);
		memset(&a_tile_depths, 0, sizeof(f32) * 64);

		v2i32 min_bounds = { TILE_WIDTH * (bin_index % WIDTH_IN_TILES), TILE_HEIGHT * (bin_index / WIDTH_IN_TILES) };
		for(u32 triangle_index = 0; triangle_index < bin.num_triangles_self; ++triangle_index) {
			TileInfo tile_info = p_fragments[bin.num_triangles_upto + triangle_index];
			if(tile_info.fragment_mask == 0) continue;
			Triangle triangle = p_triangles[tile_info.triangle_id];

			for(u32 fragment_index = 0; fragment_index < 64; ++fragment_index) {
				if(!((((u64)1)<<fragment_index) & tile_info.fragment_mask)) continue;

				i32 x = min_bounds.x + (fragment_index % 8);
				i32 y = min_bounds.y + (fragment_index / 8);
				i32 alpha = (triangle.setup.a_edge_functions[0].a * x + triangle.setup.a_edge_functions[0].b *y) * (1 << NUM_SUB_PIXEL_PRECISION_BITS) + triangle.setup.a_edge_functions[0].c;
				i32 beta = (triangle.setup.a_edge_functions[1].a * x + triangle.setup.a_edge_functions[1].b *y) * (1 << NUM_SUB_PIXEL_PRECISION_BITS) + triangle.setup.a_edge_functions[1].c;
				i32 gamma = (triangle.setup.a_edge_functions[2].a * x + triangle.setup.a_edge_functions[2].b *y) * (1 << NUM_SUB_PIXEL_PRECISION_BITS) + triangle.setup.a_edge_functions[2].c;

				v2f32 barycentric_coords = { (float)(beta >> (NUM_SUB_PIXEL_PRECISION_BITS * 2)) * triangle.setup.one_over_area, (float)(gamma >> (NUM_SUB_PIXEL_PRECISION_BITS * 2)) * triangle.setup.one_over_area };
				v2f32 perspective_barycentric_coords = compute_perspective_barycentric_coords(&triangle.setup, barycentric_coords);
				
				v4f32 a_fragment_attributes[3];
				for(i32 attribute_index = 0; attribute_index < num_attibutes; ++attribute_index) {
					a_fragment_attributes[attribute_index] = interpolate_attribute(triangle.p_attributes + attribute_index, attribute_index ? perspective_barycentric_coords : barycentric_coords, num_attibutes);
				}

				a_fragment_attributes[1].w = barycentric_coords.x;
				a_fragment_attributes[2].x = barycentric_coords.y;

				// Early-Z Test
				// ASSUMPTION(Cerlet): Pixel shader does not change the depth of the fragment! 
				f32 fragment_z = a_fragment_attributes[0].z;
				const fragment_linear_coordinate = y*(i32)graphics_pipeline.rs.viewport.width + x;
				f32 depth = a_tile_depths[fragment_index];
				if(depth > fragment_z) {
					continue;
				}
				a_tile_depths[fragment_index] = fragment_z;

				// Pixel Shader
				v4f32 fragment_out_color;
				//graphics_pipeline.ps.shader(a_fragment_attributes, (void*)&fragment_out_color, graphics_pipeline.ps.p_shader_resource_views);
				{
					struct Ps_Input
					{
						float4 SV_POSITION;
						float3 NORMAL;
						float2 UV;
						float _pad[3];
					} ps_input;

					struct Ps_Output
					{
						float4 SV_TARGET;
					} ps_output;

					typedef struct Ps_Input Ps_Input;
					typedef struct Ps_Output Ps_Output;

					const uint scene_tex_id = 0;
					const uint env_tex_id = 1;

					Ps_Input *p_in = (Ps_Input*)a_fragment_attributes;
					Ps_Output *p_out = (Ps_Output*)(&fragment_out_color);
					Texture2D scene_tex = *((Texture2D*)graphics_pipeline.ps.p_shader_resource_views[scene_tex_id]);
					Texture2D env_tex = *((Texture2D*)graphics_pipeline.ps.p_shader_resource_views[env_tex_id]);

					//float3 normal = v3f32_normalize(p_in->NORMAL);
					//float4 tex_color = sample_2D(scene_tex, p_in->UV);
					//float4 irradiance = sample_2D_latlon(env_tex, normal);

					//p_out->SV_TARGET = v4f32_mul_f32(v4f32_mul_v4f32(irradiance, tex_color), 1.0 / PI * 2);
					float z = p_in->SV_POSITION.z * 100;
					p_out->SV_TARGET = (float4) { z, z, z, 1.0 };
				}

				// Output Merger
				a_tile_colors[fragment_index] = encode_color_as_u32(fragment_out_color.xyz);
			}
		}
		for(int j = 0; j < 8; ++j) {
			for(int i = 0; i < 8; ++i) {
				i32 x = min_bounds.x + i;
				i32 y = min_bounds.y + j;
				const fragment_linear_coordinate = y * (i32)graphics_pipeline.rs.viewport.width + x;
				graphics_pipeline.om.p_colors[fragment_linear_coordinate] = a_tile_colors[j * 8 + i];
				graphics_pipeline.om.p_depth[fragment_linear_coordinate] = a_tile_depths[j * 8 + i];
			}
		}

	}

	rmt_EndCPUSample();
}

void run_pixel_shader_stage_omp_simd(const TileInfo* p_fragments, const Triangle *p_triangles) {
	rmt_BeginCPUSample(pixel_shader_stage, 0);

	u8 num_attibutes = graphics_pipeline.vs.output_register_count;

	#pragma omp parallel for schedule(dynamic,4)
	for(u32 bin_index = 0; bin_index < num_compacted_bins; ++bin_index) {
		CompactedBin bin = p_compacted_bins[bin_index];
		u32 a_tile_colors[64];
		f32 a_tile_depths[64];
		memset(&a_tile_colors, 0, sizeof(u32) * 64);
		memset(&a_tile_depths, 0, sizeof(f32) * 64);

		v2i32 min_bounds = { TILE_WIDTH * (bin.bin_index % WIDTH_IN_TILES), TILE_HEIGHT * (bin.bin_index / WIDTH_IN_TILES) };
		for(u32 triangle_index = 0; triangle_index < bin.num_triangles_self; ++triangle_index) {
			TileInfo tile_info = p_fragments[bin.num_triangles_upto + triangle_index];
			if(tile_info.fragment_mask == 0) continue;
			Triangle triangle = p_triangles[tile_info.triangle_id];

			__m256i fragment_x_index = _mm256_set_epi32(7, 6, 5, 4, 3, 2, 1, 0);
			for(u32 fragment_y_index = 0; fragment_y_index < 8; ++fragment_y_index) {
						
				//if(!((((u64)1) << fragment_index) & tile_info.fragment_mask)) continue;
				u8 mask_8 = (tile_info.fragment_mask >> (8 * fragment_y_index)) & 0xFF;
				//if(mask_8 == 0) continue;
				__m256i mask = _mm256_setr_epi32(
					0xFFFFFFFF * (mask_8 & 1), 0xFFFFFFFF * ((mask_8 >> 1) & 1), 0xFFFFFFFF * ((mask_8 >> 2) & 1), 0xFFFFFFFF * ((mask_8 >> 3) & 1),
					0xFFFFFFFF * ((mask_8 >> 4) & 1), 0xFFFFFFFF * ((mask_8 >> 5) & 1), 0xFFFFFFFF * ((mask_8 >> 6) & 1), 0xFFFFFFFF * ((mask_8 >> 7) & 1)
				);

				//i32 x = min_bounds.x + (fragment_index % 8);
				__m256i x = _mm256_add_epi32(_mm256_set1_epi32(min_bounds.x), fragment_x_index);
				//i32 y = min_bounds.y + (fragment_index / 8);
				__m256i y = _mm256_add_epi32(_mm256_set1_epi32(min_bounds.y), _mm256_set1_epi32(fragment_y_index));

				// ASSUMPTION(Cerlet): 32 bit precision is enough for the fixed point representations of barycentric coordinates
				//i32 alpha = (triangle.setup.a_edge_functions[0].a * x + triangle.setup.a_edge_functions[0].b *y) * (1 << NUM_SUB_PIXEL_PRECISION_BITS) + triangle.setup.a_edge_functions[0].c;
				__m256i alpha = _mm256_slli_epi32(_mm256_mullo_epi32(_mm256_set1_epi32(triangle.setup.a_edge_functions[0].a), x), NUM_SUB_PIXEL_PRECISION_BITS);
				alpha = _mm256_add_epi32(alpha, _mm256_slli_epi32(_mm256_mullo_epi32(_mm256_set1_epi32(triangle.setup.a_edge_functions[0].b), y), NUM_SUB_PIXEL_PRECISION_BITS));
				alpha = _mm256_add_epi32(alpha, _mm256_set1_epi32(triangle.setup.a_edge_functions[0].c));

				//i32 beta = (triangle.setup.a_edge_functions[1].a * x + triangle.setup.a_edge_functions[1].b *y) * (1 << NUM_SUB_PIXEL_PRECISION_BITS) + triangle.setup.a_edge_functions[1].c;
				//f32 barycentric_coords_x = (f32)(beta >> (NUM_SUB_PIXEL_PRECISION_BITS * 2));
				__m256i beta = _mm256_slli_epi32(_mm256_mullo_epi32(_mm256_set1_epi32(triangle.setup.a_edge_functions[1].a), x), NUM_SUB_PIXEL_PRECISION_BITS);
				beta = _mm256_add_epi32(beta, _mm256_slli_epi32(_mm256_mullo_epi32(_mm256_set1_epi32(triangle.setup.a_edge_functions[1].b), y), NUM_SUB_PIXEL_PRECISION_BITS));
				beta = _mm256_add_epi32(beta, _mm256_set1_epi32(triangle.setup.a_edge_functions[1].c));
				beta = _mm256_srai_epi32(beta, NUM_SUB_PIXEL_PRECISION_BITS * 2);
				__m256 barycentric_coords_x = _mm256_mul_ps(_mm256_cvtepi32_ps(beta), _mm256_set1_ps(triangle.setup.one_over_area));
			
				//i32 gamma = (triangle.setup.a_edge_functions[2].a * x + triangle.setup.a_edge_functions[2].b *y) * (1 << NUM_SUB_PIXEL_PRECISION_BITS) + triangle.setup.a_edge_functions[2].c;
				//f32 barycentric_coords_y = (float)(gamma >> (NUM_SUB_PIXEL_PRECISION_BITS * 2)) * triangle.setup.one_over_area;
				__m256i gamma = _mm256_slli_epi32(_mm256_mullo_epi32(_mm256_set1_epi32(triangle.setup.a_edge_functions[2].a), x), NUM_SUB_PIXEL_PRECISION_BITS);
				gamma = _mm256_add_epi32(gamma, _mm256_slli_epi32(_mm256_mullo_epi32(_mm256_set1_epi32(triangle.setup.a_edge_functions[2].b), y), NUM_SUB_PIXEL_PRECISION_BITS));
				gamma = _mm256_add_epi32(gamma, _mm256_set1_epi32(triangle.setup.a_edge_functions[2].c));
				//gamma = _mm256_srli_epi32(gamma, NUM_SUB_PIXEL_PRECISION_BITS * 2);
				gamma = _mm256_srai_epi32(gamma, NUM_SUB_PIXEL_PRECISION_BITS * 2);
				__m256 barycentric_coords_y = _mm256_mul_ps(_mm256_cvtepi32_ps(gamma), _mm256_set1_ps(triangle.setup.one_over_area));

				// f32 denom = (1.0 - u_bary - v_bary) * p_setup->a_reciprocal_ws[0] + u_bary * p_setup->a_reciprocal_ws[1] + v_bary * p_setup->a_reciprocal_ws[2];
				// denom = 1.0 / denom;
				__m256 denom = _mm256_sub_ps(_mm256_set1_ps(1.0), _mm256_add_ps(barycentric_coords_x, barycentric_coords_y));
				denom = _mm256_mul_ps(denom, _mm256_set1_ps(triangle.setup.a_reciprocal_ws[0]));
				denom = _mm256_add_ps(denom, _mm256_mul_ps(barycentric_coords_x, _mm256_set1_ps(triangle.setup.a_reciprocal_ws[1])));
				denom = _mm256_add_ps(denom, _mm256_mul_ps(barycentric_coords_y, _mm256_set1_ps(triangle.setup.a_reciprocal_ws[2])));
				denom = _mm256_div_ps(_mm256_set1_ps(1.0), denom);
				
				// f32 perspective_barycentric_coords.x = barycentric_coords_x * p_setup->a_reciprocal_ws[1] * denom;
				__m256 perspective_barycentric_coords_x = _mm256_mul_ps(_mm256_mul_ps(barycentric_coords_x, _mm256_set1_ps(triangle.setup.a_reciprocal_ws[1])), denom);
				// f32 perspective_barycentric_coords.y = barycentric_coords_y * p_setup->a_reciprocal_ws[2] * denom;
				__m256 perspective_barycentric_coords_y = _mm256_mul_ps(_mm256_mul_ps(barycentric_coords_y, _mm256_set1_ps(triangle.setup.a_reciprocal_ws[2])), denom);

				//v4f32 a_fragment_attributes[3];
				//for(i32 attribute_index = 0; attribute_index < num_attibutes; ++attribute_index) {
				//	a_fragment_attributes[attribute_index] = interpolate_attribute(triangle.p_attributes + attribute_index, attribute_index ? perspective_barycentric_coords : barycentric_coords, num_attibutes);
				//}

				__m256 a_fragment_attributes[12];
				for(i32 attribute_index = 0; attribute_index < num_attibutes; ++attribute_index) {
					if(attribute_index > 0) {
						barycentric_coords_x = perspective_barycentric_coords_x;
						barycentric_coords_y = perspective_barycentric_coords_y;
					}

					v4f32 v0_attribute = triangle.p_attributes[attribute_index];
					v4f32 v1_attribute = triangle.p_attributes[attribute_index + 3];
					v4f32 v2_attribute = triangle.p_attributes[attribute_index + 6]; 

					__m256 v0_attribute_x = _mm256_set1_ps(v0_attribute.x);
					__m256 v1_attribute_x = _mm256_set1_ps(v1_attribute.x);
					__m256 v2_attribute_x = _mm256_set1_ps(v2_attribute.x);
					__m256 temp_x = _mm256_add_ps(v0_attribute_x, _mm256_mul_ps(_mm256_sub_ps(v1_attribute_x, v0_attribute_x), barycentric_coords_x));
					temp_x = _mm256_add_ps(temp_x, _mm256_mul_ps(_mm256_sub_ps(v2_attribute_x, v0_attribute_x), barycentric_coords_y));
					a_fragment_attributes[attribute_index * 4 + 0] = temp_x;

					__m256 v0_attribute_y = _mm256_set1_ps(v0_attribute.y);
					__m256 v1_attribute_y = _mm256_set1_ps(v1_attribute.y);
					__m256 v2_attribute_y = _mm256_set1_ps(v2_attribute.y);
					__m256 temp_y = _mm256_add_ps(v0_attribute_y, _mm256_mul_ps(_mm256_sub_ps(v1_attribute_y, v0_attribute_y), barycentric_coords_x));
					temp_y = _mm256_add_ps(temp_y, _mm256_mul_ps(_mm256_sub_ps(v2_attribute_y, v0_attribute_y), barycentric_coords_y));
					a_fragment_attributes[attribute_index * 4 + 1] = temp_y;

					__m256 v0_attribute_z = _mm256_set1_ps(v0_attribute.z);
					__m256 v1_attribute_z = _mm256_set1_ps(v1_attribute.z);
					__m256 v2_attribute_z = _mm256_set1_ps(v2_attribute.z);
					__m256 temp_z = _mm256_add_ps(v0_attribute_z, _mm256_mul_ps(_mm256_sub_ps(v1_attribute_z, v0_attribute_z), barycentric_coords_x));
					temp_z = _mm256_add_ps(temp_z, _mm256_mul_ps(_mm256_sub_ps(v2_attribute_z, v0_attribute_z), barycentric_coords_y));
					a_fragment_attributes[attribute_index * 4 + 2] = temp_z;

					__m256 v0_attribute_w = _mm256_set1_ps(v0_attribute.w);
					__m256 v1_attribute_w = _mm256_set1_ps(v1_attribute.w);
					__m256 v2_attribute_w = _mm256_set1_ps(v2_attribute.w);
					__m256 temp_w = _mm256_add_ps(v0_attribute_w, _mm256_mul_ps(_mm256_sub_ps(v1_attribute_w, v0_attribute_w), barycentric_coords_x));
					temp_w = _mm256_add_ps(temp_w, _mm256_mul_ps(_mm256_sub_ps(v2_attribute_w, v0_attribute_w), barycentric_coords_y));
					a_fragment_attributes[attribute_index * 4 + 3] = temp_w;

					//a_fragment_attributes[attribute_index * 4 + 0] = temp_x;
					//a_fragment_attributes[attribute_index * 4 + 1] = temp_y;
					//a_fragment_attributes[attribute_index * 4 + 2] = temp_z;
					//a_fragment_attributes[attribute_index * 4 + 3] = temp_w;
				}

				// Early-Z Test
				// ASSUMPTION(Cerlet): Pixel shader does not change the depth of the fragment! 
				__m256 fragment_z = a_fragment_attributes[2];
				__m256 depth = _mm256_load_ps(a_tile_depths + fragment_y_index*8);
				__m256 depth_test = _mm256_cmp_ps(fragment_z, depth, _CMP_GE_OQ);
				mask = _mm256_and_si256(_mm256_castps_si256(depth_test), mask);
				if(_mm256_testz_si256(mask, mask) == 1) continue;

				// Pixel Shader
				__m256 fragment_out_color[4];
				graphics_pipeline.ps.shader(a_fragment_attributes, (void*)&fragment_out_color, graphics_pipeline.ps.p_shader_resource_views, mask);

				// Output Merger
				// (((u32)(color.x*255.f)) << 16) + (((u32)(color.y*255.f)) << 8) + (((u32)(color.z*255.f)));
				__m256i encoded_color = _mm256_slli_epi32(_mm256_cvtps_epi32(_mm256_mul_ps(fragment_out_color[0], _mm256_set1_ps(255.0))), 16); // r
				encoded_color = _mm256_add_epi32(encoded_color, _mm256_slli_epi32(_mm256_cvtps_epi32(_mm256_mul_ps(fragment_out_color[1], _mm256_set1_ps(255.0))), 8)); // r+g
				encoded_color = _mm256_add_epi32(encoded_color, _mm256_cvtps_epi32(_mm256_mul_ps(fragment_out_color[2], _mm256_set1_ps(255.0)))); // r+g+b

				_mm256_maskstore_epi32(a_tile_colors + fragment_y_index * 8, mask, encoded_color);
				_mm256_maskstore_ps(a_tile_depths + fragment_y_index * 8, mask, fragment_z);
			}
		}

		for(int j = 0; j < 8; ++j) {
			for(int i = 0; i < 8; ++i) {
				i32 x = min_bounds.x + i;
				i32 y = min_bounds.y + j;
				const fragment_linear_coordinate = y * (i32)graphics_pipeline.rs.viewport.width + x;
				graphics_pipeline.om.p_colors[fragment_linear_coordinate] = a_tile_colors[j * 8 + i];
				graphics_pipeline.om.p_depth[fragment_linear_coordinate] = a_tile_depths[j * 8 + i];
			}
		}

	}

	rmt_EndCPUSample();
}

void draw_indexed(UINT index_count /* TODO(cerlet): Use UINT start_index_location, int base_vertex_location*/) {
	rmt_BeginCPUSample(draw_indexed, 0);

	void *p_vertex_input_data = NULL;
	run_input_assembler_stage_omp_simd(index_count, &p_vertex_input_data);

	u32 per_vertex_output_data_size = 0;
	void *p_vertex_output_data = NULL;
	run_vertex_shader_stage_omp_simd(index_count, p_vertex_input_data, &per_vertex_output_data_size, &p_vertex_output_data);

	assert((index_count % 3) == 0);
	u32 triangle_count = index_count / 3;

	Triangle *p_triangles = NULL;
	v4f32 *p_attributes = NULL;
	u32 assembled_triangle_count = 0;
	run_primitive_assembly_stage(triangle_count, p_vertex_output_data, &assembled_triangle_count, &p_triangles, &p_attributes);
	
	u32 *p_triangle_ids = NULL;
	u32 total_triangle_count_in_bins = 0;
	run_binner(assembled_triangle_count, p_triangles, &p_triangle_ids, &total_triangle_count_in_bins);

	TileInfo *p_tile_infos = NULL;
	run_rasterizer_omp(total_triangle_count_in_bins, p_triangles, p_triangle_ids, &p_tile_infos);

	run_pixel_shader_stage_omp_simd(p_tile_infos, p_triangles);

	free(p_vertex_input_data);
	free(p_vertex_output_data);
	free(p_attributes);
	free(p_triangles);
	free(p_triangle_ids);
	free(p_tile_infos);
	free(p_compacted_bins);
	rmt_EndCPUSample();
}

void clear_render_target_view(const f32 *p_clear_color) {
	rmt_BeginCPUSample(clear_render_target_view, 0);
	v3f32 clear_color = { p_clear_color[0],p_clear_color[1] ,p_clear_color[2] };
	u32 encoded_clear = encode_color_as_u32(clear_color);
	
	u32 frame_buffer_texel_count = sizeof(frame_buffer) / sizeof(u32);
	u32 *p_texel = frame_buffer;
	while(frame_buffer_texel_count--) {
		*p_texel++ = encoded_clear;
	}
	rmt_EndCPUSample();
}

void clear_depth_stencil_view(const f32 depth) {
	rmt_BeginCPUSample(clear_depth_stencil_view, 0);
	f32 *p_depth = depth_buffer;
	u32 depth_buffer_texel_count = sizeof(depth_buffer) / sizeof(f32);
	while(depth_buffer_texel_count--) {
		*p_depth++ = depth;
	}
	rmt_EndCPUSample();
}

void render() {
	rmt_BeginCPUSample(render, 0);
	//graphics_pipeline.ia.input_layout = transform_vs.in_vertex_size;
	graphics_pipeline.ia.input_layout = transform_vs_simd.in_vertex_size / VECTOR_WIDTH;
	
	graphics_pipeline.ia.primitive_topology = PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	graphics_pipeline.ia.p_index_buffer = test_mesh.p_index_buffer;
	graphics_pipeline.ia.p_vertex_buffer = test_mesh.p_vertex_buffer;

	//graphics_pipeline.vs.output_register_count = transform_vs.out_vertex_size / sizeof(v4f32);
	graphics_pipeline.vs.output_register_count = transform_vs_simd.out_vertex_size / (sizeof(v4f32)*VECTOR_WIDTH);
	//graphics_pipeline.vs.shader = transform_vs.vs_main;
	graphics_pipeline.vs.shader = transform_vs_simd.vs_main;
	
	graphics_pipeline.vs.p_shader_resource_views[0] = &env_tex;
	graphics_pipeline.vs.p_constant_buffers[0] = &per_frame_cb;

	Viewport viewport = { 0.f,0.f,(f32)frame_width,(f32)frame_height,0.f,1.f };
	graphics_pipeline.rs.viewport = viewport;

	graphics_pipeline.ps.shader = passthrough_ps.ps_main;
	graphics_pipeline.ps.p_shader_resource_views[0] = &scene_tex;

	graphics_pipeline.om.p_colors = &frame_buffer[0][0];
	graphics_pipeline.om.p_depth = &depth_buffer[0][0];
	const f32 clear_color[4] = { (f32)227/255, (f32)223/255, (f32)216/255, 0.f };
	clear_render_target_view(clear_color); // TODO(cerlet): Implement clearing via render target view pointer!
	clear_depth_stencil_view(0.0);
	draw_indexed(test_mesh.header.index_count);
	rmt_EndCPUSample();
}

void present(HWND h_window) {
	rmt_BeginCPUSample(present, 0);
	InvalidateRect(h_window, NULL, FALSE);
	UpdateWindow(h_window);
	rmt_EndCPUSample();
}

void init() {
	{ // Load mesh

		void *p_data = NULL;
		OCTARINE_MESH_RESULT result = octarine_mesh_read_from_file("../assets/suzanne.octrn", &test_mesh.header, &p_data);
		//OCTARINE_MESH_RESULT result = octarine_mesh_read_from_file("../assets/sphere_x4.octrn", &test_mesh.header, &p_data);
		if(result != OCTARINE_MESH_OK) { assert(false); };

		u32 vertex_size = sizeof(float) * 8;
		u32 vertex_buffer_size = test_mesh.header.vertex_count * vertex_size;
		u32 index_buffer_size = test_mesh.header.index_count * sizeof(u32);

		test_mesh.p_vertex_buffer = p_data;
		test_mesh.p_index_buffer = (u32*)(((uint8_t*)p_data) + vertex_buffer_size);
	}

	{ // Load scene texture

		OctarineImageHeader header;
		//OCTARINE_IMAGE result = octarine_image_read_from_file("../assets/malevich_scene_colors.octrn", &header, &scene_tex.p_data);
		OCTARINE_IMAGE result = octarine_image_read_from_file("../assets/ninomaru_teien_panorama_irradiance.octrn", &header, &scene_tex.p_data);
		if(result != OCTARINE_IMAGE_OK) { assert(false); };

		scene_tex.width = header.width;
		scene_tex.height = header.height;

	}

	{ // Load texture

		OctarineImageHeader header;
		OCTARINE_IMAGE result = octarine_image_read_from_file("../assets/ninomaru_teien_panorama_irradiance.octrn", &header, &env_tex.p_data);
		//OCTARINE_IMAGE result = octarine_image_read_from_file("../assets/panorama_test.octrn", &header, &env_tex.p_data);
		if(result != OCTARINE_IMAGE_OK) { assert(false); };

		env_tex.width = header.width;
		env_tex.height = header.height;

	}

	{ // Init Camera
#if 1
		camera.pos = (v3f32){ 3.5f, 1.0f, 1.0f};
		camera.yaw_rad = TO_RADIANS(0.0);
		camera.pitch_rad = TO_RADIANS(0.0);
#else
	  camera.pos = (v3f32){ 0.395f, 0.216f, 0.025f};
	  camera.yaw_rad = -2.0;
	  camera.pitch_rad = -0.4;
#endif
		camera.fov_y_angle_deg = 90.f;
		camera.near_plane = 0.01;
		camera.far_plane = 100.01;

		// clip from view transformation, view space : y - up, left-handed
		float fov_y_angle_rad = TO_RADIANS(camera.fov_y_angle_deg);
		float aspect_ratio = (float)WIDTH / HEIGHT;
		float scale_y = (float)(1.0 / tan(fov_y_angle_rad / 2.0));
		float scale_x = scale_y / aspect_ratio;
		m4x4f32 clip_from_view = { // left-handed reversed-z infinite projection
			scale_x, 0.0, 0.0, 0.0,
			0.0, scale_y, 0.0, 0.0,
			0.0, 0.0, 0.0, camera.near_plane,
			0.0, 0.0, 1.0, 0.0
		};

		camera.clip_from_view = clip_from_view;

		float cos_pitch = cos(camera.pitch_rad);
		float sin_pitch = sin(camera.pitch_rad);
		m4x4f32 rotation_pitch = { // pitch axis is x in view space
			1.0, 0.0, 0.0, 0.0,
			0.0, cos_pitch, sin_pitch, 0.0,
			0.0,-sin_pitch, cos_pitch, 0.0,
			0.0, 0.0, 0.0, 1.0
		};

		float cos_yaw = cos(camera.yaw_rad);
		float sin_yaw = sin(camera.yaw_rad);
		m4x4f32 rotation_yaw = { // yaw axis is y in view space
			cos_yaw, 0.0, -sin_yaw, 0.0,
			0.0, 1.0, 0.0, 0.0,
			sin_yaw, 0.0, cos_yaw, 0.0,
			0.0, 0.0, 0.0, 1.0
		};

		// View Space Left-handed +y : up, +x: right  -> World Space Right-handed +z : up, -y: right 
		static const m4x4f32 change_of_basis = {
			0.0, 0.0, -1.0, 0,
			1.0, 0.0, 0.0, 0,
			0.0, 1.0, 0.0, 0,
			0.0, 0.0, 0.0, 1.0
		};

		m4x4f32 world_from_view = m4x4f32_mul_m4x4f32( &rotation_yaw, &rotation_pitch);
		world_from_view = m4x4f32_mul_m4x4f32(&change_of_basis, &world_from_view);
		world_from_view.m03 = camera.pos.x;
		world_from_view.m13 = camera.pos.y;
		world_from_view.m23 = camera.pos.z;
		camera.view_from_world = m4x4f32_inverse(&world_from_view);
	
		per_frame_cb.clip_from_world = m4x4f32_mul_m4x4f32(&camera.clip_from_view, &camera.view_from_world);
	}
}

void update() {
	rmt_BeginCPUSample(update, 0);

	f32 delta_pitch_rad, delta_yaw_rad;
	static f32 mouse_pos_scale = 0.0025f;
	if(input.is_right_mouse_button_pressed) {
		delta_pitch_rad = (input.mouse_pos.y - input.last_mouse_pos.y) * mouse_pos_scale;
		delta_yaw_rad = (input.mouse_pos.x - input.last_mouse_pos.x) * mouse_pos_scale;
	}
	else {
		delta_pitch_rad = 0.0;
		delta_yaw_rad = 0.0;
	}
	input.last_mouse_pos = input.mouse_pos;

	float yaw_rad = camera.yaw_rad;
	yaw_rad += delta_yaw_rad;
	if(yaw_rad > PI) yaw_rad -= TAU;
	else if(yaw_rad <= -PI) yaw_rad += TAU;
	camera.yaw_rad = yaw_rad;

	float pitch_rad = camera.pitch_rad;
	pitch_rad += delta_pitch_rad;
	pitch_rad = min(PI_OVER_TWO, pitch_rad);
	pitch_rad = max(-PI_OVER_TWO, pitch_rad);
	camera.pitch_rad = pitch_rad;

	static float move_speed_mps = 10.0f;
	static float speed_scale = .06f;
	static float delta_time_s = 0.016666f;

	float forward = move_speed_mps * speed_scale * ((input.is_w_pressed ? delta_time_s : 0.0f) + (input.is_s_pressed ? -delta_time_s : 0.0f));
	float strafe = move_speed_mps * speed_scale * ((input.is_d_pressed ? delta_time_s : 0.0f) + (input.is_a_pressed ? -delta_time_s : 0.0f));
	float ascent = move_speed_mps * speed_scale * ((input.is_e_pressed ? delta_time_s : 0.0f) + (input.is_q_pressed ? -delta_time_s : 0.0f));
	
	float cos_pitch = cos(-camera.pitch_rad);
	float sin_pitch = sin(-camera.pitch_rad);
	m4x4f32 rotation_pitch = { // pitch axis is x in view space
		1.0, 0.0, 0.0, 0.0,
		0.0, cos_pitch, sin_pitch, 0.0,
		0.0,-sin_pitch, cos_pitch, 0.0,
		0.0, 0.0, 0.0, 1.0
	};

	float cos_yaw = cos(-camera.yaw_rad);
	float sin_yaw = sin(-camera.yaw_rad);
	m4x4f32 rotation_yaw = { // yaw axis is y in view space
		cos_yaw, 0.0, -sin_yaw, 0.0,
		0.0, 1.0, 0.0, 0.0,
		sin_yaw, 0.0, cos_yaw, 0.0,
		0.0, 0.0, 0.0, 1.0
	};

	// Left-handed +y : up, +x: right View Space -> Right-handed +z : up, -y: right World Space
	static const m4x4f32 change_of_basis = {
		0.0, 0.0,-1.0, 0,
		1.0, 0.0, 0.0, 0,
		0.0, 1.0, 0.0, 0,
		0.0, 0.0, 0.0, 1.0
	};

	m4x4f32 world_from_view = m4x4f32_mul_m4x4f32(&rotation_yaw, &rotation_pitch);
	world_from_view = m4x4f32_mul_m4x4f32(&change_of_basis, &world_from_view);
	
	v3f32 movement_vs = { strafe, ascent, forward };
	movement_vs = m4x4f32_mul_v4f32(&world_from_view, v4f32_from_v3f32(movement_vs, 1.0f)).xyz;
	camera.pos = v3f32_add_v3f32(camera.pos, movement_vs);
	
	world_from_view.m03 = camera.pos.x;
	world_from_view.m13 = camera.pos.y;
	world_from_view.m23 = camera.pos.z;
	camera.view_from_world = m4x4f32_inverse(&world_from_view);

	per_frame_cb.clip_from_world = m4x4f32_mul_m4x4f32(&camera.clip_from_view, &camera.view_from_world);
	
	//char buf[1024];
	//sprintf(buf,"pos: %f, %f, %f --  yaw: %f -- pitch: %f \n", camera.pos.x, camera.pos.y, camera.pos.z, camera.yaw_rad, camera.pitch_rad);
	//OutputDebugString(buf);

	rmt_EndCPUSample();
}

int CALLBACK WinMain(
	HINSTANCE hInstance,
	HINSTANCE hPrevInstance,
	LPSTR     lpCmdLine,
	int       nCmdShow
) {
	// Remotery
	Remotery* p_remotery;
	rmt_CreateGlobalInstance(&p_remotery);

	const char *p_window_class_name = "Malevich Window Class";
	const char *p_window_name = "Malevich";

	HWND h_window = NULL;
	{
		WNDCLASSEX window_class = { 0 };
		window_class.cbSize = sizeof(WNDCLASSEX);
		window_class.style = CS_HREDRAW | CS_VREDRAW;
		window_class.lpfnWndProc = window_proc;
		window_class.hInstance = hInstance;
		window_class.hIcon = (HICON)(LoadImage(NULL,"suprematism.ico", IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE));
		window_class.hCursor = LoadCursor(NULL, IDC_ARROW);
		window_class.hbrBackground = CreateSolidBrush(RGB(227, 223, 216));// (HBRUSH)(COLOR_WINDOW + 1);
		window_class.lpszClassName = p_window_class_name;

		ATOM result = RegisterClassExA(&window_class);
		if(!result) { error_win32("RegisterClassExA", GetLastError()); return -1; };

		RECT window_rect = { 0, 0, frame_width*STRECTH_FACTOR, frame_height*STRECTH_FACTOR };
		AdjustWindowRect(&window_rect, WS_OVERLAPPEDWINDOW, FALSE);

		h_window = CreateWindowExA(
			0, p_window_class_name, p_window_name,
			WS_OVERLAPPEDWINDOW | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT,
			window_rect.right - window_rect.left,
			window_rect.bottom - window_rect.top,
			NULL, NULL, hInstance, NULL);
		if(!h_window) { error_win32("CreateWindowExA", GetLastError()); return -1; };

		ShowWindow(h_window, nCmdShow);
		UpdateWindow(h_window);
	}

	init();

	MSG msg = { 0 };
	while(msg.message != WM_QUIT) {
		if(PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessageA(&msg);
		}
		else {
			rmt_BeginCPUSample(Malevich, 0);
			update();
			render();
			present(h_window);
			rmt_EndCPUSample();
		}
	}

	rmt_DestroyGlobalInstance(p_remotery);

	return 0;
}