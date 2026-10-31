// HIGH TRAIN - Nintendo DSi port
// Port of the three.js "High Train" scene to native DS/DSi hardware (BlocksDS + libnds).
// Top screen: 3D scene + HUD. Bottom screen: touch menu.

#include <nds.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "font8.h"

typedef struct { float x, y, z; } V3;
static inline V3 v3(float x, float y, float z) { V3 r = { x, y, z }; return r; }
static inline V3 vadd(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline V3 vsub(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline V3 vscale(V3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static inline float vdot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline V3 vcross(V3 a, V3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static inline V3 vnorm(V3 a) { float l = sqrtf(vdot(a, a)); return l > 1e-6f ? vscale(a, 1.0f / l) : v3(0, 0, 1); }
static inline V3 vlerp(V3 a, V3 b, float t) { return v3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t); }
static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static inline int ifloor(float x) { int i = (int)x; return (x < (float)i) ? i - 1 : i; }
static inline float smoothf(float a, float b, float x) { x = clampf((x - a) / (b - a), 0, 1); return x * x * (3 - 2 * x); }

/* ------------------------------------------------------------------ random / noise */
static uint32_t rs;
static float rnd(void)
{
    rs += 0x6D2B79F5u;
    uint32_t t = rs;
    t = (t ^ (t >> 15)) * (1u | t);
    t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
    return (float)((t ^ (t >> 14)) >> 8) * (1.0f / 16777216.0f);
}
static uint32_t SOi;
static inline int hashI(int x, int y)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + SOi;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (int)(h & 0xFFF);
}
// value noise in fixed point: input 16.16, output 0..4095
static int vnoiseQ(s32 x, s32 y)
{
    int xi = x >> 16, yi = y >> 16;
    int fxq = (x & 0xFFFF) >> 4, fyq = (y & 0xFFFF) >> 4;
    int u = (((fxq * fxq) >> 12) * (12288 - 2 * fxq)) >> 12;
    int v = (((fyq * fyq) >> 12) * (12288 - 2 * fyq)) >> 12;
    int a = hashI(xi, yi), b = hashI(xi + 1, yi), c = hashI(xi, yi + 1), d = hashI(xi + 1, yi + 1);
    return a + (((b - a) * u) >> 12) + (((c - a) * v) >> 12) + (((a - b - c + d) * ((u * v) >> 12)) >> 12);
}
static int fbmQ(s32 x, s32 y)
{
    int s = 0;
    for (int i = 0; i < 4; i++) {
        s += vnoiseQ(x, y) >> (i + 1);
        x = (s32)(((int64_t)x * 2079) >> 10);
        y = (s32)(((int64_t)y * 2079) >> 10);
    }
    return (s * 1092) >> 10; // / 0.9375
}
static inline s32 q16(float v) { return (s32)(v * 65536.0f); }
static float vnoise(float x, float y) { return vnoiseQ(q16(x), q16(y)) * (1.0f / 4096.0f); }
static float fbm(float x, float y) { return fbmQ(q16(x), q16(y)) * (1.0f / 4096.0f); }

/* ------------------------------------------------------------------ colours / toon */
// Original post shader: luminance -> pow(.72) -> 7 colour blue palette with dithering.
// Here: vertex colour red channel = toon index (palette position), toon table = palette.
static const uint32_t PALHEX[7] = { 0x03060e, 0x0a1428, 0x142a4c, 0x234a7a, 0x3f72aa, 0x7fb0e2, 0xd4ebff };
static uint8_t lumLUT[257];
static uint16_t toonTab[32];
static float hexLum(uint32_t c)
{
    return (0.299f * ((c >> 16) & 255) + 0.587f * ((c >> 8) & 255) + 0.114f * (c & 255)) / 255.0f;
}
static int toonIdx(float l)
{
    if (l <= 0) return 0;
    if (l >= 1) return 31;
    return lumLUT[(int)(l * 256)];
}
static uint16_t palMix(float v) // v = palette position 0..6
{
    v = clampf(v, 0, 6);
    int b = (int)v; if (b > 5) b = 5;
    float f = v - b;
    float r = 0, g = 0, bl = 0;
    uint32_t c0 = PALHEX[b], c1 = PALHEX[b + 1];
    r = ((c0 >> 16) & 255) * (1 - f) + ((c1 >> 16) & 255) * f;
    g = ((c0 >> 8) & 255) * (1 - f) + ((c1 >> 8) & 255) * f;
    bl = (c0 & 255) * (1 - f) + (c1 & 255) * f;
    return RGB15((int)r >> 3, (int)g >> 3, (int)bl >> 3);
}
static void initToon(void)
{
    for (int i = 0; i <= 256; i++) {
        float l = i / 256.0f;
        int v = (int)(powf(l, 0.72f) * 31 + 0.5f);
        lumLUT[i] = v > 31 ? 31 : v;
    }
    for (int i = 0; i < 32; i++) {
        float v = i * 6.0f / 31.0f;
        int b = (int)v; float f = v - b;
        float fq = f < 0.25f ? 0 : (f < 0.75f ? 0.5f : 1.0f);
        toonTab[i] = palMix(b + fq);
    }
}
static uint16_t uiCol(uint32_t hex) { return RGB15(((hex >> 16) & 255) >> 3, ((hex >> 8) & 255) >> 3, (hex & 255) >> 3); }

// lighting (moon directional + hemisphere), returns toon index
static const float LX = -0.5507f, LY = 0.4206f, LZ = 0.7210f;
#define HEMI_SKY 0.337f
#define HEMI_GND 0.031f
static float lambL(float mat, V3 n)
{
    float hemi = HEMI_GND + (HEMI_SKY - HEMI_GND) * (n.y * 0.5f + 0.5f);
    float d = n.x * LX + n.y * LY + n.z * LZ;
    if (d < 0) d = 0;
    return mat * (0.45f * hemi + 0.51f * d);
}
static int lamb(float mat, V3 n) { return toonIdx(lambL(mat, n)); }

/* material luminances */
#define L_SNOW 0.690f
#define L_BALLAST 0.330f
#define L_RAIL 0.470f
#define L_SLEEPER 0.170f
#define L_BRIDGE 0.290f
#define L_TREE 0.130f
#define L_POLE 0.195f
#define L_WIRE 0.450f
#define L_HOUSE 0.260f
#define L_ROOF 0.775f
#define L_LIT 0.910f
#define L_LITDIM 0.540f
#define L_PLATFORM 0.570f
#define L_BODY 0.298f
#define L_BODYDARK 0.110f
#define L_STRIPE 0.740f
#define L_CARROOF 0.630f
#define L_WIN 0.850f
#define L_SHIELD 0.160f
#define L_CABLIT 0.670f
#define L_INWALL 0.310f
#define L_INCEIL 0.480f
#define L_INFLOOR 0.170f
#define L_SEAT 0.210f
#define L_PERSON 0.090f

/* ------------------------------------------------------------------ display list builder */
typedef struct {
    u32 *b; int n, cap, hdr, pk, lastc, ovf;
    float ox, oy, oz, k;
} DL;
#define GXC_COLOR 0x20
#define GXC_TEXCOORD 0x22
#define GXC_VTX16 0x23
#define GXC_POLYFMT 0x29
#define GXC_BEGIN 0x40

static void dl_begin(DL *d, u32 *buf, int cap, float ox, float oy, float oz, float S)
{
    d->b = buf; d->n = 1; d->cap = cap; d->pk = 0; d->lastc = -1; d->ovf = 0;
    d->ox = ox; d->oy = oy; d->oz = oz; d->k = 4096.0f / S;
}
static inline void dl_c(DL *d, u32 c)
{
    if (d->pk == 0) { d->hdr = d->n; d->b[d->n++] = 0; }
    d->b[d->hdr] |= c << (8 * d->pk);
    d->pk = (d->pk + 1) & 3;
}
static inline int dl_room(DL *d, int w) { if (d->n + w + 8 > d->cap) { d->ovf = 1; return 0; } return 1; }
static void dl_poly(DL *d, u32 attr) { if (!dl_room(d, 2)) return; dl_c(d, GXC_POLYFMT); d->b[d->n++] = attr; }
static void dl_beginp(DL *d, int type) { if (!dl_room(d, 2)) return; dl_c(d, GXC_BEGIN); d->b[d->n++] = type; d->lastc = -1; }
static inline void dl_col(DL *d, int idx)
{
    if (idx == d->lastc) return;
    if (!dl_room(d, 2)) return;
    d->lastc = idx;
    dl_c(d, GXC_COLOR); d->b[d->n++] = RGB15(idx, idx, idx);
}
static inline void dl_tex(DL *d, int s, int t)
{
    if (!dl_room(d, 2)) return;
    dl_c(d, GXC_TEXCOORD); d->b[d->n++] = ((u32)(s * 16) & 0xFFFF) | ((u32)(t * 16) << 16);
}
static inline int16_t qs16(float v) { int i = (int)v; if (i > 32767) i = 32767; if (i < -32768) i = -32768; return (int16_t)i; }
static inline void dl_v(DL *d, float x, float y, float z)
{
    if (!dl_room(d, 3)) return;
    dl_c(d, GXC_VTX16);
    int16_t X = qs16((x - d->ox) * d->k), Y = qs16((y - d->oy) * d->k), Z = qs16((z - d->oz) * d->k);
    d->b[d->n++] = ((u32)(uint16_t)X) | ((u32)(uint16_t)Y << 16);
    d->b[d->n++] = (uint16_t)Z;
}
static inline void dl_vv(DL *d, V3 p) { dl_v(d, p.x, p.y, p.z); }
static u32 *dl_finish(DL *d)
{
    if (d->n <= 1) return NULL;
    d->b[0] = d->n - 1;
    u32 *m = malloc(d->n * 4);
    if (!m) return NULL;
    memcpy(m, d->b, d->n * 4);
    DC_FlushRange(m, d->n * 4);
    return m;
}
static void dl_finish_static(DL *d) { d->b[0] = d->n - 1; DC_FlushRange(d->b, d->n * 4); }

// quad (a,b,c,d counter-clockwise seen from the front)
static int FLIPW = 0;
static void dl_quad(DL *d, V3 a, V3 b, V3 c, V3 e)
{
    if (FLIPW) { dl_vv(d, a); dl_vv(d, e); dl_vv(d, c); dl_vv(d, b); }
    else { dl_vv(d, a); dl_vv(d, b); dl_vv(d, c); dl_vv(d, e); }
}
// a face of a box/plane with center c, half axes u, v (u x v = normal)
static void dl_face(DL *d, V3 c, V3 u, V3 v, int col)
{
    dl_col(d, col);
    dl_quad(d, vsub(vsub(c, u), v), vsub(vadd(c, u), v), vadd(vadd(c, u), v), vadd(vsub(c, u), v));
}
// faces mask: 1:+x 2:-x 4:+y 8:-y 16:+z 32:-z.  Box rotated around y by ry. Must be inside a GL_QUADS block.
static void dl_box(DL *d, float mat, int lit, float w, float h, float dd, V3 c, float ry, int faces)
{
    float cs = cosf(ry), sn = sinf(ry);
    V3 X = v3(cs, 0, -sn), Y = v3(0, 1, 0), Z = v3(sn, 0, cs);
    V3 hx = vscale(X, w * 0.5f), hy = vscale(Y, h * 0.5f), hz = vscale(Z, dd * 0.5f);
#define COLF(n) (lit ? toonIdx(mat) : lamb(mat, n))
    if (faces & 1) dl_face(d, vadd(c, hx), vscale(hz, -1), hy, COLF(X));
    if (faces & 2) dl_face(d, vsub(c, hx), hz, hy, COLF(vscale(X, -1)));
    if (faces & 4) dl_face(d, vadd(c, hy), hx, vscale(hz, -1), COLF(Y));
    if (faces & 8) dl_face(d, vsub(c, hy), hx, hz, COLF(vscale(Y, -1)));
    if (faces & 16) dl_face(d, vadd(c, hz), hx, hy, COLF(Z));
    if (faces & 32) dl_face(d, vsub(c, hz), vscale(hx, -1), hy, COLF(vscale(Z, -1)));
#undef COLF
}
// a plane (three.js PlaneGeometry) w x h at c rotated by ry, facing +z rotated
static void dl_plane(DL *d, int col, float w, float h, V3 c, float ry)
{
    float cs = cosf(ry), sn = sinf(ry);
    dl_face(d, c, v3(cs * w * 0.5f, 0, -sn * w * 0.5f), v3(0, h * 0.5f, 0), col);
}
static void dl_hplane(DL *d, int col, float w, float dd, V3 c, int up)
{
    if (up) dl_face(d, c, v3(w * 0.5f, 0, 0), v3(0, 0, -dd * 0.5f), col);
    else dl_face(d, c, v3(w * 0.5f, 0, 0), v3(0, 0, dd * 0.5f), col);
}

#define PF_OPAQUE (POLY_ALPHA(31) | POLY_CULL_BACK | POLY_FOG | POLY_TOON_HIGHLIGHT | POLY_ID(0))
#define PF_OPAQUE_NC (POLY_ALPHA(31) | POLY_CULL_NONE | POLY_FOG | POLY_TOON_HIGHLIGHT | POLY_ID(0))
#define PF_TRANS(a, id) (POLY_ALPHA(a) | POLY_CULL_NONE | POLY_TOON_HIGHLIGHT | POLY_ID(id))
#define PF_TRANSF(a, id) (POLY_ALPHA(a) | POLY_CULL_NONE | POLY_FOG | POLY_TOON_HIGHLIGHT | POLY_ID(id))

/* ------------------------------------------------------------------ procedural track */
typedef struct { float x, y, z, h, v, rx, rz; } TPt;
#define TRN 4096
static TPt trk[TRN];
static int trEnd = 0; // absolute index of next point
static struct { float x, y, z, h, c, ct, seg, g, gt, gseg; } TR;

static void genPoint(void)
{
    if (TR.seg <= 0) {
        if (rnd() < 0.36f) { TR.ct = 0; TR.seg = 60 + rnd() * 240; }
        else { float R = 240 + rnd() * 520; TR.ct = (rnd() < 0.5f ? -1.0f : 1.0f) / R; TR.seg = 80 + rnd() * 260; }
    }
    TR.seg--; TR.c += (TR.ct - TR.c) * 0.012f; TR.h += TR.c;
    if (TR.gseg <= 0) { TR.gt = (rnd() - 0.5f) * 0.036f; TR.gseg = 120 + rnd() * 320; }
    TR.gseg--;
    if (TR.y > 30 && TR.gt > 0) TR.gt = -TR.gt;
    if (TR.y < -30 && TR.gt < 0) TR.gt = -TR.gt;
    TR.g += (TR.gt - TR.g) * 0.008f;
    float sh = sinf(TR.h), ch = cosf(TR.h);
    TR.x += sh; TR.z += ch; TR.y += TR.g;
    TPt *p = &trk[trEnd & (TRN - 1)];
    float vn = vnoise(trEnd * 0.0032f + 11.3f, 4.1f);
    p->x = TR.x; p->y = TR.y; p->z = TR.z; p->h = TR.h; p->v = smoothf(0.64f, 0.74f, vn);
    p->rx = ch; p->rz = -sh;
    trEnd++;
}
static inline const TPt *P(int i)
{
    int lo = trEnd - TRN + 1; if (lo < 0) lo = 0;
    if (i < lo) i = lo;
    if (i > trEnd - 1) i = trEnd - 1;
    return &trk[i & (TRN - 1)];
}
static void sample(float d, TPt *o)
{
    int i = ifloor(d); float f = d - i;
    const TPt *a = P(i), *b = P(i + 1);
    o->x = a->x + (b->x - a->x) * f; o->y = a->y + (b->y - a->y) * f; o->z = a->z + (b->z - a->z) * f;
    o->h = a->h + (b->h - a->h) * f; o->v = a->v + (b->v - a->v) * f;
    o->rx = a->rx + (b->rx - a->rx) * f; o->rz = a->rz + (b->rz - a->rz) * f;
}
static float groundH(float py, float pv, float l, float wx, float wz)
{
    float al = fabsf(l);
    float h = py - 0.95f;
    if (al > 4) {
        float n = fbm(wx * 0.0055f, wz * 0.0055f);
        float amp = 14 + 50 * vnoise(wx * 0.0012f + 31, wz * 0.0012f - 7);
        h += (n - 0.4f) * amp * 1.6f * smoothf(4, 75, al);
    }
    if (pv > 0) h -= pv * 30 * (1 - smoothf(25, 130, al));
    return h;
}
static V3 TP(float d, float l, float u)
{
    TPt s; sample(d, &s);
    return v3(s.x + s.rx * l, s.y + u, s.z + s.rz * l);
}
static V3 TPG(float d, float l, float u)
{
    TPt s; sample(d, &s);
    float x = s.x + s.rx * l, z = s.z + s.rz * l;
    float y = s.y + u;
    if (fabsf(l) > 2.6f) {
        float g = groundH(s.y, s.v, l, x, z) + fmaxf(2.2f, u * 0.7f);
        if (g > y) y = g;
    }
    return v3(x, y, z);
}

/* ------------------------------------------------------------------ chunks */
#define CH 60
#define NCHUNK 20
typedef struct {
    int k, used, ready;
    float ox, oy, oz;  // list origin
    V3 c;              // centre for culling
    u32 *lNear, *lFar, *lTree, *lDet, *lTrans;
} Chunk;
static Chunk chunks[NCHUNK];
static int nextChunk = 0;

static const float TL[] = { -120, -82, -56, -38, -26, -17, -10, -5.5f, -3, 3, 5.5f, 10, 17, 26, 38, 56, 82, 120 };
#define NTL 18
#define FSTEP 5
#define NROWF (CH / FSTEP + 1)
static float hgrid[NROWF + 2][NTL];
static uint8_t cgrid[NROWF + 2][NTL];

#define BUFN 7000
#define BUFF 3000
#define BUFT 3000
#define BUFD 5000
#define BUFX 800
static u32 bufNear[BUFN], bufFar[BUFF], bufTree[BUFT], bufDet[BUFD], bufTr[BUFX];
static DL dN, dF, dT, dD, dX;

static int treeTexParam = 0;

typedef struct { int active, k, stage, station; Chunk *ch; } Builder;
static Builder bld;
static u32 stageMax[20];

static Chunk *chunkSlot(void)
{
    for (int i = 0; i < NCHUNK; i++) if (!chunks[i].used) return &chunks[i];
    return NULL;
}
static void freeList(u32 **l) { if (*l) { free(*l); *l = NULL; } }
static void disposeChunk(Chunk *c)
{
    freeList(&c->lNear); freeList(&c->lFar); freeList(&c->lTree); freeList(&c->lDet); freeList(&c->lTrans);
    c->used = 0; c->ready = 0;
}

static void terrainStrip(DL *d, int rstep)
{
    // rows of the height grid; row r <-> track index i0 + (r-1)*FSTEP
    int i0 = bld.k * CH;
    dl_poly(d, PF_OPAQUE);
    for (int j = 0; j < NTL - 1; j++) {
        dl_beginp(d, GL_QUAD_STRIP);
        for (int r = 1; r <= NROWF; r += rstep) {
            int i = i0 + (r - 1) * FSTEP;
            const TPt *p = P(i);
            for (int q = 0; q < 2; q++) {
                int jj = j + (FLIPW ? 1 - q : q);
                float l = TL[jj];
                float h = hgrid[r][jj];
                // normal by central differences
                dl_col(d, cgrid[r][jj]);
                dl_v(d, p->x + p->rx * l, h, p->z + p->rz * l);
            }
        }
    }
}

static void buildStageTerrain(int ra, int rb)
{
    int i0 = bld.k * CH;
    if (rb > NROWF + 2) rb = NROWF + 2;
    for (int r = ra; r < rb; r++) {
        int i = i0 + (r - 1) * FSTEP;
        const TPt *p = P(i);
        for (int j = 0; j < NTL; j++) {
            float l = TL[j];
            hgrid[r][j] = groundH(p->y, p->v, l, p->x + p->rx * l, p->z + p->rz * l);
        }
    }
}
static void buildStageColors(int ra, int rb)
{
    int i0 = bld.k * CH;
    if (rb > NROWF + 1) rb = NROWF + 1;
    for (int r = ra; r < rb; r++) {
        const TPt *p = P(i0 + (r - 1) * FSTEP);
        for (int jj = 0; jj < NTL; jj++) {
            float hl = hgrid[r][jj > 0 ? jj - 1 : jj], hr = hgrid[r][jj < NTL - 1 ? jj + 1 : jj];
            float dl = TL[jj < NTL - 1 ? jj + 1 : jj] - TL[jj > 0 ? jj - 1 : jj];
            float hb = hgrid[r - 1][jj], hf = hgrid[r + 1][jj];
            float gx = (hr - hl) / dl, gz = (hf - hb) * (1.0f / (2.0f * FSTEP));
            // local lateral axis = (rx,rz), forward = (-rz, rx)
            V3 n = vnorm(v3(-gx * p->rx + gz * p->rz, 1, -gx * p->rz - gz * p->rx));
            cgrid[r][jj] = lamb(L_SNOW, n);
        }
    }
}
static void buildSetup(void)
{
    int i0 = bld.k * CH;
    const TPt *o = P(i0 + CH / 2);
    Chunk *c = bld.ch;
    c->ox = o->x; c->oy = o->y; c->oz = o->z;
    c->c = v3(o->x, o->y, o->z);
    dl_begin(&dN, bufNear, BUFN, c->ox, c->oy, c->oz, 32);
    dl_begin(&dF, bufFar, BUFF, c->ox, c->oy, c->oz, 32);
    dl_begin(&dT, bufTree, BUFT, c->ox, c->oy, c->oz, 32);
    dl_begin(&dD, bufDet, BUFD, c->ox, c->oy, c->oz, 32);
    dl_begin(&dX, bufTr, BUFX, c->ox, c->oy, c->oz, 32);
}

static void ribbonQuads(DL *d, int a, int b, int st, float l0, float l1, float y0off, float y1off, int bridgeOnly, int col)
{
    // vertical-ish/horizontal strip between lateral l0 (height y+y0off) and l1 (y+y1off)
    dl_col(d, col);
    for (int i = a; i < b; i += st) {
        const TPt *p = P(i), *q = P(i + st);
        if (bridgeOnly && (p->v <= 0.15f || q->v <= 0.15f)) continue;
        V3 A = v3(p->x + p->rx * l0, p->y + y0off, p->z + p->rz * l0);
        V3 B = v3(p->x + p->rx * l1, p->y + y1off, p->z + p->rz * l1);
        V3 C = v3(q->x + q->rx * l1, q->y + y1off, q->z + q->rz * l1);
        V3 E = v3(q->x + q->rx * l0, q->y + y0off, q->z + q->rz * l0);
        dl_quad(d, A, E, C, B);
    }
}

static void houseInto(DL *d, V3 pos, float ry, float w, float hh, float dd, int wside, int nwin, float wseed[2])
{
    dl_beginp(d, GL_QUADS);
    V3 c = v3(pos.x, pos.y + hh * 0.5f, pos.z);
    dl_box(d, L_HOUSE, 0, w, hh, dd, c, ry, 1 | 2 | 16 | 32);
    // gable roof: prism along local z
    float cs = cosf(ry), sn = sinf(ry);
    V3 X = v3(cs, 0, -sn), Z = v3(sn, 0, cs);
    float rw = w * 0.62f * 0.866f, rh = w * 0.42f * 0.75f;
    float base = pos.y + hh;
    float hz = (dd + 0.6f) * 0.5f;
    V3 apexA = vadd(v3(pos.x, base + rh, pos.z), vscale(Z, hz)), apexB = vadd(v3(pos.x, base + rh, pos.z), vscale(Z, -hz));
    V3 l0 = vadd(vadd(v3(pos.x, base - 0.15f, pos.z), vscale(X, -rw)), vscale(Z, hz));
    V3 l1 = vadd(vadd(v3(pos.x, base - 0.15f, pos.z), vscale(X, -rw)), vscale(Z, -hz));
    V3 r0 = vadd(vadd(v3(pos.x, base - 0.15f, pos.z), vscale(X, rw)), vscale(Z, hz));
    V3 r1 = vadd(vadd(v3(pos.x, base - 0.15f, pos.z), vscale(X, rw)), vscale(Z, -hz));
    V3 nr = vnorm(vadd(vscale(X, rh), v3(0, rw, 0))), nl = vnorm(vadd(vscale(X, -rh), v3(0, rw, 0)));
    dl_col(d, lamb(L_ROOF, nr)); dl_quad(d, r0, r1, apexB, apexA);
    dl_col(d, lamb(L_ROOF, nl)); dl_quad(d, l1, l0, apexA, apexB);
    // windows on side facing the track
    for (int q = 0; q < nwin; q++) {
        if (wseed[q] < 0.35f) continue;
        V3 wc = vadd(vadd(v3(pos.x, pos.y + hh * 0.35f, pos.z), vscale(X, wside * (w * 0.5f + 0.03f))), vscale(Z, (q - 0.5f) * dd * 0.45f));
        dl_col(d, toonIdx(wseed[q] < 0.75f ? L_LIT : L_LITDIM));
        V3 u = vscale(Z, 0.45f * (wside > 0 ? -1 : 1)), v = v3(0, 0.4f, 0);
        dl_quad(d, vsub(vsub(wc, u), v), vsub(vadd(wc, u), v), vadd(vadd(wc, u), v), vadd(vsub(wc, u), v));
    }
    dl_beginp(d, GL_TRIANGLES);
    dl_col(d, lamb(L_HOUSE, Z));
    if (FLIPW) { dl_vv(d, r0); dl_vv(d, apexA); dl_vv(d, l0); } else { dl_vv(d, l0); dl_vv(d, apexA); dl_vv(d, r0); }
    dl_col(d, lamb(L_HOUSE, vscale(Z, -1)));
    if (FLIPW) { dl_vv(d, l1); dl_vv(d, apexB); dl_vv(d, r1); } else { dl_vv(d, r1); dl_vv(d, apexB); dl_vv(d, l1); }
}

static void buildBallast(int t)
{
    int i0 = bld.k * CH, i1 = i0 + CH;
    DL *ds[2] = { &dN, &dF };
    {
        DL *d = ds[t];
        int st = t ? 12 : 4;
        dl_poly(d, PF_OPAQUE);
        dl_beginp(d, GL_QUADS);
        for (int i = i0; i < i1; i += st) {
            const TPt *p = P(i), *q = P(i + st);
            float lat[4] = { -2.5f, -1.55f, 1.55f, 2.5f };
            float hp[4], hq[4];
            for (int j = 0; j < 4; j++) {
                hp[j] = (j == 0 || j == 3) ? p->y - (p->v > 0.15f ? 1.9f : 0.95f) : p->y + 0.06f;
                hq[j] = (j == 0 || j == 3) ? q->y - (q->v > 0.15f ? 1.9f : 0.95f) : q->y + 0.06f;
            }
            for (int j = 0; j < 3; j++) {
                V3 A = v3(p->x + p->rx * lat[j], hp[j], p->z + p->rz * lat[j]);
                V3 B = v3(p->x + p->rx * lat[j + 1], hp[j + 1], p->z + p->rz * lat[j + 1]);
                V3 C = v3(q->x + q->rx * lat[j + 1], hq[j + 1], q->z + q->rz * lat[j + 1]);
                V3 E = v3(q->x + q->rx * lat[j], hq[j], q->z + q->rz * lat[j]);
                V3 n = j == 1 ? v3(0, 1, 0) : (j == 0 ? v3(-p->rx * 0.7f, 0.71f, -p->rz * 0.7f) : v3(p->rx * 0.7f, 0.71f, p->rz * 0.7f));
                dl_col(d, lamb(L_BALLAST, n));
                dl_quad(d, A, E, C, B);
            }
        }
        // bridge parapets
        int any = 0;
        for (int i = i0; i <= i1; i += 4) if (P(i)->v > 0.15f) { any = 1; break; }
        if (any) {
            int sp = t ? 8 : 4;
            V3 nrm = v3(P(i0)->rx, 0, P(i0)->rz);
            dl_poly(d, PF_OPAQUE_NC);
            dl_beginp(d, GL_QUADS);
            ribbonQuads(d, i0, i1, sp, -2.55f, -2.55f, -1.95f, 0.75f, 1, lamb(L_BRIDGE, vscale(nrm, -1)));
            ribbonQuads(d, i0, i1, sp, 2.55f, 2.55f, 0.75f, -1.95f, 1, lamb(L_BRIDGE, nrm));
            dl_poly(d, PF_OPAQUE);
            dl_beginp(d, GL_QUADS);
            for (int i = ((i0 + 15) / 16) * 16; i < i1; i += 16) {
                const TPt *p = P(i);
                if (p->v > 0.3f) {
                    float gh = groundH(p->y, p->v, 0, p->x, p->z) - 2, top = p->y - 1.9f;
                    if (top - gh > 1) dl_box(d, L_BRIDGE, 0, 3.4f, top - gh, 2.2f, v3(p->x, (gh + top) * 0.5f, p->z), p->h, 1 | 2 | 16 | 32);
                }
            }
        }
    }
}
static void buildStationHouses(void)
{
    int i0 = bld.k * CH, i1 = i0 + CH;
    DL *ds[2] = { &dN, &dF };
    // station?
    int station = 0;
    if (bld.k % 26 == 12) {
        station = 1;
        for (int i = i0; i <= i1; i += 3) if (P(i)->v > 0) { station = 0; break; }
    }
    bld.station = station;
    if (station) {
        int a = i0 + 4, b = i0 + 56;
        for (int t = 0; t < 2; t++) {
            DL *d = ds[t];
            dl_poly(d, PF_OPAQUE);
            dl_beginp(d, GL_QUADS);
            // platform: front face, top
            ribbonQuads(d, a, b, 4, 2.0f, 2.0f, -0.95f, 1.0f, 0, lamb(L_PLATFORM, v3(-P(a)->rx, 0, -P(a)->rz)));
            ribbonQuads(d, a, b, 4, 6.8f, 2.0f, 1.0f, 1.0f, 0, lamb(L_PLATFORM, v3(0, 1, 0)));
            ribbonQuads(d, a, b, 4, 2.4f, 2.02f, 1.025f, 1.025f, 0, toonIdx(L_LIT));
            // lamps
            for (int i = a + 6; i < b; i += 15) {
                const TPt *p = P(i);
                float l = 6.0f, x = p->x + p->rx * l, z = p->z + p->rz * l;
                dl_box(d, L_POLE, 0, 0.14f, 3.6f, 0.14f, v3(x, p->y + 1 + 1.8f, z), p->h, 1 | 2 | 16 | 32);
                dl_box(d, L_LIT, 1, 0.7f, 0.18f, 0.35f, v3(x - p->rx * 0.3f, p->y + 4.5f, z - p->rz * 0.3f), p->h, 63);
            }
            // shelter
            const TPt *pm = P(i0 + 30);
            float sx = pm->x + pm->rx * 5.4f, sz = pm->z + pm->rz * 5.4f;
            dl_box(d, L_HOUSE, 0, 2.2f, 2.7f, 8, v3(sx, pm->y + 1 + 1.35f, sz), pm->h, 1 | 2 | 16 | 32);
            dl_box(d, L_ROOF, 0, 2.8f, 0.25f, 8.6f, v3(sx, pm->y + 3.82f, sz), pm->h, 63);
            dl_box(d, L_LIT, 1, 0.05f, 0.8f, 2.5f, v3(sx - pm->rx * 1.12f, pm->y + 2.4f, sz - pm->rz * 1.12f), pm->h, 3);
            const TPt *ps = P(i0 + 44);
            float gx = ps->x + ps->rx * 6.4f, gz = ps->z + ps->rz * 6.4f;
            dl_box(d, L_LIT, 1, 0.1f, 0.6f, 2.6f, v3(gx, ps->y + 3.5f, gz), ps->h, 3 | 16 | 32);
            dl_box(d, L_POLE, 0, 0.1f, 2.3f, 0.1f, v3(gx, ps->y + 2.15f, gz), ps->h, 3 | 16 | 32);
        }
        // light pools (translucent)
        dl_poly(&dX, PF_TRANSF(11, 20));
        dl_beginp(&dX, GL_TRIANGLES);
        dl_col(&dX, 22);
        for (int i = a + 6; i < b; i += 15) {
            const TPt *p = P(i);
            float l = 4.8f, x = p->x + p->rx * l, z = p->z + p->rz * l, y = p->y + 1.04f;
            for (int s = 0; s < 8; s++) {
                float a0 = s * 0.7854f, a1 = (s + 1) * 0.7854f;
                V3 C = v3(x, y, z), A = v3(x + cosf(a0) * 3.2f, y, z + sinf(a0) * 3.2f), B = v3(x + cosf(a1) * 3.2f, y, z + sinf(a1) * 3.2f);
                dl_vv(&dX, C); dl_vv(&dX, A); dl_vv(&dX, B);
            }
        }
    }
    // houses
    if (!station && rnd() < 0.3f) {
        int i = i0 + 8 + (int)(rnd() * (CH - 16));
        int side = rnd() < 0.5f ? -1 : 1;
        float lc = side * (18 + rnd() * 50);
        int nH = 1 + (int)(rnd() * 4);
        for (int h = 0; h < nH; h++) {
            int ii = i + (int)((rnd() - 0.5f) * 30);
            if (ii > i1) ii = i1;
            if (ii < i0) ii = i0;
            const TPt *p = P(ii);
            float l = lc + (rnd() - 0.5f) * 20;
            if (fabsf(l) < 12) continue;
            float wx = p->x + p->rx * l, wz = p->z + p->rz * l, gy = groundH(p->y, p->v, l, wx, wz) - 0.3f;
            float w = 4 + rnd() * 3, dd = 5 + rnd() * 4, hh = 2.8f + rnd() * 1.6f;
            float ry = p->h + ((rnd() < 0.3f) ? 1.5708f : 0);
            float ws[2] = { rnd(), rnd() };
            // window side faces the track: local x axis direction relative to track
            float cs = cosf(ry), sn = sinf(ry);
            float dotx = cs * p->rx + (-sn) * p->rz; // local X vs lateral
            int wside = (l > 0) ? (dotx > 0 ? -1 : 1) : (dotx > 0 ? 1 : -1);
            houseInto(&dN, v3(wx, gy, wz), ry, w, hh, dd, wside, 2, ws);
            houseInto(&dF, v3(wx, gy, wz), ry, w, hh, dd, wside, 2, ws);
        }
    }
}

static int treeCnt;
static void buildStageTrees(int part)
{
    int i0 = bld.k * CH;
    DL *d = &dT;
    if (part == 0) {
        dl_poly(d, PF_OPAQUE_NC);
        dl_beginp(d, GL_QUADS);
        treeCnt = 0;
    }
    int col = toonIdx(L_TREE * 0.9f + 0.05f);
    int cnt = treeCnt;
    for (int n = part * 20; n < part * 20 + 20 && cnt < 42; n++) {
        int i = i0 + (int)(rnd() * CH);
        const TPt *p = P(i);
        int side = rnd() < 0.5f ? -1 : 1;
        float l = side * (9 + powf(rnd(), 1.4f) * 115);
        if (bld.station && side > 0 && fabsf(l) < 14) continue;
        float wx = p->x + p->rx * l, wz = p->z + p->rz * l;
        if (fbm(wx * 0.012f + 5, wz * 0.012f - 3) < 0.46f) continue;
        float s = 0.7f + rnd() * 0.9f;
        float y = groundH(p->y, p->v, l, wx, wz) - 0.2f;
        float hw = 2.3f * s, hh = 8.2f * s, a = rnd() * 3.1416f;
        float cs = cosf(a), sn = sinf(a);
        dl_col(d, col);
        for (int q = 0; q < 2; q++) {
            float ux = q ? -sn : cs, uz = q ? cs : sn;
            dl_tex(d, 0, 64); dl_v(d, wx - ux * hw, y, wz - uz * hw);
            dl_tex(d, 32, 64); dl_v(d, wx + ux * hw, y, wz + uz * hw);
            dl_tex(d, 32, 0); dl_v(d, wx + ux * hw, y + hh, wz + uz * hw);
            dl_tex(d, 0, 0); dl_v(d, wx - ux * hw, y + hh, wz - uz * hw);
        }
        cnt++;
    }
    treeCnt = cnt;
}

static void buildDetRails(void)
{
    int i0 = bld.k * CH, i1 = i0 + CH;
    DL *d = &dD;
    dl_poly(d, PF_OPAQUE);
    dl_beginp(d, GL_QUADS);
    // rails: top + inner side
    int ctop = toonIdx(lambL(L_RAIL, v3(0, 1, 0)) * 1.4f);
    for (int r = 0; r < 2; r++) {
        float lo = r ? 0.7f : -0.8f, li = r ? 0.8f : -0.7f;
        ribbonQuads(d, i0, i1, 3, lo, li, 0.3f, 0.3f, 0, ctop);
        float lin = r ? 0.7f : -0.7f;
        // inner vertical side
        if (r) ribbonQuads(d, i0, i1, 3, lin, lin, 0.12f, 0.3f, 0, toonIdx(L_RAIL * 0.25f));
        else ribbonQuads(d, i0, i1, 3, lin, lin, 0.3f, 0.12f, 0, toonIdx(L_RAIL * 0.25f));
    }
}
static void buildDetSleepers(void)
{
    int i0 = bld.k * CH, i1 = i0 + CH;
    DL *d = &dD;
    // sleepers (top faces)
    dl_col(d, lamb(L_SLEEPER, v3(0, 1, 0)));
    for (float dd = i0; dd < i1; dd += 1.7f) {
        TPt s; sample(dd, &s);
        V3 c = v3(s.x, s.y + 0.14f, s.z);
        V3 u = v3(s.rx * 1.25f, 0, s.rz * 1.25f), f = v3(-s.rz * 0.13f, 0, s.rx * 0.13f);
        dl_quad(d, vsub(vsub(c, u), f), vadd(vsub(c, u), f), vadd(vadd(c, u), f), vsub(vadd(c, u), f));
    }
}
static void buildDetPoles(void)
{
    int i0 = bld.k * CH, i1 = i0 + CH;
    DL *d = &dD;
    // telegraph poles + wires
    V3 prev[2]; int hasPrev = 0;
    V3 wires[64]; int nw = 0;
    for (int i = ((i0 + 29) / 30) * 30; i <= i1; i += 30) {
        const TPt *p = P(i);
        if (p->v > 0.05f) { hasPrev = 0; continue; }
        float l = 4.2f, wx = p->x + p->rx * l, wz = p->z + p->rz * l, gy = groundH(p->y, p->v, l, wx, wz);
        dl_box(d, L_POLE, 0, 0.22f, 7.6f, 0.22f, v3(wx, gy + 3.8f, wz), p->h, 1 | 2 | 16 | 32);
        dl_box(d, L_POLE, 0, 1.5f, 0.12f, 0.12f, v3(wx, gy + 6.9f, wz), p->h, 4 | 8 | 16 | 32);
        V3 tops[2] = { v3(wx - p->rx * 0.65f, gy + 7.0f, wz - p->rz * 0.65f), v3(wx + p->rx * 0.65f, gy + 7.0f, wz + p->rz * 0.65f) };
        if (hasPrev) {
            for (int w = 0; w < 2; w++) {
                V3 A = prev[w], B = tops[w];
                for (int s = 0; s < 6 && nw < 62; s++) {
                    float t0 = s / 6.0f, t1 = (s + 1) / 6.0f;
                    V3 a = vlerp(A, B, t0), b = vlerp(A, B, t1);
                    a.y -= 1.1f * 4 * t0 * (1 - t0); b.y -= 1.1f * 4 * t1 * (1 - t1);
                    wires[nw++] = a; wires[nw++] = b;
                }
            }
        }
        prev[0] = tops[0]; prev[1] = tops[1]; hasPrev = 1;
    }
    if (nw) {
        dl_poly(d, PF_OPAQUE_NC);
        dl_beginp(d, GL_TRIANGLES);
        dl_col(d, toonIdx(L_WIRE * 0.55f));
        for (int w = 0; w < nw; w += 2) { dl_vv(d, wires[w]); dl_vv(d, wires[w + 1]); dl_vv(d, wires[w + 1]); }
    }
}

static void buildFinish(void)
{
    Chunk *c = bld.ch;
    c->lNear = dl_finish(&dN);
    c->lFar = dl_finish(&dF);
    c->lTree = dl_finish(&dT);
    c->lDet = dl_finish(&dD);
    c->lTrans = dl_finish(&dX);
    c->ready = 1;
}

static int builderStep(void) // returns 1 if work done
{
    if (!bld.active) return 0;
    u32 st0 = cpuGetTiming();
    int stg = bld.stage;
    switch (bld.stage) {
    case 0: buildSetup(); buildStageTerrain(0, 4); break;
    case 1: buildStageTerrain(4, 8); break;
    case 2: buildStageTerrain(8, 12); break;
    case 3: buildStageTerrain(12, 15); break;
    case 4: buildStageColors(1, 5); break;
    case 5: buildStageColors(5, 9); break;
    case 6: buildStageColors(9, 14); break;
    case 7: terrainStrip(&dN, 1); break;
    case 8: terrainStrip(&dF, 3); break;
    case 9: buildBallast(0); break;
    case 10: buildBallast(1); break;
    case 11: buildStationHouses(); break;
    case 12: buildStageTrees(0); break;
    case 13: buildStageTrees(1); break;
    case 14: buildStageTrees(2); break;
    case 15: buildDetRails(); break;
    case 16: buildDetSleepers(); break;
    case 17: buildDetPoles(); break;
    case 18: buildFinish(); bld.active = 0; break;
    }
    { u32 dtt = (cpuGetTiming() - st0) / 33; if (dtt > stageMax[stg]) stageMax[stg] = dtt; }
    bld.stage++;
    return 1;
}

/* ------------------------------------------------------------------ world state */
static float D = 420, speed = 24, speedT = 24;
static const float SPEEDS[3] = { 11, 24, 40 };
static int spI = 1;
static float T = 0;

static void streamWorld(int force)
{
    while (trEnd < (int)D + 560) genPoint();
    // dispose
    for (int i = 0; i < NCHUNK; i++)
        if (chunks[i].used && (chunks[i].k + 1) * CH < D - 320) disposeChunk(&chunks[i]);
    do {
        if (!bld.active && nextChunk * CH < D + 440) {
            Chunk *c = chunkSlot();
            if (c) {
                memset(c, 0, sizeof(*c));
                c->used = 1; c->k = nextChunk;
                bld.active = 1; bld.k = nextChunk; bld.stage = 0; bld.ch = c;
                nextChunk++;
            }
        }
        if (!builderStep()) break;
    } while (force);
}

/* ------------------------------------------------------------------ train */
typedef struct {
    float L; int type;
    u32 *ext, *wins, *interior;
    V3 pos; V3 X, Y, Z; // world basis
} Car;
#define NCARS 5
static Car cars[NCARS];
#define SEATCAR (&cars[2])
#define GAP 1.1f
static float TRAINLEN;
static u32 *beamList;
static const float BAY = 2.3f;

static u32 cbuf[9000];

static void running(DL *d, float L)
{
    for (int sz = -1; sz <= 1; sz += 2) {
        float bz = sz * (L / 2 - 2.6f);
        dl_box(d, L_BODYDARK, 0, 2.0f, 0.5f, 2.9f, v3(0, 0.85f, bz), 0, 1 | 2 | 16 | 32);
        for (int ax = -1; ax <= 1; ax += 2)
            for (int sx = -1; sx <= 1; sx += 2) {
                V3 c = v3(sx * 0.86f, 0.75f, bz + ax * 0.9f);
                dl_face(d, c, v3(0, 0, sx * 0.45f), v3(0, 0.45f, 0), lamb(L_BODYDARK * 1.3f, v3((float)sx, 0, 0)));
            }
    }
    dl_box(d, L_BODYDARK, 0, 2.3f, 0.45f, L - 5.6f, v3(0, 1.18f, 0), 0, 1 | 2 | 16 | 32);
}

static void makeLoco(Car *c)
{
    float L = 15; c->L = L; c->type = 0;
    DL d; dl_begin(&d, cbuf, 9000, 0, 0, 0, 4);
    dl_poly(&d, PF_OPAQUE);
    dl_beginp(&d, GL_QUADS);
    dl_box(&d, L_BODY, 0, 2.9f, 2.7f, L, v3(0, 2.75f, 0), 0, 1 | 2 | 4 | 16 | 32);
    dl_box(&d, L_CARROOF, 0, 2.6f, 0.35f, L - 0.8f, v3(0, 4.27f, 0), 0, 1 | 2 | 4 | 16 | 32);
    dl_box(&d, L_STRIPE, 0, 2.92f, 0.14f, L - 0.2f, v3(0, 2.1f, 0), 0, 1 | 2);
    dl_box(&d, L_BODYDARK, 0, 2.95f, 0.5f, 0.5f, v3(0, 1.35f, L / 2 + 0.05f), 0, 1 | 2 | 4 | 16);
    running(&d, L);
    int cl = toonIdx(L_CABLIT), sh = toonIdx(L_SHIELD);
    dl_plane(&d, cl, 2.4f, 1.0f, v3(0, 3.25f, L / 2 + 0.01f), 0);
    for (int s = -1; s <= 1; s += 2) {
        dl_plane(&d, cl, 1.6f, 0.9f, v3(s * 1.451f, 3.25f, L / 2 - 1.6f), s * 1.5708f);
        dl_plane(&d, sh, 1.6f, 0.9f, v3(s * 1.451f, 3.25f, -L / 2 + 1.6f), s * 1.5708f);
    }
    dl_plane(&d, sh, 2.4f, 1.0f, v3(0, 3.25f, -L / 2 - 0.01f), 3.1416f);
    for (int s = -1; s <= 1; s += 2) dl_plane(&d, 31, 0.34f, 0.34f, v3(s * 0.9f, 1.85f, L / 2 + 0.02f), 0);
    dl_plane(&d, 31, 0.28f, 0.28f, v3(0, 3.95f, L / 2 + 0.02f), 0);
    c->ext = dl_finish(&d);
    // cab interior
    dl_begin(&d, cbuf, 9000, 0, 0, 0, 4);
    dl_poly(&d, PF_OPAQUE_NC);
    dl_beginp(&d, GL_QUADS);
    dl_box(&d, L_BODYDARK, 0, 2.7f, 0.4f, 0.8f, v3(0, 2.15f, L / 2 - 0.5f), 0, 4 | 32);
    for (int i = 0; i < 5; i++) dl_box(&d, (i & 1) ? L_LITDIM : L_LIT, 1, 0.07f, 0.02f, 0.05f, v3(-0.7f + i * 0.32f, 2.36f, L / 2 - 0.85f), 0, 4);
    int iw = toonIdx(L_INWALL * 0.75f);
    for (int s = -1; s <= 1; s += 2) {
        dl_box(&d, L_BODYDARK, 0, 0.16f, 1.3f, 0.16f, v3(s * 1.32f, 3.2f, L / 2 - 0.12f), 0, 1 | 2 | 32);
        dl_plane(&d, iw, 3.0f, 1.2f, v3(s * 1.40f, 1.95f, L / 2 - 1.5f), -s * 1.5708f);
        dl_plane(&d, iw, 3.0f, 0.5f, v3(s * 1.40f, 3.95f, L / 2 - 1.5f), -s * 1.5708f);
    }
    dl_box(&d, L_BODYDARK, 0, 2.8f, 0.3f, 0.2f, v3(0, 3.92f, L / 2 - 0.12f), 0, 8 | 32);
    dl_hplane(&d, toonIdx(L_INCEIL * 0.6f), 2.8f, 3.0f, v3(0, 4.05f, L / 2 - 1.5f), 0);
    dl_plane(&d, iw, 2.8f, 2.7f, v3(0, 2.75f, L / 2 - 3.0f), 0);
    dl_hplane(&d, toonIdx(L_INFLOOR * 0.8f), 2.8f, 3.0f, v3(0, 1.5f, L / 2 - 1.5f), 1);
    c->interior = dl_finish(&d);
    // beam (translucent cone, S=16)
    dl_begin(&d, cbuf, 9000, 0, 0, 0, 16);
    dl_poly(&d, PF_TRANS(2, 30));
    dl_beginp(&d, GL_TRIANGLES);
    dl_col(&d, 29);
    for (int s = 0; s < 10; s++) {
        float a0 = s * 0.6283f, a1 = (s + 1) * 0.6283f;
        V3 A = v3(0, 1.9f, L / 2 + 0.1f);
        V3 B = v3(cosf(a0) * 6, 1.9f + sinf(a0) * 6, L / 2 + 48), C = v3(cosf(a1) * 6, 1.9f + sinf(a1) * 6, L / 2 + 48);
        dl_vv(&d, A); dl_vv(&d, B); dl_vv(&d, C);
    }
    beamList = dl_finish(&d);
}

static void makeCar(Car *c, int seat)
{
    float L = 17; c->L = L; c->type = 1;
    DL d; dl_begin(&d, cbuf, 9000, 0, 0, 0, 4);
    dl_poly(&d, PF_OPAQUE);
    dl_beginp(&d, GL_QUADS);
    dl_box(&d, L_BODY, 0, 2.9f, 2.7f, L, v3(0, 2.75f, 0), 0, 1 | 2 | 4 | 16 | 32);
    dl_box(&d, L_CARROOF, 0, 2.6f, 0.35f, L - 0.6f, v3(0, 4.27f, 0), 0, 1 | 2 | 4 | 16 | 32);
    dl_box(&d, L_STRIPE, 0, 2.92f, 0.14f, L - 0.2f, v3(0, 2.12f, 0), 0, 1 | 2);
    running(&d, L);
    c->ext = dl_finish(&d);
    dl_begin(&d, cbuf, 9000, 0, 0, 0, 4);
    dl_poly(&d, PF_OPAQUE);
    dl_beginp(&d, GL_QUADS);
    int wc = toonIdx(L_WIN), wd = toonIdx(L_LITDIM);
    for (int k = 0; k < 7; k++) {
        float cz = (k - 3) * BAY;
        for (int s = -1; s <= 1; s += 2) dl_plane(&d, wc, 1.6f, 0.95f, v3(s * 1.452f, 2.85f, cz), s * 1.5708f);
    }
    for (int s = -1; s <= 1; s += 2) dl_plane(&d, wd, 0.9f, 2.0f, v3(s * 1.452f, 2.4f, L / 2 - 0.6f), s * 1.5708f);
    c->wins = dl_finish(&d);
    if (seat) {
        dl_begin(&d, cbuf, 9000, 0, 0, 0, 4);
        dl_poly(&d, PF_OPAQUE_NC);
        dl_beginp(&d, GL_QUADS);
        int fl = toonIdx(L_INFLOOR * 0.9f), ce = toonIdx(L_INCEIL * 0.85f), wl = toonIdx(L_INWALL * 0.8f), wl2 = toonIdx(L_INWALL * 0.65f);
        dl_hplane(&d, fl, 2.78f, L - 0.4f, v3(0, 1.45f, 0), 1);
        dl_hplane(&d, ce, 2.78f, L - 0.4f, v3(0, 4.02f, 0), 0);
        for (int s = -1; s <= 1; s += 2) {
            dl_plane(&d, wl2, L - 0.4f, 0.9f, v3(s * 1.39f, 1.9f, 0), -s * 1.5708f);
            dl_plane(&d, wl, L - 0.4f, 0.67f, v3(s * 1.39f, 3.69f, 0), -s * 1.5708f);
            for (int k = 0; k < 6; k++) dl_plane(&d, wl, 0.6f, 1.0f, v3(s * 1.39f, 2.85f, (k - 3) * BAY + BAY / 2), -s * 1.5708f);
            for (int e = -1; e <= 1; e += 2) dl_plane(&d, wl, 0.75f, 1.0f, v3(s * 1.39f, 2.85f, e * 8.07f), -s * 1.5708f);
            dl_box(&d, L_LIT, 1, 0.22f, 0.03f, L - 1.6f, v3(s * 0.5f, 4.0f, 0), 0, 8);
            dl_box(&d, L_BODYDARK * 1.5f, 0, 0.45f, 0.04f, L - 1, v3(s * 1.13f, 3.55f, 0), 0, 4 | 8);
            for (int k = 0; k < 7; k++) {
                float cz = (k - 3) * BAY;
                dl_box(&d, L_BODYDARK * 2, 0, 0.06f, 0.06f, 1.7f, v3(s * 1.36f, 2.36f, cz), 0, 4);
                int aisle = s > 0 ? 2 : 1;
                for (int f = -1; f <= 1; f += 2) {
                    float z = cz + f * 0.58f;
                    float seatL = 0.55f;
                    dl_box(&d, seatL, 0, 1.0f, 0.68f, 0.78f, v3(s * 0.82f, 1.79f, z), 0, 4 | 16 | 32 | aisle);
                    dl_box(&d, seatL * 0.9f, 0, 1.02f, 1.05f, 0.16f, v3(s * 0.82f, 2.55f, cz + f * 1.02f), 0, 4 | 16 | 32 | aisle);
                }
                dl_box(&d, L_INCEIL, 0, 0.42f, 0.05f, 0.6f, v3(s * 1.12f, 2.45f, cz), 0, 4 | aisle);
            }
        }
        for (int e = -1; e <= 1; e += 2) {
            dl_plane(&d, wl, 2.78f, 2.57f, v3(0, 2.73f, e * (L / 2 - 0.25f)), e > 0 ? 3.1416f : 0);
            dl_plane(&d, toonIdx(L_BODYDARK), 0.9f, 2.0f, v3(0, 2.45f, e * (L / 2 - 0.27f)), e > 0 ? 3.1416f : 0);
        }
        static const int pas[4][3] = { { 1, -1, 1 }, { 5, 1, -1 }, { 4, -1, -1 }, { 6, 1, 1 } };
        for (int q = 0; q < 4; q++) {
            float cz = (pas[q][0] - 3) * BAY, z = cz + pas[q][2] * 0.6f;
            float x = pas[q][1] * 0.85f;
            dl_box(&d, L_PERSON, 0, 0.5f, 0.72f, 0.32f, v3(x, 2.45f, z), 0, 1 | 2 | 4 | 16 | 32);
            dl_box(&d, L_PERSON, 0, 0.3f, 0.32f, 0.3f, v3(x, 3.0f, z), 0, 1 | 2 | 4 | 16 | 32);
        }
        c->interior = dl_finish(&d);
    }
}

static void placeTrain(void)
{
    float off = 0;
    for (int n = 0; n < NCARS; n++) {
        Car *c = &cars[n];
        float center = D - off - c->L / 2; off += c->L + GAP;
        float b = c->L / 2 - 2.6f;
        TPt sa, sb; sample(center + b, &sa); sample(center - b, &sb);
        c->pos = v3((sa.x + sb.x) * 0.5f, (sa.y + sb.y) * 0.5f, (sa.z + sb.z) * 0.5f);
        V3 Z = vnorm(v3(sa.x - c->pos.x, sa.y - c->pos.y, sa.z - c->pos.z));
        V3 X = vnorm(vcross(v3(0, 1, 0), Z));
        V3 Y = vcross(Z, X);
        c->X = X; c->Y = Y; c->Z = Z;
    }
}
static V3 carL2W(const Car *c, V3 l)
{
    return vadd(c->pos, vadd(vadd(vscale(c->X, l.x), vscale(c->Y, l.y)), vscale(c->Z, l.z)));
}

/* ------------------------------------------------------------------ camera / matrices */
static V3 camP, camRight, camUp, camBack;
static float curFov = 50, curTanH = 0.62f;
static void setView(V3 pos, V3 target)
{
    camP = pos;
    V3 f = vnorm(vsub(target, pos));
    V3 up = v3(0, 1, 0);
    if (fabsf(f.y) > 0.999f) up = v3(0, 0, 1);
    V3 r = vnorm(vcross(f, up));
    V3 u = vcross(r, f);
    camRight = r; camUp = u; camBack = vscale(f, -1);
}
static void computeViewFixed(void);
static inline s32 fx(float v) { return (s32)(v * 4096.0f); }
static s32 viewM[12];
static void computeViewFixed(void)
{
    V3 ax[3] = { v3(1, 0, 0), v3(0, 1, 0), v3(0, 0, 1) };
    for (int j = 0; j < 3; j++) {
        viewM[j * 3 + 0] = fx(j == 0 ? camRight.x : j == 1 ? camRight.y : camRight.z);
        viewM[j * 3 + 1] = fx(j == 0 ? camUp.x : j == 1 ? camUp.y : camUp.z);
        viewM[j * 3 + 2] = fx(j == 0 ? camBack.x : j == 1 ? camBack.y : camBack.z);
    }
    (void)ax;
    viewM[9] = viewM[10] = viewM[11] = 0;
}
// modelview = View * T(origin-cam) * [X Y Z] * S   (computed by the geometry engine)
static void loadMV(const V3 *X, const V3 *Y, const V3 *Z, float s, V3 origin)
{
    MATRIX_CONTROL = GL_MODELVIEW;
    for (int i = 0; i < 12; i++) MATRIX_LOAD4x3 = viewM[i];
    MATRIX_TRANSLATE = fx(origin.x - camP.x);
    MATRIX_TRANSLATE = fx(origin.y - camP.y);
    MATRIX_TRANSLATE = fx(origin.z - camP.z);
    if (X) {
        MATRIX_MULT3x3 = fx(X->x); MATRIX_MULT3x3 = fx(X->y); MATRIX_MULT3x3 = fx(X->z);
        MATRIX_MULT3x3 = fx(Y->x); MATRIX_MULT3x3 = fx(Y->y); MATRIX_MULT3x3 = fx(Y->z);
        MATRIX_MULT3x3 = fx(Z->x); MATRIX_MULT3x3 = fx(Z->y); MATRIX_MULT3x3 = fx(Z->z);
    }
    s32 si = fx(s);
    MATRIX_SCALE = si; MATRIX_SCALE = si; MATRIX_SCALE = si;
}
static void setProjection(float fov)
{
    curFov = fov;
    curTanH = tanf(fov * 0.0087266f) * 1.3333f;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(fov, 256.0f / 192.0f, 0.2f, 420.0f);
    glMatrixMode(GL_MODELVIEW);
}

/* ------------------------------------------------------------------ sky */
static u32 skyBuf[6000];
static u32 *skyList;
static void buildSky(void)
{
    DL d; dl_begin(&d, skyBuf, 6000, 0, 0, 0, 64);
    const float R = 380;
    // dome
    static const float EL[] = { -40, -6, 0, 4, 9, 16, 26, 40, 60, 90 };
    const int NA = 12;
    float horL = hexLum(0x1f3a62), topL = hexLum(0x01030a);
    dl_poly(&d, PF_TRANS(30, 1));
    for (int e = 0; e < 9; e++) {
        dl_beginp(&d, GL_QUAD_STRIP);
        for (int a = 0; a <= NA; a++) {
            float az = a * 6.28318f / NA;
            for (int q = 0; q < 2; q++) {
                int ee = e + (q ^ FLIPW);
                float el = EL[ee] * 0.0174533f;
                float y = sinf(el), rr = cosf(el);
                float t = powf(fmaxf(0, y), 0.45f);
                dl_col(&d, toonIdx(horL + (topL - horL) * t));
                dl_v(&d, cosf(az) * rr * R, y * R, sinf(az) * rr * R);
            }
        }
    }
    // mountain rings
    for (int ring = 0; ring < 2; ring++) {
        float RR = ring ? 285 : 332, base = (ring ? -10 : 10) * 0.19f, amp = (ring ? 230 : 300) * 0.19f, f = ring ? 3.1f : 2.2f, off = ring ? 9.1f : 3.3f;
        int col = toonIdx(hexLum(ring ? 0x0c1932 : 0x172d50));
        dl_poly(&d, PF_TRANS(30, 2 + ring));
        dl_beginp(&d, GL_QUAD_STRIP);
        dl_col(&d, col);
        const int N = 56;
        for (int i = 0; i <= N; i++) {
            float a = i * 6.28318f / N, c = cosf(a), s = sinf(a);
            float h = base + amp * powf(fbm(c * f + off, s * f - off), 2.2f) * 1.6f;
            if (FLIPW) { dl_v(&d, c * RR, h, s * RR); dl_v(&d, c * RR, -25, s * RR); }
            else { dl_v(&d, c * RR, -25, s * RR); dl_v(&d, c * RR, h, s * RR); }
        }
    }
    // stars (lines)
    dl_poly(&d, PF_TRANS(30, 4));
    dl_beginp(&d, GL_TRIANGLES);
    for (int i = 0; i < 110; i++) {
        float a = rnd() * 6.283f, y = 0.1f + powf(rnd(), 0.8f) * 0.9f, r = sqrtf(1 - y * y);
        V3 p = v3(cosf(a) * r * R * 0.97f, y * R * 0.97f, sinf(a) * r * R * 0.97f);
        dl_col(&d, rnd() < 0.3f ? 31 : 27);
        V3 q = vadd(p, v3(-sinf(a) * 0.9f, 0, cosf(a) * 0.9f));
        dl_vv(&d, p); dl_vv(&d, q); dl_vv(&d, q);
    }
    // moon + halo
    V3 md = v3(LX, LY, LZ);
    V3 mu = vnorm(vcross(md, v3(0, 1, 0))), mv = vcross(mu, md);
    for (int k = 0; k < 2; k++) {
        float rad = k ? 10.3f : 42, dist = k ? R * 0.95f : R * 0.96f;
        dl_poly(&d, k ? PF_TRANS(30, 6) : PF_TRANS(8, 5));
        dl_beginp(&d, GL_TRIANGLES);
        dl_col(&d, k ? 31 : 20);
        V3 c = vscale(md, dist);
        for (int s = 0; s < 14; s++) {
            float a0 = s * 6.28318f / 14, a1 = (s + 1) * 6.28318f / 14;
            V3 A = vadd(c, vadd(vscale(mu, cosf(a0) * rad), vscale(mv, sinf(a0) * rad)));
            V3 B = vadd(c, vadd(vscale(mu, cosf(a1) * rad), vscale(mv, sinf(a1) * rad)));
            dl_vv(&d, c); dl_vv(&d, A); dl_vv(&d, B);
        }
    }
    dl_finish_static(&d);
    skyList = skyBuf;
}

/* ------------------------------------------------------------------ snow */
#define SN 140
static s32 snp[SN][3];   // world mode: world pos in 1/512 m; local mode: car-local in 1/512 m
static s32 snv[SN];      // fall speed factor (4.12)
static int snowOn = 1, snowMode = 0; // 0 world 1 local
static Car *snowCar = NULL;
static inline s32 wrapi(s32 v, s32 size) { return ((v + (size >> 1)) & (size - 1)) - (size >> 1); }
static void resetSnow(int mode, Car *car)
{
    snowMode = mode; snowCar = car;
    s32 cx = (s32)(camP.x * 512), cy = (s32)(camP.y * 512), cz = (s32)(camP.z * 512);
    for (int i = 0; i < SN; i++) {
        snp[i][0] = (s32)((rnd() - 0.5f) * 32 * 512); snp[i][1] = (s32)((rnd() - 0.5f) * 16 * 512); snp[i][2] = (s32)((rnd() - 0.5f) * 32 * 512);
        if (mode == 0) { snp[i][0] += cx; snp[i][1] += cy; snp[i][2] += cz; }
    }
}
static u32 snowBuf[3200];
static void updateDrawSnow(float dt)
{
    if (!snowOn) return;
    DL d; dl_begin(&d, snowBuf, 3200, 0, 0, 0, 8);
    dl_poly(&d, PF_OPAQUE_NC);
    dl_beginp(&d, GL_TRIANGLES);
    dl_col(&d, 29);
    // flake = tiny line along screen right+up (camera-space offset in 1/512 m)
    s32 ox = (s32)((camRight.x * 0.07f + camUp.x * 0.05f) * 512), oy = (s32)((camRight.y * 0.07f + camUp.y * 0.05f) * 512), oz = (s32)((camRight.z * 0.07f + camUp.z * 0.05f) * 512);
    s32 dtq = (s32)(dt * 4096);
    s32 cx = (s32)(camP.x * 512), cy = (s32)(camP.y * 512), cz = (s32)(camP.z * 512);
    s32 ph0 = (s32)(T * 0.3f * 5215), ph1 = (s32)(T * 0.5f * 5215);
    u32 *b = d.b;
    int n = d.n;
#define EMITV(X, Y, Z) do { \
        if (d.pk == 0) { d.hdr = n; b[n++] = 0; } \
        b[d.hdr] |= GXC_VTX16 << (8 * d.pk); d.pk = (d.pk + 1) & 3; \
        b[n++] = ((u32)(uint16_t)(X)) | ((u32)(uint16_t)(Y) << 16); b[n++] = (uint16_t)(Z); } while (0)
    d.n = n;
    if (snowMode == 0) {
        for (int i = 0; i < SN; i++) {
            s32 *p = snp[i];
            p[0] += ((819 + ((sinLerp(ph0 + i * 5215) * 307) >> 12)) * dtq) >> 12;
            p[1] -= ((1229 * snv[i]) >> 12) * dtq >> 12;
            p[2] += ((cosLerp(ph1 + i * 3650) >> 3) * dtq) >> 12;
            s32 rx = wrapi(p[0] - cx, 16384), ry = wrapi(p[1] - cy - 2048, 8192) + 2048, rz = wrapi(p[2] - cz, 16384);
            p[0] = cx + rx; p[1] = cy + ry; p[2] = cz + rz;
            EMITV(rx, ry, rz); EMITV(rx + ox, ry + oy, rz + oz); EMITV(rx + ox, ry + oy, rz + oz);
        }
    } else {
        Car *c = snowCar;
        s32 hl = (s32)((c->L / 2 + 0.4f) * 512);
        s32 M[9] = { fx(c->X.x), fx(c->X.y), fx(c->X.z), fx(c->Y.x), fx(c->Y.y), fx(c->Y.z), fx(c->Z.x), fx(c->Z.y), fx(c->Z.z) };
        s32 bx = (s32)((c->pos.x - camP.x) * 512), by = (s32)((c->pos.y - camP.y) * 512), bz = (s32)((c->pos.z - camP.z) * 512);
        s32 vz = (s32)((speed * 0.9f + 2) * 512);
        for (int i = 0; i < SN; i++) {
            s32 *p = snp[i];
            p[0] += (614 * dtq) >> 12;
            p[1] -= ((1229 * snv[i]) >> 12) * dtq >> 12;
            p[2] -= (vz * dtq) >> 12;
            p[0] = wrapi(p[0], 16384); p[1] = wrapi(p[1] - 4096, 8192) + 4096; p[2] = wrapi(p[2], 16384);
            if (p[0] < 896 && p[0] > -896 && p[2] < hl && p[2] > -hl && p[1] < 2355) p[1] = 8000;
            s32 wx = bx + ((M[0] * p[0] + M[3] * p[1] + M[6] * p[2]) >> 12);
            s32 wy = by + ((M[1] * p[0] + M[4] * p[1] + M[7] * p[2]) >> 12);
            s32 wz = bz + ((M[2] * p[0] + M[5] * p[1] + M[8] * p[2]) >> 12);
            if (wx > 32000 || wx < -32000 || wy > 32000 || wy < -32000 || wz > 32000 || wz < -32000) continue;
            EMITV(wx, wy, wz); EMITV(wx + ox, wy + oy, wz + oz); EMITV(wx + ox, wy + oy, wz + oz);
        }
    }
    d.n = n;
#undef EMITV
    dl_finish_static(&d);
    loadMV(NULL, NULL, NULL, 8, camP);
    glCallList(snowBuf);
}

/* ------------------------------------------------------------------ headlight pool */
static u32 poolBuf[600];
static void drawHeadPool(void)
{
    Car *c = &cars[0];
    DL d; dl_begin(&d, poolBuf, 600, c->pos.x, c->pos.y, c->pos.z, 8);
    float d0 = D + 0.5f;
    for (int s = 0; s < 6; s++) {
        float a = d0 + s * 7, b = d0 + (s + 1) * 7;
        int al = 12 - s * 2; if (al < 1) al = 1;
        dl_poly(&d, PF_TRANSF(al, 40 + s));
        dl_beginp(&d, GL_QUADS);
        dl_col(&d, 30);
        float wa = 1.6f + s * 0.55f, wb = 1.6f + (s + 1) * 0.55f;
        if (wa > 2.4f) wa = 2.4f;
        if (wb > 2.4f) wb = 2.4f;
        TPt pa, pb; sample(a, &pa); sample(b, &pb);
        V3 A = v3(pa.x - pa.rx * wa, pa.y + 0.17f, pa.z - pa.rz * wa), B = v3(pa.x + pa.rx * wa, pa.y + 0.17f, pa.z + pa.rz * wa);
        V3 C = v3(pb.x + pb.rx * wb, pb.y + 0.17f, pb.z + pb.rz * wb), E = v3(pb.x - pb.rx * wb, pb.y + 0.17f, pb.z - pb.rz * wb);
        dl_vv(&d, A); dl_vv(&d, B); dl_vv(&d, C); dl_vv(&d, E);
    }
    dl_finish_static(&d);
    loadMV(NULL, NULL, NULL, 8, c->pos);
    glCallList(poolBuf);
}

/* ------------------------------------------------------------------ camera shots */
enum { S_CHASE, S_SIDE, S_TRACKSIDE, S_DRONE, S_NOSE, S_CAB, S_SEAT, S_CRANE, S_TOP, S_FLYBY, S_WHEEL, S_COUNT };
static const char *SHOTLBL[S_COUNT] = { "VERFOLGER", "SEITE", "GLEIS", "DROHNE", "BUG", "F\x80HRERSTAND", "ABTEIL", "KRAN", "VOGEL", "\x80" "BERHOLUNG", "FAHRWERK" };
static const float SHOTFOV[S_COUNT] = { 52, 44, 46, 50, 62, 64, 62, 48, 46, 50, 60 };
static const float SHOTSM[S_COUNT] = { 4, 3, 0, 2.5f, 8, 0, 0, 0, 3, 0, 0 };
static const float SHOTLSM[S_COUNT] = { 4, 3, 3, 2.5f, 8, 0, 0, 4, 3, 0, 0 };
static const int SHOTDIRECT[S_COUNT] = { 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 1 };
static const int BTNSHOTS[8] = { S_CHASE, S_SIDE, S_TRACKSIDE, S_DRONE, S_NOSE, S_WHEEL, S_CAB, S_SEAT };

typedef struct { int key; float t; V3 pos, look; float l, o, a, r, hh, ad, d; int started, cut, hasA; V3 A; } Shot;
static Shot cur;
static float lookYaw = 0, lookPitch = 0;
static int snap = 1;
static float fade = 0;
static V3 camPos, camLook;

static float sideSign(void) { return rnd() < 0.5f ? -1.0f : 1.0f; }
static float bestSide(float d, float l)
{
    TPt s; sample(d, &s);
    float xa = s.x + s.rx * l, za = s.z + s.rz * l, xb = s.x - s.rx * l, zb = s.z - s.rz * l;
    float a = groundH(s.y, s.v, l, xa, za), b = groundH(s.y, s.v, -l, xb, zb);
    return fabsf(a - b) < 1.5f ? sideSign() : (a < b ? 1.0f : -1.0f);
}
static V3 dirFrom(const Car *c)
{
    V3 l = v3(sinf(lookYaw) * cosf(lookPitch), sinf(lookPitch), cosf(lookYaw) * cosf(lookPitch));
    return vadd(vadd(vscale(c->X, l.x), vscale(c->Y, l.y)), vscale(c->Z, l.z));
}

static void shotInit(Shot *s)
{
    switch (s->key) {
    case S_CHASE: s->l = sideSign() * (3 + rnd() * 5); break;
    case S_SIDE: { s->o = 10 + rnd() * (TRAINLEN - 20); float L = 13 + rnd() * 9; s->l = bestSide(D - s->o + 40, L) * L; } break;
    case S_TRACKSIDE: s->hasA = 0; s->started = 0; break;
    case S_DRONE: s->a = rnd() * 6.28f; s->r = 55 + rnd() * 25; s->hh = 30 + rnd() * 20; break;
    case S_NOSE: s->l = sideSign() * 2.3f; break;
    case S_CAB: lookYaw = 0; lookPitch = -0.08f; break;
    case S_SEAT: lookYaw = 1.28f; lookPitch = -0.05f; break;
    case S_CRANE: { s->ad = D + speed * 3.4f; float L = 10 + rnd() * 8; s->l = bestSide(s->ad, L) * L; } break;
    case S_FLYBY: { s->d = D - TRAINLEN - 18; float L = 8 + rnd() * 4; s->l = bestSide(D, L) * L; } break;
    default: break;
    }
}
static void shotUpdate(Shot *s, float t, float dt)
{
    switch (s->key) {
    case S_CHASE: s->pos = TPG(D - TRAINLEN - 22, s->l, 8.5f); s->look = TP(D + 10, 0, 2); break;
    case S_SIDE: { float d = D - s->o; s->pos = TPG(d, s->l, 2.6f); s->look = TP(d, 0, 2.6f); } break;
    case S_TRACKSIDE: {
        if (!s->hasA || D - s->ad > TRAINLEN + 30) {
            s->ad = D + fmaxf(80, speed * 4.2f);
            float sd = bestSide(s->ad, 12);
            s->A = TPG(s->ad, sd * (9 + rnd() * 6), 1.3f + rnd() * 2.2f);
            s->hasA = 1;
            if (s->started) s->cut = 1;
            s->started = 1;
        }
        s->pos = s->A;
        float u = clampf((D - s->ad + 12) / (TRAINLEN + 24), 0, 1);
        s->look = TP(D - 6 - u * (TRAINLEN - 10), 0, 2.4f);
    } break;
    case S_DRONE: {
        V3 c = TP(D - TRAINLEN * 0.5f, 0, 0);
        float a = s->a + t * 0.11f;
        s->pos = vadd(c, v3(sinf(a) * s->r, s->hh, cosf(a) * s->r));
        s->look = vadd(c, v3(0, 2, 0));
    } break;
    case S_NOSE: s->pos = TP(D + 15, s->l, 0.55f); s->look = TP(D - 8, 0, 2.4f); break;
    case S_CAB: {
        Car *c = &cars[0];
        s->pos = carL2W(c, v3(0.5f, 3.15f + sinf(t * 9) * 0.006f, c->L / 2 - 1.7f));
        s->look = vadd(s->pos, dirFrom(c));
    } break;
    case S_SEAT: {
        Car *c = SEATCAR;
        float bob = sinf(t * 11) * 0.006f + sinf(t * 2.3f) * 0.008f;
        s->pos = carL2W(c, v3(-0.5f, 2.85f + bob, -0.58f));
        s->look = vadd(s->pos, dirFrom(c));
    } break;
    case S_CRANE: s->pos = TPG(s->ad, s->l, 1 + t * 2.6f); s->look = TP(D - 12, 0, 2); break;
    case S_TOP: s->pos = TP(D - TRAINLEN * 0.5f - 6, 0, 80); s->look = TP(D - TRAINLEN * 0.5f + 8, 0, 0); break;
    case S_FLYBY: s->d += dt * speed * 1.28f; s->pos = TPG(s->d, s->l, 3.2f); s->look = TP(s->d + 14, 0, 2.2f); break;
    case S_WHEEL: {
        Car *c = &cars[3];
        s->pos = carL2W(c, v3(2.15f, 0.95f, c->L / 2 + 0.6f));
        s->look = carL2W(c, v3(1.35f, 1.15f, -c->L * 1.6f));
    } break;
    }
}

/* ------------------------------------------------------------------ UI (bitmaps) */
static u8 topBox[256 * 192], topTxt[256 * 192], subBuf[256 * 192];
static int sbgId = 7;
static int hudHidden = 0, hudDirty = 1, subDirty = 1;
enum { C_TR = 0, C_BOX = 1, C_TXT = 2, C_TITLE = 3, C_DARK = 4, C_BORDER = 5, C_BORDERK = 6, C_ON = 7, C_BG0 = 8, C_BG1 = 9, C_DIM = 10 };
static void uiPalettes(void)
{
    uint16_t p[11] = { 0, uiCol(0x03060e), uiCol(0xa9d0f5), uiCol(0xcfe6ff), uiCol(0x03060e), uiCol(0x234a7a), uiCol(0x4a7fb5), uiCol(0xa9d0f5), uiCol(0x03060e), uiCol(0x0a1428), uiCol(0x4a7fb5) };
    for (int i = 0; i < 11; i++) { BG_PALETTE[i] = p[i]; BG_PALETTE_SUB[i] = p[i]; }
}
static void fillRect(u8 *b, int x, int y, int w, int h, u8 c)
{
    for (int yy = y; yy < y + h; yy++) {
        if (yy < 0 || yy >= 192) continue;
        int x0 = x < 0 ? 0 : x, x1 = x + w > 256 ? 256 : x + w;
        if (x1 > x0) memset(b + yy * 256 + x0, c, x1 - x0);
    }
}
static void rectOutline(u8 *b, int x, int y, int w, int h, u8 c)
{
    fillRect(b, x, y, w, 1, c); fillRect(b, x, y + h - 1, w, 1, c);
    fillRect(b, x, y, 1, h, c); fillRect(b, x + w - 1, y, 1, h, c);
}
static int strW(const char *s) { return (int)strlen(s) * 8; }
static void drawStr(u8 *b, int x, int y, const char *s, u8 c, int tall)
{
    for (; *s; s++, x += 8) {
        unsigned ch = (unsigned char)*s;
        if (ch >= 140) ch = '?';
        const unsigned char *g = font8[ch];
        for (int r = 0; r < 8; r++) {
            unsigned bits = g[r];
            if (!bits) continue;
            for (int k = 0; k < (tall ? 2 : 1); k++) {
                int yy = y + r * (tall ? 2 : 1) + k;
                if (yy < 0 || yy >= 192) continue;
                u8 *row = b + yy * 256;
                for (int q = 0; q < 8; q++) if (bits & (0x80 >> q)) { int xx = x + q; if (xx >= 0 && xx < 256) row[xx] = c; }
            }
        }
    }
}
static void label(u8 *box, u8 *txt, int x, int y, const char *s, int tall, u8 col)
{
    int w = strW(s) + 8, h = tall ? 18 : 11;
    fillRect(box, x, y, w, h, C_BOX);
    drawStr(txt, x + 4, y + (tall ? 1 : 2), s, col, tall);
}

/* cinema state */
static struct { int on; float t, dur, clock; } cine = { 1, 0, 0, 0 };
static char hintText[48] = ""; static float hintTimer = 0;
static float uiTimer = 0;      // cinema auto hide countdown
static float subIdle = 0;      // bottom screen dim timer
static int subDim = 0;
static int soundOn = 0;

static void showHint(const char *s, float secs) { strncpy(hintText, s, 47); hintTimer = secs; hudDirty = 1; }

static void drawHUD(void)
{
    memset(topBox, 0, sizeof(topBox)); memset(topTxt, 0, sizeof(topTxt));
    char buf[64];
    label(topBox, topTxt, 6, 5, "HIGH TRAIN", 1, C_TITLE);
    sprintf(buf, "KM %03d.%d \x83 %03d KM/H", (int)(D / 1000), ((int)(D / 100)) % 10, (int)(speed * 3.6f));
    label(topBox, topTxt, 6, 26, buf, 0, C_TXT);
    sprintf(buf, "\x81 CAM %02d \x83 %s", cur.key + 1, SHOTLBL[cur.key]);
    label(topBox, topTxt, 6, 40, buf, 0, C_TXT);
    if (cine.on) {
        int c = (int)cine.clock;
        sprintf(buf, "%c KINO %02d:%02d:%02d", ((int)(T * 2) & 1) ? ' ' : '\x82', c / 3600, (c / 60) % 60, c % 60);
        label(topBox, topTxt, 256 - strW(buf) - 14, 5, buf, 0, C_TXT);
    }
    if (hintTimer > 0 && hintText[0]) {
        int w = strW(hintText) + 8;
        label(topBox, topTxt, (256 - w) / 2, 172, hintText, 0, C_TXT);
    }
    DC_FlushRange(topBox, sizeof(topBox)); DC_FlushRange(topTxt, sizeof(topTxt));
    dmaCopy(topBox, bgGetGfxPtr(2), sizeof(topBox));
    dmaCopy(topTxt, bgGetGfxPtr(3), sizeof(topTxt));
}

typedef struct { int x, y, w, h; int id; } Btn;
enum { B_SHOT0 = 0, B_CINE = 8, B_SPEED, B_SNOW, B_SOUND, B_HUD };
static Btn btns[13];
static int nbtn = 0;
static void layoutButtons(void)
{
    nbtn = 0;
    for (int i = 0; i < 8; i++) { Btn b = { i % 2 ? 131 : 6, 30 + (i / 2) * 25, 119, 22, B_SHOT0 + i }; btns[nbtn++] = b; }
    Btn c1 = { 6, 134, 119, 22, B_CINE }; btns[nbtn++] = c1;
    Btn c2 = { 131, 134, 119, 22, B_SPEED }; btns[nbtn++] = c2;
    Btn c3 = { 6, 160, 80, 22, B_SNOW }; btns[nbtn++] = c3;
    Btn c4 = { 90, 160, 88, 22, B_SOUND }; btns[nbtn++] = c4;
    Btn c5 = { 182, 160, 68, 22, B_HUD }; btns[nbtn++] = c5;
}
static void drawSub(void)
{
    u8 *b = subBuf;
    for (int y = 0; y < 192; y++) memset(b + y * 256, (y < 24) ? C_BG1 : C_BG0, 256);
    drawStr(b, 8, 4, "HIGH TRAIN", C_TITLE, 1);
    char buf[40];
    sprintf(buf, "KM %03d.%d", (int)(D / 1000), ((int)(D / 100)) % 10); drawStr(b, 248 - strW(buf), 3, buf, C_TXT, 0);
    sprintf(buf, "%03d KM/H", (int)(speed * 3.6f)); drawStr(b, 248 - strW(buf), 13, buf, C_TXT, 0);
    fillRect(b, 0, 24, 256, 1, C_BORDER);
    for (int i = 0; i < nbtn; i++) {
        Btn *t = &btns[i];
        const char *s = ""; int on = 0, key = 0;
        static const char *camL[8] = { "VERFOLGER", "SEITE", "GLEIS", "DROHNE", "BUG", "FAHRWERK", "F\x80HRERSTAND", "ABTEIL \x88" };
        static const char *spL[3] = { "TEMPO \x84\x85\x85", "TEMPO \x84\x84\x85", "TEMPO \x84\x84\x84" };
        if (t->id < 8) { s = camL[t->id]; on = !cine.on && cur.key == BTNSHOTS[t->id]; }
        else if (t->id == B_CINE) { s = "\x87 KINO"; on = cine.on; key = 1; }
        else if (t->id == B_SPEED) s = spL[spI];
        else if (t->id == B_SNOW) { s = "SCHNEE"; on = snowOn; }
        else if (t->id == B_SOUND) { s = soundOn ? "\x86 TON AN" : "\x86 TON AUS"; on = soundOn; }
        else if (t->id == B_HUD) { s = "HUD"; on = !hudHidden; }
        fillRect(b, t->x, t->y, t->w, t->h, on ? C_ON : C_BOX);
        rectOutline(b, t->x, t->y, t->w, t->h, on ? C_ON : (key ? C_BORDERK : C_BORDER));
        drawStr(b, t->x + (t->w - strW(s)) / 2, t->y + 3, s, on ? C_DARK : C_TXT, 1);
    }
    drawStr(b, 4, 184, "L/R KAMERA \x83 A KINO \x83 \x8b SCHAUEN", C_DIM, 0);
    DC_FlushRange(subBuf, sizeof(subBuf));
    dmaCopy(subBuf, bgGetGfxPtr(sbgId), sizeof(subBuf));
}

/* ------------------------------------------------------------------ audio */
#define SR 11025
static s16 *smpRumIn, *smpRumOut, *smpClack;
static int lenRum, lenClack;
static int rumbleId = -1, rumbleInside = -1;
typedef struct { float t, v; } Knock;
static Knock kq[16]; static int nkq = 0;
static const float AXLES[4][2] = { { 0, 0.75f }, { 2.6f, 1 }, { 26, 0.6f }, { 28.6f, 0.8f } };
#define JOINT 52
static int lastJoint = 0;

static void synthAudio(void)
{
    lenRum = SR * 2;
    int fade = SR / 4;
    smpRumIn = malloc(lenRum * 2); smpRumOut = malloc(lenRum * 2);
    float *tmpA = malloc((lenRum + fade) * sizeof(float)), *tmpB = malloc((lenRum + fade) * sizeof(float));
    float last = 0, lp1 = 0, lp2 = 0, lp3 = 0, bp1 = 0, bp2 = 0;
    float aLow = 1 - expf(-6.2832f * 110 / SR), aMid = 1 - expf(-6.2832f * 420 / SR), aHp = 1 - expf(-6.2832f * 200 / SR);
    for (int i = 0; i < lenRum + fade; i++) {
        float w = rnd() * 2 - 1;
        last = (last + 0.02f * w) / 1.02f;
        float br = last * 3.5f;
        lp1 += (br - lp1) * aLow; lp2 += (lp1 - lp2) * aLow;
        // roar: band around 320Hz from white-ish noise
        bp1 += (w - bp1) * aMid; bp2 += (bp1 - bp2) * aHp;
        float roar = (bp1 - bp2) * 0.6f;
        float hum = sinf(6.2832f * 41 * i / SR);
        lp3 += (lp2 - lp3) * aMid;
        tmpA[i] = lp2 * 1.5f + hum * 0.10f;          // inside: muffled
        tmpB[i] = lp2 * 0.9f + roar * 0.35f + hum * 0.04f; // outside
    }
    float pkA = 0.001f, pkB = 0.001f;
    for (int i = 0; i < lenRum; i++) {
        if (i < fade) { float f = (float)i / fade; tmpA[i] = tmpA[i] * f + tmpA[i + lenRum] * (1 - f); tmpB[i] = tmpB[i] * f + tmpB[i + lenRum] * (1 - f); }
        if (fabsf(tmpA[i]) > pkA) pkA = fabsf(tmpA[i]);
        if (fabsf(tmpB[i]) > pkB) pkB = fabsf(tmpB[i]);
    }
    for (int i = 0; i < lenRum; i++) { smpRumIn[i] = (s16)(tmpA[i] / pkA * 26000); smpRumOut[i] = (s16)(tmpB[i] / pkB * 24000); }
    free(tmpA); free(tmpB);
    // clack: deep thump + woody knock + echo
    lenClack = (int)(SR * 1.0f);
    float *c = calloc(lenClack, sizeof(float));
    float ph = 0, nlp = 0;
    float aKnock = 1 - expf(-6.2832f * 210 / SR);
    for (int i = 0; i < lenClack; i++) {
        float t = (float)i / SR;
        float f = t < 0.22f ? 64 * powf(37.0f / 64.0f, t / 0.22f) : 37;
        ph += 6.2832f * f / SR;
        float env = t < 0.028f ? t / 0.028f : expf(-(t - 0.028f) * 19.0f);
        float v = sinf(ph) * env * 1.35f;
        float nenv = t < 0.02f ? t / 0.02f : (t < 0.14f ? expf(-(t - 0.02f) * 60.0f) : 0);
        nlp += ((rnd() * 2 - 1) - nlp) * aKnock;
        v += nlp * nenv * 1.2f;
        c[i] = v;
    }
    // echo (delay .13 fb .45 lowpassed)
    int dly = (int)(SR * 0.13f); float elp = 0, aE = 1 - expf(-6.2832f * 300 / SR);
    for (int i = dly; i < lenClack; i++) { elp += (c[i - dly] - elp) * aE; c[i] += elp * 0.45f; }
    float pk = 0.001f;
    for (int i = 0; i < lenClack; i++) if (fabsf(c[i]) > pk) pk = fabsf(c[i]);
    smpClack = malloc(lenClack * 2);
    for (int i = 0; i < lenClack; i++) { float e = i > lenClack - 400 ? (lenClack - i) / 400.0f : 1; smpClack[i] = (s16)(c[i] / pk * 30000 * e); }
    free(c);
    DC_FlushRange(smpRumIn, lenRum * 2); DC_FlushRange(smpRumOut, lenRum * 2); DC_FlushRange(smpClack, lenClack * 2);
}
static int isInside(void) { return cur.key == S_SEAT || cur.key == S_CAB; }
static void audioStart(void)
{
    if (!smpClack) synthAudio();
    rumbleInside = -1;
    lastJoint = (int)(D / JOINT);
    nkq = 0;
    for (int a = 0; a < 4; a++) { kq[nkq].t = T + 0.05f + AXLES[a][0] / 24; kq[nkq].v = 0.6f * AXLES[a][1]; nkq++; }
}
static void audioStop(void)
{
    if (rumbleId >= 0) soundKill(rumbleId);
    rumbleId = -1; rumbleInside = -1; nkq = 0;
}
static void updateAudio(void)
{
    if (!soundOn) return;
    int inside = isInside();
    float sp = fminf(1.3f, speed / 24);
    if (inside != rumbleInside) {
        if (rumbleId >= 0) soundKill(rumbleId);
        s16 *s = inside ? smpRumIn : smpRumOut;
        rumbleId = soundPlaySample(s, SoundFormat_16Bit, lenRum * 2, SR, 0, 64, true, 0);
        rumbleInside = inside;
    }
    if (rumbleId >= 0) {
        int v = (int)((inside ? 95 : 60) * sp);
        if (v > 127) v = 127;
        soundSetVolume(rumbleId, v);
        soundSetFreq(rumbleId, (u16)(SR * (0.85f + 0.2f * sp)));
    }
    int J = (int)(D / JOINT);
    if (J != lastJoint) {
        lastJoint = J;
        float v = (inside ? 0.85f : 0.6f) * fminf(1, 0.5f + sp * 0.5f), s = fmaxf(speed, 1);
        for (int a = 0; a < 4 && nkq < 16; a++) { kq[nkq].t = T + 0.01f + AXLES[a][0] / s; kq[nkq].v = v * AXLES[a][1]; nkq++; }
    }
    for (int i = 0; i < nkq;) {
        if (kq[i].t <= T) {
            int vol = (int)(127 * kq[i].v);
            soundPlaySample(smpClack, SoundFormat_16Bit, lenClack * 2, inside ? 9000 : SR, vol > 127 ? 127 : vol, 64, false, 0);
            kq[i] = kq[--nkq];
        } else i++;
    }
}

/* ------------------------------------------------------------------ shot switching */
static void setShot(int key)
{
    memset(&cur, 0, sizeof(cur));
    cur.key = key;
    shotInit(&cur);
    snap = 1; fade = 0;
    setProjection(SHOTFOV[key]);
    if (key == S_SEAT) resetSnow(1, SEATCAR);
    else if (key == S_CAB) resetSnow(1, &cars[0]);
    else if (snowMode != 0) resetSnow(0, NULL);
    hudDirty = 1; subDirty = 1;
    if (!cine.on && (key == S_SEAT || key == S_CAB)) showHint("STEUERKREUZ: UMSEHEN", 3.5f);
}
static const int CINELIST[11] = { S_CHASE, S_SIDE, S_TRACKSIDE, S_DRONE, S_NOSE, S_CAB, S_SEAT, S_CRANE, S_TOP, S_FLYBY, S_WHEEL };
static void nextCine(void)
{
    int k;
    do { k = CINELIST[(int)(rnd() * 11) % 11]; } while (k == cur.key);
    cine.t = 0;
    cine.dur = k == S_SEAT ? 11 + rnd() * 5 : k == S_CAB ? 8 + rnd() * 4 : k == S_TRACKSIDE ? 8.5f : 5.5f + rnd() * 4.5f;
    setShot(k);
    if (k == S_SEAT) lookYaw = 1.15f + rnd() * 0.3f;
}
static void setCine(int on)
{
    cine.on = on;
    if (on) { cine.clock = 0; nextCine(); uiTimer = 2.2f; }
    else {
        uiTimer = 0;
        int k = cur.key;
        if (k == S_CRANE || k == S_TOP || k == S_FLYBY) k = S_CHASE;
        setShot(k);
    }
    hudDirty = subDirty = 1;
}

static void updateCam(float dt)
{
    cur.t += dt;
    shotUpdate(&cur, cur.t, dt);
    if (cur.cut) { cur.cut = 0; snap = 1; fade = 0; }
    if (snap || SHOTDIRECT[cur.key]) { camPos = cur.pos; camLook = cur.look; snap = 0; }
    else {
        float sm = SHOTSM[cur.key], ls = SHOTLSM[cur.key];
        float kp = sm > 0 ? 1 - expf(-dt * sm) : 1;
        float kl = ls > 0 ? 1 - expf(-dt * ls) : 1;
        camPos = vlerp(camPos, cur.pos, kp);
        camLook = vlerp(camLook, cur.look, kl);
    }
    setView(camPos, camLook);
    computeViewFixed();
}

/* ------------------------------------------------------------------ fog */
static void setupFog(void)
{
    const float n = 0.2f, f = 420.0f;
    uint16_t fogc = palMix(toonIdx(hexLum(0x182c4c)) * 6.0f / 31.0f);
    glFogColor(fogc & 31, (fogc >> 5) & 31, (fogc >> 10) & 31, 31);
    // depth15 = z_ndc * 0x4000 + 0x3FFF ;  z_ndc(w) = (f+n)/(f-n) - 2fn/((f-n) w)
#define DEPTHW(w) (((f + n) / (f - n) - 2 * f * n / ((f - n) * (w))) * 16384.0f + 16383.0f)
    float d0 = DEPTHW(10.0f), d1 = DEPTHW(405.0f);
    int shift = 0;
    for (int s = 10; s >= 0; s--) if (32 * (0x400 >> s) >= (int)(d1 - d0)) { shift = s; break; }
    int step = 0x400 >> shift;
    glFogShift(shift);
    glFogOffset((int)d0);
    for (int i = 0; i < 32; i++) {
        float dep = d0 + (i + 0.5f) * step;
        float zn = (dep - 16383.0f) / 16384.0f;
        float den = (f + n) - zn * (f - n);
        float w = den > 0.0001f ? 2 * f * n / den : 10000;
        float a = 0.0042f * w;
        float fogv = 1 - expf(-a * a);
        fogv = fmaxf(fogv, smoothf(330, 400, w));
        int dv = (int)(fogv * 127);
        glFogDensity(i, dv > 127 ? 127 : dv);
    }
}

/* ------------------------------------------------------------------ tree texture */
static void makeTreeTexture(void)
{
    static u8 tex[32 * 64 / 4];
    memset(tex, 0, sizeof(tex));
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 32; x++) {
            float fy = (63 - y) / 64.0f; // 0 bottom .. 1 top
            float cx = (x + 0.5f) / 32.0f - 0.5f;
            int c = 0;
            // trunk
            if (fy < 0.08f && fabsf(cx) < 0.05f) c = 1;
            // lower tier: cone base fy .06..0.7, half width .5 -> 0
            float t1 = (fy - 0.06f) / 0.64f;
            if (t1 >= 0 && t1 <= 1 && fabsf(cx) < 0.5f * (1 - t1)) c = 2;
            float t2 = (fy - 0.42f) / 0.58f;
            if (t2 >= 0 && t2 <= 1 && fabsf(cx) < 0.36f * (1 - t2)) c = 2;
            if (c == 2 && cx < -0.06f) c = 3; // lit side (moon from -x)
            if (c) tex[(y * 32 + x) / 4] |= c << ((x & 3) * 2);
        }
    int id;
    glGenTextures(1, &id);
    glBindTexture(0, id);
    glTexImage2D(0, 0, GL_RGB4, 32, 64, 0, TEXGEN_OFF | GL_TEXTURE_COLOR0_TRANSPARENT, tex);
    uint16_t pal[4] = { 0, RGB15(14, 14, 14), RGB15(22, 22, 22), RGB15(31, 31, 31) };
    glColorTableEXT(0, 0, 4, 0, 0, pal);
    treeTexParam = id;
    glBindTexture(0, 0);
}

/* ------------------------------------------------------------------ input */
static void pressButton(int id)
{
    if (id < 8) {
        if (cine.on) { cine.on = 0; uiTimer = 0; }
        setShot(BTNSHOTS[id]);
        hudHidden = 0;
    } else if (id == B_CINE) setCine(!cine.on);
    else if (id == B_SPEED) { spI = (spI + 1) % 3; speedT = SPEEDS[spI]; }
    else if (id == B_SNOW) { snowOn = !snowOn; }
    else if (id == B_SOUND) {
        soundOn = !soundOn;
        if (soundOn) { audioStart(); showHint("TON AN", 1.2f); }
        else audioStop();
    } else if (id == B_HUD) { hudHidden = !hudHidden; uiTimer = 0; }
    subDirty = hudDirty = 1;
}
static void cycleShot(int dir)
{
    int idx = 0;
    for (int i = 0; i < 8; i++) if (BTNSHOTS[i] == cur.key) idx = i;
    idx = (idx + dir + 8) % 8;
    pressButton(B_SHOT0 + idx);
}

/* ------------------------------------------------------------------ main */
static volatile int vblanks = 0;
static void onVBlank(void) { vblanks++; }

static int chunkVisible(Chunk *c, float *dist)
{
    V3 t = vsub(c->c, camP);
    float dd = sqrtf(vdot(t, t));
    *dist = dd;
    const float rad = 130;
    if (dd > 400 + rad) return 0;
    float z = -vdot(camBack, t);
    if (z < -rad) return 0;
    float tanh_ = curTanH;
    float x = vdot(camRight, t), y = vdot(camUp, t);
    float zz = z > 1 ? z : 1;
    if (fabsf(x) - rad * 1.3f > zz * tanh_) return 0;
    if (fabsf(y) - rad * 1.2f > zz * tanh_ * 0.75f + 40) return 0;
    return 1;
}

static u32 rt[4]; static int fpsShow = 0, fpsCnt = 0, fpsV0 = 0; static u32 tacc[4], tmax[4], tcnt; static int dbgPoly = 0, dbgVert = 0, dbgFrames = 0;
static float drawDist = 400;

static void renderScene(float dt)
{
    // chunks sorted by distance
    int idx[NCHUNK]; float dist[NCHUNK]; int n = 0;
    for (int i = 0; i < NCHUNK; i++) {
        Chunk *c = &chunks[i];
        if (!c->used || !c->ready) continue;
        float dd;
        if (!chunkVisible(c, &dd)) continue;
        if (dd > drawDist + 100) continue;
        int j = n++;
        while (j > 0 && dist[j - 1] > dd) { idx[j] = idx[j - 1]; dist[j] = dist[j - 1]; j--; }
        idx[j] = i; dist[j] = dd;
    }
    u32 r0 = cpuGetTiming();
    // train
    for (int i = 0; i < NCARS; i++) {
        Car *c = &cars[i];
        loadMV(&c->X, &c->Y, &c->Z, 4, c->pos);
        glCallList(c->ext);
        if (c->wins && !(c == SEATCAR && cur.key == S_SEAT)) glCallList(c->wins);
        if (c == SEATCAR && cur.key == S_SEAT && c->interior) glCallList(c->interior);
        if (i == 0 && cur.key == S_CAB && c->interior) glCallList(c->interior);
    }
    // near/far chunks
    for (int q = 0; q < n; q++) {
        Chunk *c = &chunks[idx[q]];
        loadMV(NULL, NULL, NULL, 32, v3(c->ox, c->oy, c->oz));
        if (dist[q] < 150) { if (c->lNear) glCallList(c->lNear); }
        else if (c->lFar) glCallList(c->lFar);
        if (dist[q] < 130 && c->lDet) glCallList(c->lDet);
    }
    u32 r1 = cpuGetTiming();
    if (snowOn) updateDrawSnow(dt);
    u32 r2 = cpuGetTiming();
    // trees (textured)
    glBindTexture(0, treeTexParam);
    for (int q = 0; q < n; q++) {
        Chunk *c = &chunks[idx[q]];
        if (dist[q] > 300 || !c->lTree) continue;
        loadMV(NULL, NULL, NULL, 32, v3(c->ox, c->oy, c->oz));
        glCallList(c->lTree);
    }
    glBindTexture(0, 0);
    GFX_TEX_FORMAT = 0;
    u32 r3 = cpuGetTiming();
    // translucent: sky first
    loadMV(NULL, NULL, NULL, 64, camP);
    glCallList(skyList);
    for (int q = 0; q < n; q++) {
        Chunk *c = &chunks[idx[q]];
        if (!c->lTrans || dist[q] > 250) continue;
        loadMV(NULL, NULL, NULL, 32, v3(c->ox, c->oy, c->oz));
        glCallList(c->lTrans);
    }
    if (cur.key != S_CAB) {
        drawHeadPool();
        Car *c = &cars[0];
        loadMV(&c->X, &c->Y, &c->Z, 16, c->pos);
        glCallList(beamList);
    }
    u32 r4 = cpuGetTiming();
    rt[0] += r1 - r0; rt[1] += r2 - r1; rt[2] += r3 - r2; rt[3] += r4 - r3;
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    if (isDSiMode()) setCpuClock(true);

    // video setup
    videoSetMode(MODE_5_3D);
    videoSetModeSub(MODE_5_2D);
    vramSetBankA(VRAM_A_TEXTURE);
    vramSetBankB(VRAM_B_MAIN_BG_0x06000000);
    vramSetBankC(VRAM_C_SUB_BG);
    vramSetBankF(VRAM_F_TEX_PALETTE);
    int bg2 = bgInit(2, BgType_Bmp8, BgSize_B8_256x256, 0, 0);
    int bg3 = bgInit(3, BgType_Bmp8, BgSize_B8_256x256, 4, 0);
    int sbg = bgInitSub(3, BgType_Bmp8, BgSize_B8_256x256, 0, 0);
    sbgId = sbg;
    bgSetPriority(bg3, 0); bgSetPriority(bg2, 1); REG_BG0CNT = (REG_BG0CNT & ~3) | 2;
    REG_BLDCNT = BLEND_ALPHA | BLEND_SRC_BG2 | BLEND_DST_BG0;
    REG_BLDALPHA = 9 | (7 << 8);
    uiPalettes();
    layoutButtons();

    irqSet(IRQ_VBLANK, onVBlank);
    irqEnable(IRQ_VBLANK);

    // seed
    uint32_t seed = (uint32_t)time(NULL) * 2654435761u ^ 0x5bd1e995u;
    rs = seed;
    SOi = (uint32_t)(rnd() * 1e9f);
    TR.h = rnd() * 6.28f; TR.seg = 180; TR.gseg = 200;

    // loading screen
    memset(subBuf, C_BG0, sizeof(subBuf));
    drawStr(subBuf, 88, 80, "HIGH TRAIN", C_TITLE, 1);
    drawStr(subBuf, 96, 104, "LADE ...", C_TXT, 0);
    DC_FlushRange(subBuf, sizeof(subBuf));
    dmaCopy(subBuf, bgGetGfxPtr(sbg), sizeof(subBuf));

    // 3D setup
    glInit();
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glEnable(GL_FOG);
    glDisable(GL_TOON_HIGHLIGHT); // toon mode
    initToon();
    glSetToonTable(toonTab);
    uint16_t clr = palMix(toonIdx(hexLum(0x1f3a62)) * 6.0f / 31.0f);
    glClearColor(clr & 31, (clr >> 5) & 31, (clr >> 10) & 31, 31);
    glClearPolyID(63);
    glClearDepth(GL_MAX_DEPTH);
    glViewport(0, 0, 255, 191);
    setupFog();
    makeTreeTexture();
    soundEnable();

    for (int i = 0; i < SN; i++) snv[i] = (s32)((0.7f + rnd() * 0.8f) * 4096);

    makeLoco(&cars[0]);
    for (int i = 1; i < NCARS; i++) makeCar(&cars[i], i == 2);
    TRAINLEN = 0;
    for (int i = 0; i < NCARS; i++) TRAINLEN += cars[i].L;
    TRAINLEN += GAP * (NCARS - 1);

    // world
    while (trEnd < (int)D + 560) genPoint();
    nextChunk = (int)((D - 300) / CH); if (nextChunk < 0) nextChunk = 0;
    streamWorld(1);
    while (nextChunk * CH < D + 440 || bld.active) streamWorld(1);
    placeTrain();
    buildSky();
    memset(stageMax, 0, sizeof(stageMax));

#ifdef AUTOSHOT
    cine.on = 0;
    setShot(AUTOSHOT);
#else
    setCine(1);
#endif
    camPos = cur.pos; camLook = cur.look;
    setView(camPos, camLook);
    resetSnow(0, NULL);
    showHint("UNTEN TIPPEN: KAMERAS", 4);
#ifdef AUTOSOUND
    pressButton(B_SOUND);
#endif
#ifdef AUTOFAST
    pressButton(B_SPEED);
#endif

    cpuStartTiming(2);
    int lastV = vblanks;
    float hudT = 0;
    while (1) {
        // timing
        int nowV = vblanks;
        int frames = nowV - lastV; if (frames < 1) frames = 1;
        lastV = nowV;
        float dt = frames / 59.83f; if (dt > 0.05f) dt = 0.05f;
        T += dt;
        fpsCnt++;
        if (nowV - fpsV0 >= 60) { fpsShow = fpsCnt * 60 / (nowV - fpsV0); fpsCnt = 0; fpsV0 = nowV; }

        // input
        scanKeys();
        int kd = keysDown(), kh = keysHeld();
        if (kd) {
            if (subDim && (kd & KEY_TOUCH)) { kd = 0; }
            subIdle = 0;
            if (subDim) { subDim = 0; REG_MASTER_BRIGHT_SUB = 0; }
        }
        if (kd & KEY_TOUCH) {
            touchPosition tp; touchRead(&tp);
            for (int i = 0; i < nbtn; i++) {
                Btn *b = &btns[i];
                if (tp.px >= b->x && tp.px < b->x + b->w && tp.py >= b->y && tp.py < b->y + b->h) { pressButton(b->id); break; }
            }
        }
        if (kd & KEY_A) pressButton(B_CINE);
        if (kd & KEY_X) pressButton(B_SPEED);
        if (kd & KEY_Y) pressButton(B_SNOW);
        if (kd & KEY_SELECT) pressButton(B_SOUND);
        if (kd & KEY_START) pressButton(B_HUD);
        if (kd & KEY_R) cycleShot(1);
        if (kd & KEY_L) cycleShot(-1);
        if (SHOTDIRECT[cur.key] && cur.key != S_WHEEL && (kh & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT))) {
            if (kh & KEY_LEFT) lookYaw += 1.6f * dt;
            if (kh & KEY_RIGHT) lookYaw -= 1.6f * dt;
            if (kh & KEY_UP) lookPitch = fminf(0.7f, lookPitch + 1.2f * dt);
            if (kh & KEY_DOWN) lookPitch = fmaxf(-0.8f, lookPitch - 1.2f * dt);
            if (cine.on) cine.t = fminf(cine.t, cine.dur - 4);
        }
        if (kd & (KEY_B)) { if (!cine.on) { } }

        // simulation
        speed += (speedT - speed) * (1 - expf(-dt * 0.6f));
        D += speed * dt;
        u32 t0 = cpuGetTiming();
        streamWorld(0);
        u32 t1 = cpuGetTiming();
        placeTrain();
        if (cine.on) { cine.t += dt; cine.clock += dt; if (cine.t > cine.dur) nextCine(); }
#ifdef AUTOCYCLE
        { static float ac = 0; ac += dt; if (ac > 4) { ac = 0; setShot((cur.key + 1) % S_COUNT); } }
#endif
        updateCam(dt);
        updateAudio();
        u32 t2 = cpuGetTiming();

        // fade on cut (master brightness)
        fade = fminf(1.05f, fade + dt * 1.8f);
        int fb = (int)((1 - fminf(1, fade)) * 14);
        REG_MASTER_BRIGHT = fb > 0 ? ((2 << 14) | fb) : 0;

        // ui timers
        if (cine.on && uiTimer > 0) { uiTimer -= dt; if (uiTimer <= 0) { hudHidden = 1; hudDirty = subDirty = 1; } }
        if (hintTimer > 0) { hintTimer -= dt; if (hintTimer <= 0) hudDirty = 1; }
        subIdle += dt;
        if (cine.on && !subDim && subIdle > 6) { subDim = 1; REG_MASTER_BRIGHT_SUB = (2 << 14) | 12; }
        hudT += dt;
        if (hudT > 0.25f) { hudT = 0; hudDirty = 1; subDirty = 1; dbgFrames++; }

        // render 3D
        u32 t3 = cpuGetTiming();
        renderScene(dt);
        u32 t4 = cpuGetTiming();
        if (t1 - t0 > (u32)tmax[0]) tmax[0] = t1 - t0;
        tacc[0] += t1 - t0; tacc[1] += t2 - t1; tacc[2] += t4 - t3; tcnt++;
        dbgPoly = GFX_POLYGON_RAM_USAGE; dbgVert = GFX_VERTEX_RAM_USAGE;
        glFlush(GL_TRANS_MANUALSORT);

        if (hudDirty) {
            hudDirty = 0;
            if (hudHidden) { bgHide(bg2); bgHide(bg3); }
            else { bgShow(bg2); bgShow(bg3); drawHUD(); }
        }
        if (subDirty) {
            subDirty = 0;
            drawSub();
#ifdef DEBUGSTATS
            {
                char b[48];
                if (!tcnt) tcnt = 1;
                { int mx = 0, mi = 0; for (int q = 0; q < 19; q++) if ((int)stageMax[q] > mx) { mx = stageMax[q]; mi = q; } sprintf(b, "MAX st%d=%d s7=%d s8=%d s11=%d", mi, mx, (int)stageMax[7], (int)stageMax[8], (int)stageMax[11]); } if (0) sprintf(b, "FPS%d V%d %d %d %d|%d %d %d %d", fpsShow, dbgVert, (int)(tmax[0] / 33), (int)(tacc[1] / tcnt / 33), (int)(tacc[2] / tcnt / 33), (int)(rt[0]/tcnt/33), (int)(rt[1]/tcnt/33), (int)(rt[2]/tcnt/33), (int)(rt[3]/tcnt/33));
                rt[0] = rt[1] = rt[2] = rt[3] = 0;
                tacc[0] = tacc[1] = tacc[2] = 0; tcnt = 0; tmax[0] = 0;
                fillRect(subBuf, 0, 184, 256, 8, C_BG0);
                drawStr(subBuf, 4, 184, b, C_TITLE, 0);
                DC_FlushRange(subBuf, sizeof(subBuf));
                dmaCopy(subBuf, bgGetGfxPtr(sbg), sizeof(subBuf));
            }
#endif
        }
        swiWaitForVBlank();
    }
    return 0;
}
