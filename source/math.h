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
#define PI 3.141592654f

#define TO_RADIANS(Degrees) (Degrees * (PI / 180.0f))
#define TO_DEGREES(Radians) (Radians * (180.0f / PI))

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

inline m4x4f32 m4x4f32_transpose(m4x4f32 *p_m0) {
	m4x4f32 result = {
		p_m0->m00, p_m0->m10, p_m0->m20, p_m0->m30,
		p_m0->m01, p_m0->m11, p_m0->m21, p_m0->m31,
		p_m0->m02, p_m0->m12, p_m0->m22, p_m0->m32,
		p_m0->m03, p_m0->m13, p_m0->m23, p_m0->m33 };
	return result;
}

inline m4x4f32 m4x4f32_mul_m4x4f32(m4x4f32 *p_m0, m4x4f32 *p_m1) {
	m4x4f32 transpose_m1 = m4x4f32_transpose(p_m1);
	m4x4f32 result = {
		v4f32_dot(p_m0->r0, transpose_m1.r0), v4f32_dot(p_m0->r0, transpose_m1.r1),  v4f32_dot(p_m0->r0, transpose_m1.r2),  v4f32_dot(p_m0->r0, transpose_m1.r3),
		v4f32_dot(p_m0->r1, transpose_m1.r0), v4f32_dot(p_m0->r1, transpose_m1.r1),  v4f32_dot(p_m0->r1, transpose_m1.r2),  v4f32_dot(p_m0->r1, transpose_m1.r3),
		v4f32_dot(p_m0->r2, transpose_m1.r0), v4f32_dot(p_m0->r2, transpose_m1.r1),  v4f32_dot(p_m0->r2, transpose_m1.r2),  v4f32_dot(p_m0->r2, transpose_m1.r3),
		v4f32_dot(p_m0->r3, transpose_m1.r0), v4f32_dot(p_m0->r3, transpose_m1.r1),  v4f32_dot(p_m0->r3, transpose_m1.r2),  v4f32_dot(p_m0->r3, transpose_m1.r3),
	};
	return result;
}

inline v4f32 v4f32_add_v4f32(v4f32 v0, v4f32 v1) {
	v4f32 result = { v0.x + v1.x, v0.y + v1.y, v0.z + v1.z, v0.w + v1.w };
	return result;
}

inline v4f32 v4f32_subtract_v4f32(v4f32 v0, v4f32 v1) {
	v4f32 result = { v0.x - v1.x, v0.y - v1.y, v0.z - v1.z, v0.w - v1.w };
	return result;
}

inline v4f32 v4f32_mul_f32(v4f32 v, f32 c) {
	v4f32 result = { v.x * c, v.y * c, v.z * c, v.w * c };
	return result;
}

inline f32 determinant(const m4x4f32* M)
{
	f32 value;
	value =
		M->m03*M->m12*M->m21*M->m30 - M->m02*M->m13*M->m21*M->m30 - M->m03*M->m11*M->m22*M->m30 + M->m01*M->m13*M->m22*M->m30 +
		M->m02*M->m11*M->m23*M->m30 - M->m01*M->m12*M->m23*M->m30 - M->m03*M->m12*M->m20*M->m31 + M->m02*M->m13*M->m20*M->m31 +
		M->m03*M->m10*M->m22*M->m31 - M->m00*M->m13*M->m22*M->m31 - M->m02*M->m10*M->m23*M->m31 + M->m00*M->m12*M->m23*M->m31 +
		M->m03*M->m11*M->m20*M->m32 - M->m01*M->m13*M->m20*M->m32 - M->m03*M->m10*M->m21*M->m32 + M->m00*M->m13*M->m21*M->m32 +
		M->m01*M->m10*M->m23*M->m32 - M->m00*M->m11*M->m23*M->m32 - M->m02*M->m11*M->m20*M->m33 + M->m01*M->m12*M->m20*M->m33 +
		M->m02*M->m10*M->m21*M->m33 - M->m00*M->m12*M->m21*M->m33 - M->m01*M->m10*M->m22*M->m33 + M->m00*M->m11*M->m22*M->m33;
	return value;
}

inline m4x4f32 inverse(const m4x4f32* M)
{
	f32 det = determinant(M);
	assert(det != 0.0);
	f32 scale = 1.0 / det;

	m4x4f32 inverse;
	inverse.m00 = (M->m12*M->m23*M->m31 - M->m13*M->m22*M->m31 + M->m13*M->m21*M->m32 - M->m11*M->m23*M->m32 - M->m12*M->m21*M->m33 + M->m11*M->m22*M->m33)*scale;
	inverse.m01 = (M->m03*M->m22*M->m31 - M->m02*M->m23*M->m31 - M->m03*M->m21*M->m32 + M->m01*M->m23*M->m32 + M->m02*M->m21*M->m33 - M->m01*M->m22*M->m33)*scale;
	inverse.m02 = (M->m02*M->m13*M->m31 - M->m03*M->m12*M->m31 + M->m03*M->m11*M->m32 - M->m01*M->m13*M->m32 - M->m02*M->m11*M->m33 + M->m01*M->m12*M->m33)*scale;
	inverse.m03 = (M->m03*M->m12*M->m21 - M->m02*M->m13*M->m21 - M->m03*M->m11*M->m22 + M->m01*M->m13*M->m22 + M->m02*M->m11*M->m23 - M->m01*M->m12*M->m23)*scale;
	inverse.m10 = (M->m13*M->m22*M->m30 - M->m12*M->m23*M->m30 - M->m13*M->m20*M->m32 + M->m10*M->m23*M->m32 + M->m12*M->m20*M->m33 - M->m10*M->m22*M->m33)*scale;
	inverse.m11 = (M->m02*M->m23*M->m30 - M->m03*M->m22*M->m30 + M->m03*M->m20*M->m32 - M->m00*M->m23*M->m32 - M->m02*M->m20*M->m33 + M->m00*M->m22*M->m33)*scale;
	inverse.m12 = (M->m03*M->m12*M->m30 - M->m02*M->m13*M->m30 - M->m03*M->m10*M->m32 + M->m00*M->m13*M->m32 + M->m02*M->m10*M->m33 - M->m00*M->m12*M->m33)*scale;
	inverse.m13 = (M->m02*M->m13*M->m20 - M->m03*M->m12*M->m20 + M->m03*M->m10*M->m22 - M->m00*M->m13*M->m22 - M->m02*M->m10*M->m23 + M->m00*M->m12*M->m23)*scale;
	inverse.m20 = (M->m11*M->m23*M->m30 - M->m13*M->m21*M->m30 + M->m13*M->m20*M->m31 - M->m10*M->m23*M->m31 - M->m11*M->m20*M->m33 + M->m10*M->m21*M->m33)*scale;
	inverse.m21 = (M->m03*M->m21*M->m30 - M->m01*M->m23*M->m30 - M->m03*M->m20*M->m31 + M->m00*M->m23*M->m31 + M->m01*M->m20*M->m33 - M->m00*M->m21*M->m33)*scale;
	inverse.m22 = (M->m01*M->m13*M->m30 - M->m03*M->m11*M->m30 + M->m03*M->m10*M->m31 - M->m00*M->m13*M->m31 - M->m01*M->m10*M->m33 + M->m00*M->m11*M->m33)*scale;
	inverse.m23 = (M->m03*M->m11*M->m20 - M->m01*M->m13*M->m20 - M->m03*M->m10*M->m21 + M->m00*M->m13*M->m21 + M->m01*M->m10*M->m23 - M->m00*M->m11*M->m23)*scale;
	inverse.m30 = (M->m12*M->m21*M->m30 - M->m11*M->m22*M->m30 - M->m12*M->m20*M->m31 + M->m10*M->m22*M->m31 + M->m11*M->m20*M->m32 - M->m10*M->m21*M->m32)*scale;
	inverse.m31 = (M->m01*M->m22*M->m30 - M->m02*M->m21*M->m30 + M->m02*M->m20*M->m31 - M->m00*M->m22*M->m31 - M->m01*M->m20*M->m32 + M->m00*M->m21*M->m32)*scale;
	inverse.m32 = (M->m02*M->m11*M->m30 - M->m01*M->m12*M->m30 - M->m02*M->m10*M->m31 + M->m00*M->m12*M->m31 + M->m01*M->m10*M->m32 - M->m00*M->m11*M->m32)*scale;
	inverse.m33 = (M->m01*M->m12*M->m20 - M->m02*M->m11*M->m20 + M->m02*M->m10*M->m21 - M->m00*M->m12*M->m21 - M->m01*M->m10*M->m22 + M->m00*M->m11*M->m22)*scale;

	return inverse;
}
