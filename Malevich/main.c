#define _CRT_SECURE_NO_WARNINGS

#define LEAN_AND_MEAN
#include <windows.h>

#include <stdbool.h>
#include <stdio.h>
#include <assert.h>

#include "math.h"
#include "External\Remotery.h"
#include "passthrough_vs.c"
#include "passthrough_ps.c"

#define WIDTH 820 //820;
#define HEIGHT 1000 //1000;

const int frame_width = WIDTH;
const int frame_height = HEIGHT;

u32 frame_buffer[WIDTH][HEIGHT];
f32 depth_buffer[WIDTH][HEIGHT];

#define NUM_SUB_PIXEL_PRECISION_BITS 4

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
	void *p_constant_buffers[16];
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
	void(*shader)(void *p_pixel_input_data, void *p_pixel_output_data);
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

typedef struct TriangleSetup {
	EdgeFunction a_edge_functions[3];
	i32 a_signed_distances[3];
	f32 one_over_area;
}TriangleSetup;

typedef struct PerFrameCB {
	m4x4f32 clip_from_world;
}PerFrameCB;

typedef struct Camera {
	m4x4f32 clip_from_view;
	m4x4f32 view_from_world;
	v3f32 pos;
	f32 yaw_in_degrees;
	f32 pitch_in_degrees;
	f32 roll_in_degrees;
	f32 fov_y_angle_deg;
	f32 near_plane;
} Camera;

Mesh test_mesh;
PerFrameCB per_frame_cb;
Camera camera;

LRESULT CALLBACK window_proc(HWND h_window, UINT msg, WPARAM w_param, LPARAM l_param)
{
	PAINTSTRUCT paint_struct;
	HDC h_device_context;
	switch(msg)
	{
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
		case WM_DESTROY:
			PostQuitMessage(0);
			break;
		default:
			return DefWindowProc(h_window, msg, w_param, l_param);
	}

	return 0;
}

void error_win32(const char* func_name, DWORD last_error)
{
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

inline u32 encode_color_as_u32(v3f32 color) {
	return (((i32)(color.x*255.99f)) << 16) + (((i32)(color.y*255.99f)) << 8) + (((i32)(color.z*255.99f)));
}

inline bool is_inside_edge(TriangleSetup *p_setup, u32 edge_index, v2i32 frag_coord) {
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

inline bool is_inside_triangle(TriangleSetup *p_setup, v2i32 frag_coord) {
	bool w0 = is_inside_edge(p_setup, 0, frag_coord);
	bool w1 = is_inside_edge(p_setup, 1, frag_coord);
	bool w2 = is_inside_edge(p_setup, 2, frag_coord);
	return w0 && w1 && w2;
}

// In this context, barycentric coords (u,v,w) = areal coordinates (A1/A,A2/A,A0/A) => u+v+w = 1;
inline v2f32 compute_barycentric_coords(TriangleSetup *p_setup) {
	v2f32 barycentric_coords;
	barycentric_coords.x = (float)(p_setup->a_signed_distances[1] >> (NUM_SUB_PIXEL_PRECISION_BITS * 2)) * p_setup->one_over_area;
	barycentric_coords.y = (float)(p_setup->a_signed_distances[2] >> (NUM_SUB_PIXEL_PRECISION_BITS * 2)) * p_setup->one_over_area;

	return barycentric_coords;
}

inline void interpolate_attribute(v4f32 *p_triangle_vertex_data, v2f32 barycentric_coords, u8 num_attributes_per_vertex, u8 attribute_index) {
	v4f32 *p_v0_attribute = p_triangle_vertex_data + attribute_index;
	v4f32 v1_attribute = *(p_triangle_vertex_data + attribute_index + num_attributes_per_vertex);
	v4f32 v2_attribute = *(p_triangle_vertex_data + attribute_index + num_attributes_per_vertex * 2);
	
	*p_v0_attribute = v4f32_add_v4f32(*p_v0_attribute, v4f32_add_v4f32(v4f32_mul_f32(v4f32_subtract_v4f32(v1_attribute, *p_v0_attribute), barycentric_coords.x), v4f32_mul_f32(v4f32_subtract_v4f32(v2_attribute, *p_v0_attribute), barycentric_coords.y)));
	return;
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

void draw_indexed(UINT index_count /* TODO(cerlet): Use UINT start_index_location, int base_vertex_location*/) {
	
	// Input Assembler
	if(graphics_pipeline.ia.primitive_topology != PRIMITIVE_TOPOLOGY_TRIANGLELIST) {
		DebugBreak();
	}

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

	// Vertex Shader
	u32 per_vertex_output_data_size = graphics_pipeline.vs.output_register_count * sizeof(v4f32);
	void **p_constant_buffers = graphics_pipeline.vs.p_constant_buffers;
	void *p_vertex_output_data = malloc(vertex_count*per_vertex_output_data_size);
	for(u32 vertex_id = 0; vertex_id < vertex_count; ++vertex_id) {
		graphics_pipeline.vs.shader(p_vertex_input_data, p_vertex_output_data, p_constant_buffers, vertex_id);
	}

	// Primitive Assembly:
	assert((index_count % 3) == 0);
	u32 triangle_count = index_count / 3;
	for(u32 triangle_index = 0; triangle_index < triangle_count; ++triangle_index) {
		
		v4f32 a_vertex_positions[3];
		u32 per_vertex_offset = per_vertex_output_data_size;
		a_vertex_positions[0] = *((v4f32*)((u8*)p_vertex_output_data + triangle_index * per_vertex_offset * 3 ));
		a_vertex_positions[1] = *((v4f32*)((u8*)p_vertex_output_data + triangle_index * per_vertex_offset * 3 + per_vertex_offset));
		a_vertex_positions[2] = *((v4f32*)((u8*)p_vertex_output_data + triangle_index * per_vertex_offset * 3 + per_vertex_offset * 2));
		
		// viewport culling
		if(a_vertex_positions[0].w == 0 || a_vertex_positions[1].w == 0 || a_vertex_positions[2].w == 0) {
			continue;// degenerate triangle
		}

		if( (a_vertex_positions[0].x < -a_vertex_positions[0].w && a_vertex_positions[1].x < -a_vertex_positions[1].w && a_vertex_positions[2].x < -a_vertex_positions[2].w) ||
			(a_vertex_positions[0].x > +a_vertex_positions[0].w && a_vertex_positions[1].x > +a_vertex_positions[1].w && a_vertex_positions[2].x > +a_vertex_positions[2].w) ||
			(a_vertex_positions[0].y < -a_vertex_positions[0].w && a_vertex_positions[1].y < -a_vertex_positions[1].w && a_vertex_positions[2].y < -a_vertex_positions[2].w) ||
			(a_vertex_positions[0].y > +a_vertex_positions[0].w && a_vertex_positions[1].y > +a_vertex_positions[1].w && a_vertex_positions[2].y > +a_vertex_positions[2].w) ||
			(a_vertex_positions[0].z < 0.f						&& a_vertex_positions[1].z < 0.f					  && a_vertex_positions[2].z < 0.f					   ) ||
			(a_vertex_positions[0].z > +a_vertex_positions[1].w	&& a_vertex_positions[1].z > +a_vertex_positions[1].w && a_vertex_positions[2].z > +a_vertex_positions[2].w)) {
			continue;
		}

		// clipping
		// TODO(cerlet): Implement Clipping
		// ASSUMPTION(cerlet): No need for clipping!

		// projection : Clip Space --> NDC Space
		f32 factor;
		factor = 1.0 / a_vertex_positions[0].w;
		a_vertex_positions[0].x *= factor;
		a_vertex_positions[0].y *= factor;
		a_vertex_positions[0].z *= factor;

		factor = 1.0 / a_vertex_positions[1].w;
		a_vertex_positions[1].x *= factor;
		a_vertex_positions[1].y *= factor;
		a_vertex_positions[1].z *= factor;

		factor = 1.0 / a_vertex_positions[2].w;
		a_vertex_positions[2].x *= factor;
		a_vertex_positions[2].y *= factor;
		a_vertex_positions[2].z *= factor;

		// viewport transformation : NDC Space --> Screen Space
		Viewport viewport = graphics_pipeline.rs.viewport;
		v4f32 vertex_pos_ss;
		m4x4f32 screen_from_ndc = {
			viewport.width*0.5, 0, 0, viewport.width*0.5 + viewport.top_left_x,
			0, viewport.height*0.5, 0, viewport.height*0.5 + viewport.top_left_y,
			0, 0, viewport.max_depth-viewport.min_depth, viewport.min_depth,
			0,	0,	0,	1
		};
		
		vertex_pos_ss = m4x4f32_mul_v4f32(screen_from_ndc, a_vertex_positions[0]);
		a_vertex_positions[0] = vertex_pos_ss;

		vertex_pos_ss = m4x4f32_mul_v4f32(screen_from_ndc, a_vertex_positions[1]);
		a_vertex_positions[1] = vertex_pos_ss;

		vertex_pos_ss = m4x4f32_mul_v4f32(screen_from_ndc, a_vertex_positions[2]);
		a_vertex_positions[2] = vertex_pos_ss;

		// convert ss posions to fixed-point representation and snap
		i32 x[3], y[3], signed_area;
		x[0] = floor(a_vertex_positions[0].x * (1 << NUM_SUB_PIXEL_PRECISION_BITS) + 0.5);
		x[1] = floor(a_vertex_positions[1].x * (1 << NUM_SUB_PIXEL_PRECISION_BITS) + 0.5);
		x[2] = floor(a_vertex_positions[2].x * (1 << NUM_SUB_PIXEL_PRECISION_BITS) + 0.5);
		y[0] = floor(a_vertex_positions[0].y * (1 << NUM_SUB_PIXEL_PRECISION_BITS) + 0.5);
		y[1] = floor(a_vertex_positions[1].y * (1 << NUM_SUB_PIXEL_PRECISION_BITS) + 0.5);
		y[2] = floor(a_vertex_positions[2].y * (1 << NUM_SUB_PIXEL_PRECISION_BITS) + 0.5);

		// triangle setup
		signed_area = (x[1] - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (y[1] - y[0]); // TODO(cerlet): Implement face culling!
		if(signed_area == 0) { // degenerate triangle 
			continue;
		}	
		TriangleSetup setup;
		set_edge_function(&setup.a_edge_functions[2], signed_area, x[0], y[0], x[1], y[1]);
		set_edge_function(&setup.a_edge_functions[0], signed_area, x[1], y[1], x[2], y[2]);
		set_edge_function(&setup.a_edge_functions[1], signed_area, x[2], y[2], x[0], y[0]);
		setup.one_over_area = fabs(1.f / (f32)(signed_area >> (NUM_SUB_PIXEL_PRECISION_BITS * 2)));

		// Rasterizer
		{
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

			for(i32 y = min_bounds.y; y <= max_bounds.y; ++y) {
				for(i32 x = min_bounds.x; x<max_bounds.x; ++x) {
					v2i32 frag_coords = { x,y };
					if(is_inside_triangle(&setup, frag_coords)) { // use edge functions for inclusion testing					
						v2f32 barycentric_coords = compute_barycentric_coords(&setup);
						v4f32 *p_vertex_data = (v4f32*)((u8*)p_vertex_output_data + triangle_index * per_vertex_offset * 3);
						u8 num_attibutes = graphics_pipeline.vs.output_register_count;
						for(i32 attribute_index = 0; attribute_index < num_attibutes; ++attribute_index) {
							interpolate_attribute(p_vertex_data, barycentric_coords, num_attibutes, attribute_index);
						}
						//Early-Z Test
						// ASSUMPTION : Pixel shader does not change the depth of the fragment!  - Cerlet 
						f32 fragment_z = p_vertex_data->z;
						f32 depth = graphics_pipeline.om.p_depth[y*(i32)graphics_pipeline.rs.viewport.width + x];
						if(depth < fragment_z) { continue; }

						// Pixel Shader
						v4f32 fragment_out_color;
						graphics_pipeline.ps.shader(p_vertex_data, (void*)&fragment_out_color);
						// Output Merger
						graphics_pipeline.om.p_colors[y*(i32)graphics_pipeline.rs.viewport.width+x] = encode_color_as_u32(fragment_out_color.xyz);
						graphics_pipeline.om.p_depth[y*(i32)graphics_pipeline.rs.viewport.width + x] = fragment_z;
					}
				}
			}
		}
	}
}

void clear_render_target_view(const f32 *p_clear_color) {
	v3f32 clear_color = { p_clear_color[0],p_clear_color[1] ,p_clear_color[2] };
	u32 encoded_clear = encode_color_as_u32(clear_color);
	
	u32 frame_buffer_texel_count = sizeof(frame_buffer) / sizeof(u32);
	u32 *p_texel = frame_buffer;
	while(frame_buffer_texel_count--) {
		*p_texel++ = encoded_clear;
	}
}

void clear_depth_stencil_view(const f32 depth) {
	f32 *p_depth = depth_buffer;
	u32 depth_buffer_texel_count = sizeof(depth_buffer) / sizeof(f32);
	while(depth_buffer_texel_count--) {
		*p_depth++ = depth;
	}
}

void render() {
	graphics_pipeline.ia.input_layout = sizeof(passthrough_vs.vs_input);
	graphics_pipeline.ia.primitive_topology = PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	graphics_pipeline.ia.p_index_buffer = test_mesh.p_index_buffer;// suprematist_index_buffer;
	graphics_pipeline.ia.p_vertex_buffer = test_mesh.p_vertex_buffer;// suprematist_vertex_buffer;

	graphics_pipeline.vs.output_register_count = sizeof(passthrough_vs.vs_output) / sizeof(v4f32);
	graphics_pipeline.vs.shader = passthrough_vs.vs_main;
	graphics_pipeline.vs.p_constant_buffers[0] = &per_frame_cb;

	Viewport viewport = { 0.f,0.f,(f32)frame_width,(f32)frame_height,0.f,1.f };
	graphics_pipeline.rs.viewport = viewport;

	graphics_pipeline.ps.shader = passthrough_ps.ps_main;

	graphics_pipeline.om.p_colors = &frame_buffer[0][0];
	graphics_pipeline.om.p_depth = &depth_buffer[0][0];
	//const f32 clear_color[4] = { (f32)227/255, (f32)223/255, (f32)216/255, 0.f };
	//clear_render_target_view(clear_color); // TODO(cerlet): Implement clearing via render target view pointer!
	clear_depth_stencil_view(1.0);
	draw_indexed(test_mesh.header.index_count);
}

void present(HWND h_window) {
	InvalidateRect(h_window, NULL, FALSE);
	UpdateWindow(h_window);
}

void init() {
	{ // Load mesh
		FILE *p_file = fopen("suzanne.poi", "rb");
		assert(p_file);
		fread(&test_mesh.header, sizeof(test_mesh.header), 1, p_file);

		uint32_t vertex_size = sizeof(float) * 6;
		uint32_t vertex_buffer_size = test_mesh.header.vertex_count * vertex_size;
		uint32_t index_buffer_size = test_mesh.header.index_count * sizeof(uint32_t);

		test_mesh.p_vertex_buffer = malloc(vertex_buffer_size);
		fread(&test_mesh.p_vertex_buffer, vertex_size, test_mesh.header.vertex_count, p_file);
		test_mesh.p_index_buffer = (u32*)malloc(index_buffer_size);
		fread(&test_mesh.p_index_buffer, sizeof(uint32_t), test_mesh.header.index_count, p_file);
		fclose(p_file);
	}
	{ // Init Camera
		camera.pos = (v3f32){ 7.48113f, -6.50764f, 5.34367f};
		camera.yaw_in_degrees = 46.6919f;
		camera.pitch_in_degrees = 63.5593;
		camera.roll_in_degrees = 0.0;
		camera.fov_y_angle_deg = 50.f;
		camera.near_plane = 0.1;

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

		float cos_pitch = cos(TO_RADIANS(camera.pitch_in_degrees));
		float sin_pitch = sin(TO_RADIANS(camera.pitch_in_degrees));
		m4x4f32 rotation_pitch = { // pitch axis is x in view space
			1.0, 0.0, 0.0, 0.0,
			0.0, cos_pitch, sin_pitch, 0.0,
			0.0,-sin_pitch, cos_pitch, 0.0,
			0.0, 0.0, 0.0, 1.0
		};

		float cos_yaw = cos(TO_RADIANS(camera.yaw_in_degrees));
		float sin_yaw = sin(TO_RADIANS(camera.yaw_in_degrees));
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

		m4x4f32 world_from_view = m4x4f32_mul_m4x4f32(&rotation_pitch, &rotation_yaw);
		world_from_view = m4x4f32_mul_m4x4f32(&change_of_basis, &world_from_view);
		camera.view_from_world = inverse(&world_from_view);
	
		per_frame_cb.clip_from_world = m4x4f32_mul_m4x4f32(&camera.clip_from_view, &camera.view_from_world);
	}
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
			rmt_BeginCPUSample(render, 0);
			render();
			rmt_EndCPUSample();
			present(h_window);
		}
	}

	rmt_DestroyGlobalInstance(p_remotery);
	return 0;
}