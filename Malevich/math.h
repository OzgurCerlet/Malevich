#pragma once

#include <stdint.h>
#include <math.h>

typedef uint8_t		u8;
typedef uint16_t	u16;
typedef uint32_t	u32;
typedef uint64_t	u64;
typedef int8_t		i8;
typedef int16_t		i16;
typedef int32_t		i32;
typedef int64_t		i64;
typedef float		f32;
typedef double		f64;

#ifndef MIN
	#define MIN(x,y) ((x<y)?(x):(y))
#endif
#ifndef MAX
	#define MAX(x,y) ((x>y)?(x):(y))
#endif

#define MAX3(x,y,z) (MAX(x,(MAX(y,z))))
#define MIN3(x,y,z) (MIN(x,(MIN(y,z))))

typedef struct v2f32
{
	union {
		f32 xy[2];
		struct {
			f32 x, y;
		};
	};
} v2f32;

typedef struct v2i32
{
	union
	{
		i32 xy[2];
		struct
		{
			i32 x, y;
		};
	};
} v2i32;

typedef struct v3f32 {
	union {
		f32 xyz[3];
		struct {
			f32 x, y, z;
		};
	};
} v3f32;

typedef struct v4f32 {
	union {
		f32 xyzw[4];
		struct {
			f32 x, y, z, w;
		};
		struct {
			v3f32 xyz;
		};
	};
} v4f32;

typedef struct m4x4f32 {
	union{
		f32 m[16];
		f32 rc[4][4];
		struct {
			v4f32 r0;
			v4f32 r1;
			v4f32 r2;
			v4f32 r3;
		};
		struct {
			f32 m00; f32 m01; f32 m02; f32 m03;
			f32 m10; f32 m11; f32 m12; f32 m13;
			f32 m20; f32 m21; f32 m22; f32 m23;
			f32 m30; f32 m31; f32 m32; f32 m33;
		};
	};
} m4x4f32;

inline f32 v4f32_dot(v4f32 v0, v4f32 v1) {
	f32 result = v0.x * v1.x + v0.y * v1.y + v0.z * v1.z + v0.w * v1.w;
	return result;
}

inline v4f32 m4x4f32_mul_v4f32(m4x4f32 m, v4f32 v) {
	v4f32 result = { v4f32_dot(m.r0, v), v4f32_dot(m.r1, v), v4f32_dot(m.r2, v), v4f32_dot(m.r3, v) };
	return result;
}

inline v4f32 v4f32_add_v4f32(v4f32 v0, v4f32 v1) {
	v4f32 result = { v0.x + v1.x, v0.y + v1.y, v0.z + v1.z, v0.w + v1.w };
	return result;
}

inline v4f32 v4f32_mul_f32(v4f32 v, f32 c) {
	v4f32 result = { v.x * c, v.y * c, v.z * c, v.w * c };
	return result;
}
