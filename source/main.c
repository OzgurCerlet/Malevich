#define _CRT_SECURE_NO_WARNINGS

#define LEAN_AND_MEAN
#include <windows.h>

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

#define WIDTH 512 //820;
#define HEIGHT 512 //1000;

const int frame_width = WIDTH;
const int frame_height = HEIGHT;

u32 frame_buffer[WIDTH][HEIGHT];
f32 depth_buffer[WIDTH][HEIGHT];

#define NUM_SUB_PIXEL_PRECISION_BITS 4
#define PIXEL_SHADER_INPUT_REGISTER_COUNT 32
#define COMMONSHADER_CONSTANT_BUFFER_HW_SLOT_COUNT 16
#define COMMONSHADER_INPUT_RESOURCE_REGISTER_COUNT 128

extern VertexShader transform_vs;
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
	void (*shader)(const void *p_vertex_input_data, void *p_vertex_output_data, const void *p_constant_buffers, u32 vertex_id);
	u8 output_register_count;
	void *p_constant_buffers[COMMONSHADER_CONSTANT_BUFFER_HW_SLOT_COUNT];
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

typedef struct EdgeFunction{
	i32 a;
	i32 b;
	i32 c;
} EdgeFunction;

typedef struct Setup {
	EdgeFunction a_edge_functions[3];
	i32 a_signed_distances[3];
	f32 a_reciprocal_ws[3];
	f32 one_over_area;
}Setup;

typedef struct Triangle {
	v4f32 *p_attributes;
	v2i32 min_bounds;
	v2i32 max_bounds;
	Setup setup;
} Triangle;

typedef struct Fragment {
	v4f32 *p_attributes;
	v2i32 coordinates;
	v2f32 barycentric_coords;
	v2f32 perspective_barycentric_coords;
} Fragment;

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
Texture2D test_tex;
PerFrameCB per_frame_cb;
Camera camera;
Input input;

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
			StretchDIBits(h_device_context, 0, 0, frame_width, frame_height, 0, 0, frame_width, frame_height, frame_buffer, &info, DIB_RGB_COLORS, SRCCOPY);
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

inline bool is_inside_edge(Setup *p_setup, u32 edge_index, v2i32 frag_coord) {
	i32 a = p_setup->a_edge_functions[edge_index].a;
	i32 b = p_setup->a_edge_functions[edge_index].b;
	i32 c = p_setup->a_edge_functions[edge_index].c;
	i32 signed_distance = ((a * frag_coord.x) << NUM_SUB_PIXEL_PRECISION_BITS) + ((b * frag_coord.y) << NUM_SUB_PIXEL_PRECISION_BITS) + c;
	p_setup->a_signed_distances[edge_index] = signed_distance;
	if(signed_distance > 0) return true;
	if(signed_distance < 0) return false;
	if(a > 0) return true;
	if(a < 0) return false;
	if(b > 0) return true;
	return false;
}

inline bool is_inside_triangle(Setup *p_setup, v2i32 frag_coord) {
	bool w0 = is_inside_edge(p_setup, 0, frag_coord);
	bool w1 = is_inside_edge(p_setup, 1, frag_coord);
	bool w2 = is_inside_edge(p_setup, 2, frag_coord);
	return w0 && w1 && w2;
}

// In this context, barycentric coords (u,v,w) = areal coordinates (A1/A,A2/A,A0/A) => u+v+w = 1;
inline v2f32 compute_barycentric_coords(const Setup *p_setup) {
	v2f32 barycentric_coords;
	barycentric_coords.x = (float)(p_setup->a_signed_distances[1] >> (NUM_SUB_PIXEL_PRECISION_BITS * 2)) * p_setup->one_over_area;
	barycentric_coords.y = (float)(p_setup->a_signed_distances[2] >> (NUM_SUB_PIXEL_PRECISION_BITS * 2)) * p_setup->one_over_area;

	return barycentric_coords;
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

inline v4f32 interpolate_attribute(const v4f32 *p_attributes, v2f32 barycentric_coords, u8 num_attributes_per_vertex) {
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

void run_input_assembler_stage(u32 index_count, void **pp_vertex_input_data) {
	rmt_BeginCPUSample(input_assambler_stage, 0);
	// Input Assembler
	if(graphics_pipeline.ia.primitive_topology != PRIMITIVE_TOPOLOGY_TRIANGLELIST) {
		DebugBreak();
	}

	// ASSUMPTION(cerlet): In Direct3D, index buffers are bounds checked!, we assume our index buffers are properly bounded.
	// TODO(cerlet): Implement some kind of post-transform vertex cache.
	u32 vertex_count = index_count;
	u32 per_vertex_input_data_size = graphics_pipeline.ia.input_layout;
	void *p_vertex_input_data = malloc(vertex_count*per_vertex_input_data_size);
	u8 *p_vertex = p_vertex_input_data;
	for(u32 index_index = 0; index_index < index_count; ++index_index) {
		u32 vertex_index = graphics_pipeline.ia.p_index_buffer[index_index];
		u32 vertex_offset = vertex_index * per_vertex_input_data_size;
		memcpy(p_vertex, ((u8*)graphics_pipeline.ia.p_vertex_buffer) + vertex_offset, per_vertex_input_data_size);
		p_vertex += per_vertex_input_data_size;
	}
	*pp_vertex_input_data = p_vertex_input_data;
	rmt_EndCPUSample();
}

void run_vertex_shader_stage(u32 vertex_count, const void * p_vertex_input_data, u32 *p_per_vertex_output_data_size, void **pp_vertex_output_data) {
	rmt_BeginCPUSample(vertex_shader_stage, 0);
	// Vertex Shader
	u32 per_vertex_output_data_size = graphics_pipeline.vs.output_register_count * sizeof(v4f32);
	void **p_constant_buffers = graphics_pipeline.vs.p_constant_buffers;
	void *p_vertex_output_data = malloc(vertex_count*per_vertex_output_data_size);
	for(u32 vertex_id = 0; vertex_id < vertex_count; ++vertex_id) {
		graphics_pipeline.vs.shader(p_vertex_input_data, p_vertex_output_data, p_constant_buffers, vertex_id);
	}
	*p_per_vertex_output_data_size = per_vertex_output_data_size;
	*pp_vertex_output_data = p_vertex_output_data;
	rmt_EndCPUSample();
}

#define  MAX_NUM_CLIP_VERTICES 16
typedef struct Vertex {
	v4f32 a_attributes[PIXEL_SHADER_INPUT_REGISTER_COUNT];
}Vertex;

void clip_by_plane(Vertex *p_clipped_vertices, v4f32 plane_normal, f32 plane_d, u32 *p_num_vertices ) {

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
			a_result_vertices[num_out_vertices++] = (p_clipped_vertices)[i];
		}

		float next_dot = v4f32_dot(plane_normal, (p_clipped_vertices)[next].a_attributes[0]);
		bool is_next_inside = next_dot > -plane_d;
		if(is_current_inside != is_next_inside) {
			assert(num_generated_clipped_vertices < MAX_NUM_CLIP_VERTICES);
			f32 t = (plane_d + current_dot) / (current_dot - next_dot);
			for(u32 attribute_index = 0; attribute_index < num_attributes; ++attribute_index) {
				a_result_vertices[num_out_vertices++].a_attributes[attribute_index] = v4f32_add_v4f32(
					v4f32_mul_f32((p_clipped_vertices)[i].a_attributes[attribute_index], (1.f - t)),
					v4f32_mul_f32((p_clipped_vertices)[next].a_attributes[attribute_index], t));
			}
		}

		current_dot = next_dot;
		is_current_inside = is_next_inside;
	}

	*p_num_vertices = num_out_vertices;
	v4f32 *p_resultPointers[MAX_NUM_CLIP_VERTICES];
	memcpy(p_clipped_vertices, a_result_vertices, sizeof(Vertex)*num_out_vertices);
}

void run_clipper(Vertex *p_clipped_vertices, const void *p_vertex_output_data, u32 in_triangle_index, u32 *p_num_clipped_vertices) {
	u32 num_attributes = graphics_pipeline.vs.output_register_count;
	u32 vertex_size = num_attributes * sizeof(v4f32);
	
	// initialize clipped vertices array with original vertex data
	memcpy(p_clipped_vertices, ((v4f32*)p_vertex_output_data) + in_triangle_index * num_attributes, vertex_size);
	memcpy(p_clipped_vertices + 1, ((v4f32*)p_vertex_output_data) + in_triangle_index * num_attributes + num_attributes, vertex_size);
	memcpy(p_clipped_vertices + 2, ((v4f32*)p_vertex_output_data) + in_triangle_index * num_attributes + num_attributes * 2, vertex_size);
	*p_num_clipped_vertices = 3;

	//clip_by_plane(p_clipped_vertices, v4f32_normalize((v4f32) { 1, 0, 0, 1 }), 0, p_num_clipped_vertices);	// -w <= x <==> 0 <= x + w
	//clip_by_plane(p_clipped_vertices, v4f32_normalize((v4f32) {-1, 0, 0, 1 }), 0, p_num_clipped_vertices);	//  x <= w <==> 0 <= w - x
	//clip_by_plane(p_clipped_vertices, v4f32_normalize((v4f32) { 0, 1, 0, 1 }), 0, p_num_clipped_vertices);	// -w <= y <==> 0 <= y + w
	//clip_by_plane(p_clipped_vertices, v4f32_normalize((v4f32) { 0,-1, 0, 1 }), 0, p_num_clipped_vertices);	//  y <= w <==> 0 <= w - y
	//clip_by_plane(p_clipped_vertices, v4f32_normalize((v4f32) { 0, 0, 1, 1 }), 0, p_num_clipped_vertices);	// -w <= z <==> 0 <= z + w
	//clip_by_plane(p_clipped_vertices, v4f32_normalize((v4f32) { 0, 0,-1, 1 }), 0, p_num_clipped_vertices);	//  z <= w <==> 0 <= w - z
}

void run_primitive_assembly_stage(u32 in_triangle_count, void* p_vertex_output_data, u32 *p_assembled_triangle_count, u32 *p_max_possible_fragment_count, Triangle **pp_triangles, v4f32 **pp_attributes) {
	rmt_BeginCPUSample(primitive_assembly_stage, 0);
	// Primitive Assembly
	const u32 max_clipper_generated_triangle_count = in_triangle_count / 10;
	const u32 out_triangle_count = in_triangle_count + max_clipper_generated_triangle_count;
	const u32 num_attributes = graphics_pipeline.vs.output_register_count;
	const u32 per_vertex_offset = num_attributes * sizeof(v4f32);
	const u32 triangle_data_size = per_vertex_offset * 3;
	
	*pp_triangles = malloc(sizeof(Triangle) * out_triangle_count);
	*pp_attributes = malloc(triangle_data_size * out_triangle_count);
	
	u32 max_possible_fragment_count = 0;
	u32 out_triangle_index = 0;

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
			(a_vertex_positions[0].z > +a_vertex_positions[1].w	&& a_vertex_positions[1].z > +a_vertex_positions[1].w && a_vertex_positions[2].z > +a_vertex_positions[2].w)) {
			continue;
		}

		// clipping
		i32 num_extra_triangles = 0;
		Vertex a_clipped_vertices[MAX_NUM_CLIP_VERTICES];
		u32 clipped_vertex_count = 0;
		run_clipper(&a_clipped_vertices, p_vertex_output_data, in_triangle_index, &clipped_vertex_count);

		for(u32 clipped_vertex_index = 1; clipped_vertex_index < clipped_vertex_count - 1; ++clipped_vertex_index) {
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
			signed_area = (x[1] - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (y[1] - y[0]);
			if(signed_area == 0) { break; } // degenerate triangle 
			if(abs(signed_area) < (1 << (NUM_SUB_PIXEL_PRECISION_BITS * 2))) { break; }; // degenerate triangle ?

			// face culling with winding order
			//if(signed_area > 0) { continue; }; // ASSUMPTION(cerlet): Default back-face culling with

			Setup setup;
			set_edge_function(&setup.a_edge_functions[2], signed_area, x[0], y[0], x[1], y[1]);
			set_edge_function(&setup.a_edge_functions[0], signed_area, x[1], y[1], x[2], y[2]);
			set_edge_function(&setup.a_edge_functions[1], signed_area, x[2], y[2], x[0], y[0]);
			setup.one_over_area = fabs(1.f / (f32)(signed_area >> (NUM_SUB_PIXEL_PRECISION_BITS * 2)));
			setup.a_reciprocal_ws[0] = a_reciprocal_ws[0];
			setup.a_reciprocal_ws[1] = a_reciprocal_ws[1];
			setup.a_reciprocal_ws[2] = a_reciprocal_ws[2];

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
			min_bounds.x = MAX(min_bounds.x, 0);							// prevent negative coords
			min_bounds.y = MAX(min_bounds.y, 0);
			// max corner
			max_bounds.x = MAX3(x[0], x[1], x[2]) >> NUM_SUB_PIXEL_PRECISION_BITS;
			max_bounds.y = MAX3(y[0], y[1], y[2]) >> NUM_SUB_PIXEL_PRECISION_BITS;
			max_bounds.x = MIN(max_bounds.x + 1, (i32)viewport.width - 1);	// prevent too large coords
			max_bounds.y = MIN(max_bounds.y + 1, (i32)viewport.height - 1);

			max_possible_fragment_count += ((max_bounds.x - min_bounds.x + 1) * (max_bounds.y - min_bounds.y + 1));
			p_current_triangle->min_bounds = min_bounds;
			p_current_triangle->max_bounds = max_bounds;

			p_current_triangle->p_attributes = (*pp_attributes) + out_triangle_index;
			out_triangle_index++;
		}	
	}

	*p_assembled_triangle_count = out_triangle_index;
	*p_max_possible_fragment_count = max_possible_fragment_count;
	rmt_EndCPUSample();
}

void run_rasterizer( u32 max_possible_fragment_count, u32 assembled_triangle_count, const Triangle *p_triangles, u32 *p_num_fragments, Fragment **pp_fragments) {
	rmt_BeginCPUSample(rasterizer_stage, 0);
	Viewport viewport = graphics_pipeline.rs.viewport;
	u8 num_attibutes = graphics_pipeline.vs.output_register_count;
	u32 current_fragment_index = 0;
	*pp_fragments = malloc(max_possible_fragment_count * sizeof(Fragment));

	for(u32 triangle_index = 0; triangle_index < assembled_triangle_count; ++triangle_index) {
		Triangle* p_tri = p_triangles + triangle_index;
		v4f32* p_attributes = p_tri->p_attributes;

		for(i32 y = p_tri->min_bounds.y; y <= p_tri->max_bounds.y; ++y) {
			for(i32 x = p_tri->min_bounds.x; x <= p_tri->max_bounds.x; ++x) {
				v2i32 frag_coords = { x,y };
				if(is_inside_triangle(&p_tri->setup, frag_coords)) { // use edge functions for inclusion testing					
					v2f32 barycentric_coords = compute_barycentric_coords(&p_tri->setup);
					v4f32 frag_pos_ss = interpolate_attribute(p_tri->p_attributes, barycentric_coords, num_attibutes);

					// Early-Z Test
					// ASSUMPTION(Cerlet): Pixel shader does not change the depth of the fragment! 
					f32 fragment_z = frag_pos_ss.z;
					f32 depth = graphics_pipeline.om.p_depth[y*(i32)graphics_pipeline.rs.viewport.width + x];
					if(depth > fragment_z) {
						continue;
					}
					graphics_pipeline.om.p_depth[y*(i32)graphics_pipeline.rs.viewport.width + x] = fragment_z;

					v2f32 perspective_barycentric_coords = compute_perspective_barycentric_coords(&p_tri->setup, barycentric_coords);

					Fragment *p_fragment = (*pp_fragments) + current_fragment_index++;
					p_fragment->coordinates = frag_coords;
					p_fragment->p_attributes = p_attributes;
					p_fragment->barycentric_coords = barycentric_coords;
					p_fragment->perspective_barycentric_coords = perspective_barycentric_coords;
				}
			}
		}
	}
	*p_num_fragments = current_fragment_index;
	rmt_EndCPUSample();
}

void run_pixel_shader_stage(const Fragment* p_fragments, u32 num_fragments) {
	rmt_BeginCPUSample(pixel_shader_stage, 0);
	u8 num_attibutes = graphics_pipeline.vs.output_register_count;
	for(u32 frag_index = 0; frag_index < num_fragments; ++frag_index) {
		Fragment *p_fragment = p_fragments + frag_index;
		v4f32 a_fragment_attributes[PIXEL_SHADER_INPUT_REGISTER_COUNT];
		for(i32 attribute_index = 0; attribute_index < num_attibutes; ++attribute_index) {
			a_fragment_attributes[attribute_index] = interpolate_attribute(p_fragment->p_attributes + attribute_index, p_fragment->perspective_barycentric_coords, num_attibutes);
		}

		// Pixel Shader
		v4f32 fragment_out_color;
		graphics_pipeline.ps.shader(a_fragment_attributes, (void*)&fragment_out_color, graphics_pipeline.ps.p_shader_resource_views);
		// Output Merger
		graphics_pipeline.om.p_colors[p_fragment->coordinates.y*(i32)graphics_pipeline.rs.viewport.width + p_fragment->coordinates.x] = encode_color_as_u32(fragment_out_color.xyz);
	}
	rmt_EndCPUSample();
}

void draw_indexed(UINT index_count /* TODO(cerlet): Use UINT start_index_location, int base_vertex_location*/) {
	rmt_BeginCPUSample(draw_indexed, 0);

	void *p_vertex_input_data = NULL;
	run_input_assembler_stage(index_count, &p_vertex_input_data);
	
	u32 per_vertex_output_data_size = 0;
	void *p_vertex_output_data = NULL;
	run_vertex_shader_stage(index_count, p_vertex_input_data, &per_vertex_output_data_size, &p_vertex_output_data);

	assert((index_count % 3) == 0);
	u32 triangle_count = index_count / 3;

	Triangle *p_triangles = NULL;
	v4f32 *p_attributes = NULL;
	u32 assembled_triangle_count = 0;
	u32 max_possible_fragment_count = 0;
	run_primitive_assembly_stage(triangle_count, p_vertex_output_data, &assembled_triangle_count, &max_possible_fragment_count, &p_triangles, &p_attributes);
	
	Viewport viewport = graphics_pipeline.rs.viewport;
	u8 num_attibutes = graphics_pipeline.vs.output_register_count;

	Fragment *p_fragments = NULL;
	u32 num_fragments;
	run_rasterizer(max_possible_fragment_count, assembled_triangle_count, p_triangles, &num_fragments, &p_fragments);

	run_pixel_shader_stage(p_fragments, num_fragments);

	free(p_vertex_input_data);
	free(p_vertex_output_data);
	free(p_attributes);
	free(p_triangles);
	free(p_fragments);
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
	graphics_pipeline.ia.input_layout = transform_vs.in_vertex_size;
	graphics_pipeline.ia.primitive_topology = PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	graphics_pipeline.ia.p_index_buffer = test_mesh.p_index_buffer;// suprematist_index_buffer;
	graphics_pipeline.ia.p_vertex_buffer = test_mesh.p_vertex_buffer;// suprematist_vertex_buffer;

	graphics_pipeline.vs.output_register_count = transform_vs.out_vertex_size / sizeof(v4f32);
	graphics_pipeline.vs.shader = transform_vs.vs_main;
	graphics_pipeline.vs.p_constant_buffers[0] = &per_frame_cb;

	Viewport viewport = { 0.f,0.f,(f32)frame_width,(f32)frame_height,0.f,1.f };
	graphics_pipeline.rs.viewport = viewport;

	graphics_pipeline.ps.shader = passthrough_ps.ps_main;
	graphics_pipeline.ps.p_shader_resource_views[0] = &test_tex;

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
		//OCTARINE_MESH_RESULT result = octarine_mesh_read_from_file("../assets/malevich_scene.octrn", &test_mesh.header, &p_data);
		OCTARINE_MESH_RESULT result = octarine_mesh_read_from_file("../assets/plane.octrn", &test_mesh.header, &p_data);
		if(result != OCTARINE_MESH_OK) { assert(false); };

		u32 vertex_size = sizeof(float) * 8;
		u32 vertex_buffer_size = test_mesh.header.vertex_count * vertex_size;
		u32 index_buffer_size = test_mesh.header.index_count * sizeof(u32);

		test_mesh.p_vertex_buffer = p_data;
		test_mesh.p_index_buffer = (u32*)(((uint8_t*)p_data) + vertex_buffer_size);
	}

	{ // Load texture

		OctarineImageHeader header;
		//OCTARINE_IMAGE result = octarine_image_read_from_file("../assets/malevich_scene_colors.octrn", &header, &test_tex.p_data);
		OCTARINE_IMAGE result = octarine_image_read_from_file("../assets/uv_grid_256.octrn", &header, &test_tex.p_data);
		if(result != OCTARINE_IMAGE_OK) { assert(false); };

		test_tex.width = header.width;
		test_tex.height = header.height;

	}

	{ // Init Camera
		camera.pos = (v3f32){ 3.5f, 1.0f, 1.0f};
		camera.yaw_rad = TO_RADIANS(-30.0);
		camera.pitch_rad = TO_RADIANS(0.0);
		camera.fov_y_angle_deg = 90.f;
		camera.near_plane = 0.1;
		camera.far_plane = 1000.1;

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

		RECT window_rect = { 0, 0, frame_width, frame_height };
		AdjustWindowRect(&window_rect, WS_OVERLAPPEDWINDOW, FALSE);

		h_window = CreateWindowExA(
			0, p_window_class_name, p_window_name,
			WS_OVERLAPPED | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT,
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