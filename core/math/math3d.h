/* math3d.h - Silver Diamond Game Engine :: 3D math (header-only)
 *
 * Column-major mat4 (matches OpenGL uniform upload with transpose=GL_FALSE).
 * Right-handed coordinates, -Z forward, +Y up (glTF convention).
 * Everything is `static` inline: zero TU setup, no library to link.
 * Hot paths are allocation-free and branch-light on purpose.
 */
#ifndef SDGE_MATH3D_H
#define SDGE_MATH3D_H

#include "../sdge_def.h"
#include <math.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef float sdg_vec3[3];
typedef float sdg_vec4[4];
typedef float sdg_quat[4];   /* (x, y, z, w), w = cos(angle/2) */
typedef float sdg_mat4[16];  /* column-major: index = col*4 + row */

#define SDG_PI 3.14159265358979323846f

/* ---------------------------------------------------------------- vec3 */
SDGE_INLINE void v3_set(sdg_vec3 o, float x, float y, float z) { o[0]=x; o[1]=y; o[2]=z; }
SDGE_INLINE void v3_copy(sdg_vec3 o, const sdg_vec3 a)          { o[0]=a[0]; o[1]=a[1]; o[2]=a[2]; }
SDGE_INLINE void v3_add(sdg_vec3 o, const sdg_vec3 a, const sdg_vec3 b) {
    o[0]=a[0]+b[0]; o[1]=a[1]+b[1]; o[2]=a[2]+b[2];
}
SDGE_INLINE void v3_sub(sdg_vec3 o, const sdg_vec3 a, const sdg_vec3 b) {
    o[0]=a[0]-b[0]; o[1]=a[1]-b[1]; o[2]=a[2]-b[2];
}
SDGE_INLINE void v3_mul(sdg_vec3 o, const sdg_vec3 a, float s) {
    o[0]=a[0]*s; o[1]=a[1]*s; o[2]=a[2]*s;
}
SDGE_INLINE void v3_mulv(sdg_vec3 o, const sdg_vec3 a, const sdg_vec3 b) { /* hadamard */
    o[0]=a[0]*b[0]; o[1]=a[1]*b[1]; o[2]=a[2]*b[2];
}
SDGE_INLINE float v3_dot(const sdg_vec3 a, const sdg_vec3 b) {
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}
SDGE_INLINE void v3_cross(sdg_vec3 o, const sdg_vec3 a, const sdg_vec3 b) {
    float x = a[1]*b[2] - a[2]*b[1];
    float y = a[2]*b[0] - a[0]*b[2];
    float z = a[0]*b[1] - a[1]*b[0];
    o[0]=x; o[1]=y; o[2]=z;
}
SDGE_INLINE float v3_len(const sdg_vec3 a) { return sqrtf(v3_dot(a, a)); }
SDGE_INLINE float v3_norm(sdg_vec3 o, const sdg_vec3 a) {
    float l = v3_len(a);
    if (l > 1e-12f) { float inv = 1.0f / l; o[0]=a[0]*inv; o[1]=a[1]*inv; o[2]=a[2]*inv; }
    else            { o[0]=0.0f; o[1]=0.0f; o[2]=0.0f; }
    return l;
}
SDGE_INLINE void v3_lerp(sdg_vec3 o, const sdg_vec3 a, const sdg_vec3 b, float t) {
    o[0]=a[0]+(b[0]-a[0])*t; o[1]=a[1]+(b[1]-a[1])*t; o[2]=a[2]+(b[2]-a[2])*t;
}

/* ---------------------------------------------------------------- vec4 */
SDGE_INLINE void v4_set(sdg_vec4 o, float x, float y, float z, float w) {
    o[0]=x; o[1]=y; o[2]=z; o[3]=w;
}
SDGE_INLINE void v4_copy(sdg_vec4 o, const sdg_vec4 a) {
    o[0]=a[0]; o[1]=a[1]; o[2]=a[2]; o[3]=a[3];
}

/* ---------------------------------------------------------------- quat */
SDGE_INLINE void q_identity(sdg_quat o)      { o[0]=0; o[1]=0; o[2]=0; o[3]=1; }
SDGE_INLINE void q_copy(sdg_quat o, const sdg_quat a) { v4_copy(o, a); }
SDGE_INLINE float q_dot(const sdg_quat a, const sdg_quat b) {
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2] + a[3]*b[3];
}
SDGE_INLINE void q_norm(sdg_quat o, const sdg_quat a) {
    float l2 = q_dot(a, a);
    if (l2 > 1e-12f) { float inv = 1.0f / sqrtf(l2);
        o[0]=a[0]*inv; o[1]=a[1]*inv; o[2]=a[2]*inv; o[3]=a[3]*inv; }
    else q_identity(o);
}
SDGE_INLINE void q_from_axis_angle(sdg_quat o, const sdg_vec3 axis, float rad) {
    float h = 0.5f * rad, s = sinf(h);
    o[0] = axis[0]*s; o[1] = axis[1]*s; o[2] = axis[2]*s; o[3] = cosf(h);
}
/* o = a * b  (apply b first, then a - glTF/scene-graph order) */
SDGE_INLINE void q_mul(sdg_quat o, const sdg_quat a, const sdg_quat b) {
    float x = a[3]*b[0] + a[0]*b[3] + a[1]*b[2] - a[2]*b[1];
    float y = a[3]*b[1] - a[0]*b[2] + a[1]*b[3] + a[2]*b[0];
    float z = a[3]*b[2] + a[0]*b[1] - a[1]*b[0] + a[2]*b[3];
    float w = a[3]*b[3] - a[0]*b[0] - a[1]*b[1] - a[2]*b[2];
    o[0]=x; o[1]=y; o[2]=z; o[3]=w;
}
/* spherical interpolation, shortest arc; t is clamped to [0,1] */
SDGE_INLINE void q_slerp(sdg_quat o, const sdg_quat a, const sdg_quat b, float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    float d = q_dot(a, b);
    sdg_quat bb;
    if (d < 0.0f) {                       /* shortest arc */
        bb[0]=-b[0]; bb[1]=-b[1]; bb[2]=-b[2]; bb[3]=-b[3]; d = -d;
    } else v4_copy(bb, b);
    if (d > 0.9995f) {                    /* nearly identical: nlerp */
        o[0]=a[0]+(bb[0]-a[0])*t; o[1]=a[1]+(bb[1]-a[1])*t;
        o[2]=a[2]+(bb[2]-a[2])*t; o[3]=a[3]+(bb[3]-a[3])*t;
        q_norm(o, o);
        return;
    }
    float th   = acosf(d);
    float sth  = sinf(th);
    float sa   = sinf((1.0f - t) * th) / sth;
    float sb   = sinf(t * th) / sth;
    o[0]=a[0]*sa + bb[0]*sb; o[1]=a[1]*sa + bb[1]*sb;
    o[2]=a[2]*sa + bb[2]*sb; o[3]=a[3]*sa + bb[3]*sb;
}
/* rotate v by q */
SDGE_INLINE void q_rot_vec3(sdg_vec3 o, const sdg_quat q, const sdg_vec3 v) {
    /* t = 2 * cross(q.xyz, v);  o = v + q.w*t + cross(q.xyz, t) */
    float tx = 2.0f*(q[1]*v[2] - q[2]*v[1]);
    float ty = 2.0f*(q[2]*v[0] - q[0]*v[2]);
    float tz = 2.0f*(q[0]*v[1] - q[1]*v[0]);
    o[0] = v[0] + q[3]*tx + (q[1]*tz - q[2]*ty);
    o[1] = v[1] + q[3]*ty + (q[2]*tx - q[0]*tz);
    o[2] = v[2] + q[3]*tz + (q[0]*ty - q[1]*tx);
}
SDGE_INLINE void q_to_mat4(sdg_mat4 o, const sdg_quat q) {
    float x=q[0], y=q[1], z=q[2], w=q[3];
    float x2=x+x, y2=y+y, z2=z+z;
    float xx=x*x2, xy=x*y2, xz=x*z2, yy=y*y2, yz=y*z2, zz=z*z2, wx=w*x2, wy=w*y2, wz=w*z2;
    o[0]=1-(yy+zz); o[1]=    xy+wz; o[2]=    xz-wy; o[3]=0;
    o[4]=    xy-wz; o[5]=1-(xx+zz); o[6]=    yz+wx; o[7]=0;
    o[8]=    xz+wy; o[9]=    yz-wx; o[10]=1-(xx+yy); o[11]=0;
    o[12]=0; o[13]=0; o[14]=0; o[15]=1;
}

/* ---------------------------------------------------------------- mat4 */
SDGE_INLINE void m4_identity(sdg_mat4 o) {
    memset(o, 0, sizeof(sdg_mat4));
    o[0]=1.0f; o[5]=1.0f; o[10]=1.0f; o[15]=1.0f;
}
SDGE_INLINE void m4_copy(sdg_mat4 o, const sdg_mat4 a) { memcpy(o, a, sizeof(sdg_mat4)); }
/* o = a * b   (transform a point by b first, then by a) */
SDGE_INLINE void m4_mul(sdg_mat4 o, const sdg_mat4 a, const sdg_mat4 b) {
    for (int c = 0; c < 4; ++c) {
        float b0=b[c*4+0], b1=b[c*4+1], b2=b[c*4+2], b3=b[c*4+3];
        o[c*4+0] = a[0]*b0 + a[4]*b1 + a[8]*b2  + a[12]*b3;
        o[c*4+1] = a[1]*b0 + a[5]*b1 + a[9]*b2  + a[13]*b3;
        o[c*4+2] = a[2]*b0 + a[6]*b1 + a[10]*b2 + a[14]*b3;
        o[c*4+3] = a[3]*b0 + a[7]*b1 + a[11]*b2 + a[15]*b3;
    }
}
SDGE_INLINE void m4_translation(sdg_mat4 o, float x, float y, float z) {
    m4_identity(o); o[12]=x; o[13]=y; o[14]=z;
}
SDGE_INLINE void m4_scaling(sdg_mat4 o, float x, float y, float z) {
    memset(o, 0, sizeof(sdg_mat4));
    o[0]=x; o[5]=y; o[10]=z; o[15]=1.0f;
}
/* transform vec4: o = m * v */
SDGE_INLINE void m4_mul_vec4(sdg_vec4 o, const sdg_mat4 m, const sdg_vec4 v) {
    o[0]=m[0]*v[0]+m[4]*v[1]+m[8]*v[2]+m[12]*v[3];
    o[1]=m[1]*v[0]+m[5]*v[1]+m[9]*v[2]+m[13]*v[3];
    o[2]=m[2]*v[0]+m[6]*v[1]+m[10]*v[2]+m[14]*v[3];
    o[3]=m[3]*v[0]+m[7]*v[1]+m[11]*v[2]+m[15]*v[3];
}
/* transform point (w=1); returns w */
SDGE_INLINE float m4_mul_point(sdg_vec3 o, const sdg_mat4 m, const sdg_vec3 p) {
    sdg_vec4 r;
    sdg_vec4 v; v4_set(v, p[0], p[1], p[2], 1.0f);
    m4_mul_vec4(r, m, v);
    if (r[3] != 0.0f) { float inv=1.0f/r[3]; v3_set(o, r[0]*inv, r[1]*inv, r[2]*inv); }
    else v3_set(o, 0, 0, 0);
    return r[3];
}
/* rotate upper-left 3x3 into o (for normals, assuming no shear) */
SDGE_INLINE void m4_mul_vec3_dir(sdg_vec3 o, const sdg_mat4 m, const sdg_vec3 v) {
    o[0]=m[0]*v[0]+m[4]*v[1]+m[8]*v[2];
    o[1]=m[1]*v[0]+m[5]*v[1]+m[9]*v[2];
    o[2]=m[2]*v[0]+m[6]*v[1]+m[10]*v[2];
}
SDGE_INLINE void m4_transpose(sdg_mat4 o, const sdg_mat4 a) {
    sdg_mat4 t;
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            t[r*4+c] = a[c*4+r];
    m4_copy(o, t);
}
/* general inverse (Cramer) - needed for inverse bind matrices, camera inverse */
SDGE_INLINE void m4_invert(sdg_mat4 o, const sdg_mat4 m) {
    float a00=m[0],a01=m[1],a02=m[2],a03=m[3];
    float a10=m[4],a11=m[5],a12=m[6],a13=m[7];
    float a20=m[8],a21=m[9],a22=m[10],a23=m[11];
    float a30=m[12],a31=m[13],a32=m[14],a33=m[15];

    float b00=a00*a11-a01*a10, b01=a00*a12-a02*a10, b02=a00*a13-a03*a10;
    float b03=a01*a12-a02*a11, b04=a01*a13-a03*a11, b05=a02*a13-a03*a12;
    float b06=a20*a31-a21*a30, b07=a20*a32-a22*a30, b08=a20*a33-a23*a30;
    float b09=a21*a32-a22*a31, b10=a21*a33-a23*a31, b11=a22*a33-a23*a32;

    float det = b00*b11 - b01*b10 + b02*b09 + b03*b08 - b04*b07 + b05*b06;
    if (det == 0.0f) { m4_identity(o); return; }
    float inv = 1.0f / det;

    o[0]  = (a11*b11 - a12*b10 + a13*b09) * inv;
    o[1]  = (a02*b10 - a01*b11 - a03*b09) * inv;
    o[2]  = (a31*b05 - a32*b04 + a33*b03) * inv;
    o[3]  = (a22*b04 - a21*b05 - a23*b03) * inv;
    o[4]  = (a12*b08 - a10*b11 - a13*b07) * inv;
    o[5]  = (a00*b11 - a02*b08 + a03*b07) * inv;
    o[6]  = (a32*b02 - a30*b05 - a33*b01) * inv;
    o[7]  = (a20*b05 - a22*b02 + a23*b01) * inv;
    o[8]  = (a10*b10 - a11*b08 + a13*b06) * inv;
    o[9]  = (a01*b08 - a00*b10 - a03*b06) * inv;
    o[10] = (a30*b04 - a31*b02 + a33*b00) * inv;
    o[11] = (a21*b02 - a20*b04 - a23*b00) * inv;
    o[12] = (a11*b07 - a10*b09 - a12*b06) * inv;
    o[13] = (a00*b09 - a01*b07 + a02*b06) * inv;
    o[14] = (a31*b01 - a30*b03 - a32*b00) * inv;
    o[15] = (a20*b03 - a21*b01 + a22*b00) * inv;
}
/* fast inverse for rigid transforms (rotation+translation) */
SDGE_INLINE void m4_invert_rigid(sdg_mat4 o, const sdg_mat4 m) {
    /* transpose 3x3, negate transformed translation */
    o[0]=m[0]; o[1]=m[4]; o[2]=m[8];   o[3]=0;
    o[4]=m[1]; o[5]=m[5]; o[6]=m[9];   o[7]=0;
    o[8]=m[2]; o[9]=m[6]; o[10]=m[10]; o[11]=0;
    o[12]=-(m[0]*m[12]+m[1]*m[13]+m[2]*m[14]);
    o[13]=-(m[4]*m[12]+m[5]*m[13]+m[6]*m[14]);
    o[14]=-(m[8]*m[12]+m[9]*m[13]+m[10]*m[14]);
    o[15]=1;
}
/* right-handed perspective, -Z forward (gluPerspective compatible) */
SDGE_INLINE void m4_perspective(sdg_mat4 o, float fovy_rad, float aspect,
                                float znear, float zfar) {
    float f = 1.0f / tanf(fovy_rad * 0.5f);
    memset(o, 0, sizeof(sdg_mat4));
    o[0] = f / aspect;
    o[5] = f;
    o[10] = (zfar + znear) / (znear - zfar);
    o[11] = -1.0f;
    o[14] = (2.0f * zfar * znear) / (znear - zfar);
}
SDGE_INLINE void m4_ortho(sdg_mat4 o, float l, float r, float b, float t,
                          float zn, float zf) {
    m4_identity(o);
    o[0]  = 2.0f / (r - l);
    o[5]  = 2.0f / (t - b);
    o[10] = -2.0f / (zf - zn);
    o[12] = -(r + l) / (r - l);
    o[13] = -(t + b) / (t - b);
    o[14] = -(zf + zn) / (zf - zn);
}
/* right-handed look-at: eye -> target, up hint */
SDGE_INLINE void m4_look_at(sdg_mat4 o, const sdg_vec3 eye,
                            const sdg_vec3 at, const sdg_vec3 up) {
    sdg_vec3 f, s, u;
    v3_sub(f, at, eye); v3_norm(f, f);
    v3_cross(s, f, up);  v3_norm(s, s);
    v3_cross(u, s, f);
    o[0]= s[0]; o[1]= u[0]; o[2]=-f[0]; o[3]=0;
    o[4]= s[1]; o[5]= u[1]; o[6]=-f[1]; o[7]=0;
    o[8]= s[2]; o[9]= u[2]; o[10]=-f[2]; o[11]=0;
    o[12]= -v3_dot(s, eye);
    o[13]= -v3_dot(u, eye);
    o[14]=  v3_dot(f, eye);
    o[15]= 1;
}
/* compose TRS: o = T * R * S   (glTF node local transform) */
SDGE_INLINE void m4_from_trs(sdg_mat4 o, const sdg_vec3 t, const sdg_quat q,
                             const sdg_vec3 s) {
    float x=q[0], y=q[1], z=q[2], w=q[3];
    float x2=x+x, y2=y+y, z2=z+z;
    float xx=x*x2, xy=x*y2, xz=x*z2, yy=y*y2, yz=y*z2, zz=z*z2, wx=w*x2, wy=w*y2, wz=w*z2;
    o[0]=(1-(yy+zz))*s[0]; o[1]=    (xy+wz)*s[0]; o[2]=     (xz-wy)*s[0];  o[3]=0;
    o[4]=    (xy-wz)*s[1]; o[5]=(1-(xx+zz))*s[1]; o[6]=     (yz+wx)*s[1];  o[7]=0;
    o[8]=    (xz+wy)*s[2]; o[9]=    (yz-wx)*s[2]; o[10]=(1-(xx+yy))*s[2];  o[11]=0;
    o[12]=t[0]; o[13]=t[1]; o[14]=t[2]; o[15]=1;
}

/* ------------------------------------------------------------ culling */
/* extract 6 frustum planes (a,b,c,d each) from a view-projection matrix;
 * plane order: L R B T N F, each (nx,ny,nz,d), n points INWARD */
SDGE_INLINE void frustum_from_vp(float planes[6][4], const sdg_mat4 vp) {
    const float *m = vp;
    /* left  = row3 + row0 */
    planes[0][0]=m[3]+m[0]; planes[0][1]=m[7]+m[4]; planes[0][2]=m[11]+m[8];  planes[0][3]=m[15]+m[12];
    /* right = row3 - row0 */
    planes[1][0]=m[3]-m[0]; planes[1][1]=m[7]-m[4]; planes[1][2]=m[11]-m[8]; planes[1][3]=m[15]-m[12];
    /* bottom = row3 + row1 */
    planes[2][0]=m[3]+m[1]; planes[2][1]=m[7]+m[5]; planes[2][2]=m[11]+m[9];  planes[2][3]=m[15]+m[13];
    /* top    = row3 - row1 */
    planes[3][0]=m[3]-m[1]; planes[3][1]=m[7]-m[5]; planes[3][2]=m[11]-m[9]; planes[3][3]=m[15]-m[13];
    /* near   = row3 + row2 */
    planes[4][0]=m[3]+m[2]; planes[4][1]=m[7]+m[6]; planes[4][2]=m[11]+m[10]; planes[4][3]=m[15]+m[14];
    /* far    = row3 - row2 */
    planes[5][0]=m[3]-m[2]; planes[5][1]=m[7]-m[6]; planes[5][2]=m[11]-m[10]; planes[5][3]=m[15]-m[14];
    for (int i = 0; i < 6; ++i) {
        float n = sqrtf(planes[i][0]*planes[i][0] + planes[i][1]*planes[i][1]
                        + planes[i][2]*planes[i][2]);
        if (n > 1e-12f) {
            planes[i][0]/=n; planes[i][1]/=n; planes[i][2]/=n; planes[i][3]/=n;
        }
    }
}
/* sphere center/radius vs 6 inward planes */
SDGE_INLINE int sphere_in_frustum(const float planes[6][4], const sdg_vec3 c,
                                  float r) {
    for (int i = 0; i < 6; ++i)
        if (planes[i][0]*c[0] + planes[i][1]*c[1] + planes[i][2]*c[2]
            + planes[i][3] < -r)
            return 0;
    return 1;
}

#ifdef __cplusplus
}
#endif
#endif /* SDGE_MATH3D_H */
