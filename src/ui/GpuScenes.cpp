#include "GpuScenes.h"

namespace gx::ui
{
using namespace juce::gl;

namespace
{
const char* vertexShader = R"GLSL(
attribute vec2 position;
void main() { gl_Position = vec4 (position, 0.0, 1.0); }
)GLSL";

// ------------------------------------------------------------------ shared GLSL
const char* prelude = R"GLSL(
uniform vec2 uRes;
uniform float uTime, uMus, uEvoT, uLight, uEvo, uDensity, uSpace, uEnergy, uRun, uFrozen, uSeed, uFade;
uniform float uPres[8];
uniform float uGlow[8];
// layers: 0 drone 1 pad 2 texture 3 shimmer 4 wind 5 bells 6 bass
vec2 S;
float hash12 (vec2 p) { vec3 p3 = fract (vec3 (p.xyx) * 0.1031); p3 += dot (p3, p3.yzx + 33.33); return fract ((p3.x + p3.y) * p3.z); }
vec2 hash22 (vec2 p) { vec3 p3 = fract (vec3 (p.xyx) * vec3 (0.1031, 0.1030, 0.0973)); p3 += dot (p3, p3.yzx + 33.33); return fract ((p3.xx + p3.yz) * p3.zy); }
float noise (vec2 p)
{
    vec2 i = floor (p), f = fract (p);
    f = f * f * (3.0 - 2.0 * f);
    return mix (mix (hash12 (i), hash12 (i + vec2 (1.0, 0.0)), f.x), mix (hash12 (i + vec2 (0.0, 1.0)), hash12 (i + vec2 (1.0, 1.0)), f.x), f.y);
}
float fbm (vec2 p)
{
    float v = 0.0, a = 0.5;
    mat2 m = mat2 (1.6, 1.2, -1.2, 1.6);
    for (int i = 0; i < 6; i++) { v += a * noise (p); p = m * p; a *= 0.5; }
    return v;
}
float fbm3 (vec2 p)
{
    float v = 0.0, a = 0.5;
    mat2 m = mat2 (1.6, 1.2, -1.2, 1.6);
    for (int i = 0; i < 3; i++) { v += a * noise (p); p = m * p; a *= 0.5; }
    return v;
}
float ridged (vec2 p)
{
    float v = 0.0, a = 0.55;
    mat2 m = mat2 (1.7, 1.1, -1.1, 1.7);
    for (int i = 0; i < 6; i++) { float n = 1.0 - abs (noise (p) * 2.0 - 1.0); v += a * n * n; p = m * p; a *= 0.5; }
    return v;
}
// soft round stars on a grid, twinkling
float stars (vec2 p, float density, float size)
{
    vec2 g = floor (p), f = fract (p);
    vec2 o = 0.2 + 0.6 * hash22 (g);
    float on = step (1.0 - density, hash12 (g + 7.1));
    float tw = 0.55 + 0.45 * sin (uTime * (0.7 + 2.5 * hash12 (g + 3.3)) + hash12 (g) * 40.0);
    float d = length (f - o);
    return on * tw * (smoothstep (size, 0.0, d) + 0.25 * smoothstep (size * 4.0, 0.0, d));
}
float rockShade (vec2 q, float sunSide)
{
    float e = 0.01;
    float h0 = fbm (q), hx = fbm (q + vec2 (e, 0.0)), hy = fbm (q + vec2 (0.0, e));
    vec3 n = normalize (vec3 ((h0 - hx) * 3.0, (h0 - hy) * 3.0, 0.08));
    vec3 l = normalize (vec3 (sunSide * 0.7, 0.6, 0.4));
    return clamp (0.25 + 0.95 * max (dot (n, l), 0.0), 0.0, 1.2);
}
vec3 skyNight() { return vec3 (0.010, 0.014, 0.045); }
// colour of the sky at height t (0 horizon .. 1 top) for a given light
vec3 skyGrad (float t, float L)
{
    vec3 top = mix (mix (vec3 (0.01, 0.015, 0.05), vec3 (0.10, 0.08, 0.27), clamp (L * 2.0, 0.0, 1.0)), vec3 (0.16, 0.38, 0.70), clamp (L * 2.0 - 1.0, 0.0, 1.0));
    vec3 low = mix (mix (vec3 (0.07, 0.10, 0.25), vec3 (0.90, 0.47, 0.36), clamp (L * 2.0, 0.0, 1.0)), vec3 (0.95, 0.85, 0.67), clamp (L * 2.0 - 1.0, 0.0, 1.0));
    return mix (low, top, pow (clamp (t, 0.0, 1.0), 0.55));
}
vec3 aces (vec3 x) { return clamp ((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }
vec3 post (vec3 c, vec2 uv)
{
    c = aces (c * 1.05);
    c = mix (c, c * vec3 (0.96, 1.0, 1.06) + vec3 (0.0, 0.006, 0.014), 1.0 - uv.y);        // cool shadows
    c = mix (c, c * vec3 (1.05, 1.0, 0.94), uv.y * 0.6);                                    // warm air
    vec2 v = (uv - 0.5) * vec2 (1.0, 1.15);
    c *= 1.0 - 0.5 * pow (clamp (length (v) * 1.25, 0.0, 1.0), 2.4);                       // vignette
    c += (hash12 (uv * uRes + fract (uTime * 7.3) * 113.0) - 0.5) * 0.018;                 // film grain
    c = mix (c, c * vec3 (0.86, 0.96, 1.14) + 0.025, uFrozen * 0.5);
    c *= mix (0.5, 1.0, uRun) * uFade;
    return c;
}
)GLSL";

const char* mainTail = R"GLSL(
void main()
{
    S = vec2 (fract (uSeed * 0.6180339) * 97.0, fract (uSeed * 0.7548776) * 89.0);
    vec2 uv0 = gl_FragCoord.xy / uRes;
    float z = 1.03 + 0.015 * sin (uTime * 0.021);                      // slow cinematic camera
    vec2 uv = (uv0 - 0.5) / z + 0.5 + vec2 (sin (uTime * 0.013) * 0.007, sin (uTime * 0.017 + 1.0) * 0.005);
    gl_FragColor = vec4 (post (scene (uv), uv0), 1.0);
}
)GLSL";

// ------------------------------------------------------------------ Valley
const char* valley = R"GLSL(
vec3 scene (vec2 uv)
{
    float L = uLight, asp = uRes.x / uRes.y;
    vec2 p = vec2 (uv.x * asp, uv.y);
    float hor = 0.30;
    vec3 col = skyGrad ((uv.y - hor) / (1.0 - hor), L);
    vec3 low = skyGrad (0.0, L);
    // sun / moon on the evolution clock
    float ph = fract (uEvoT / 420.0 + fract (uSeed * 0.0137));
    vec2 sp = vec2 ((0.1 + 0.8 * ph) * asp, hor + 0.03 + sin (ph * 3.14159) * 0.52);
    vec3 sc = mix (vec3 (0.85, 0.9, 1.05), vec3 (1.25, 0.95, 0.6), step (0.5, L));
    float sd = length (p - sp);
    col += sc * 0.018 / (sd * sd + 0.01) * (0.25 + 0.25 * uEnergy);
    col = mix (col, sc * 1.6, smoothstep (0.042, 0.036, sd));
    col += vec3 (stars (p * 110.0 + S, 0.09, 0.08)) * clamp ((0.6 - L) * 2.2, 0.0, 1.0);
    // aurora veil (Shimmer) in the night sky
    float au = uPres[3] * clamp ((0.65 - L) * 2.0, 0.0, 1.0);
    if (au > 0.0)
    {
        float n = fbm (vec2 (p.x * 1.2 + uTime * 0.02, uTime * 0.03) + S);
        float yb = 0.72 + (n - 0.5) * 0.3;
        float a = exp (-abs (uv.y - yb) * 9.0) * fbm (vec2 (p.x * 6.0, uv.y * 2.0 - uTime * 0.05));
        col += vec3 (0.2, 0.9, 0.65) * a * au * (0.5 + uGlow[3]);
    }
    // volumetric clouds lit by the sun
    vec2 cp = vec2 (p.x * 1.3 + uMus * 0.006, uv.y * 4.5) + S;
    float d1 = fbm (cp), d2 = fbm (cp + normalize (sp - p) * 0.06);
    float cl = smoothstep (0.48, 0.82, d1) * smoothstep (hor, hor + 0.3, uv.y) * (0.6 + 0.5 * uDensity);
    float lit = clamp ((d1 - d2) * 6.0 + 0.5, 0.0, 1.0);
    vec3 cc = mix (low * 0.55 + 0.02, sc * mix (0.5, 1.1, L), lit);
    col = mix (col, cc, cl * 0.85);
    // mountain ranges: ridged fractal terrain with aerial perspective (Drone far, Pad middle, Texture near)
    float layerY[3]; layerY[0] = hor + 0.20; layerY[1] = hor + 0.10; layerY[2] = hor + 0.02;
    for (int i = 0; i < 3; i++)
    {
        float fi = float (i);
        float amp = (0.24 - 0.05 * fi) * (0.45 + 0.55 * uPres[i]);
        float x = p.x * (0.9 + 0.7 * fi) + uMus * (0.002 + 0.006 * fi) + S.x + fi * 11.0;
        float h = layerY[i] - 0.08 + amp * ridged (vec2 (x, fi * 3.7));
        if (uv.y < h)
        {
            float depthIn = h - uv.y;
            float shade = rockShade (vec2 (x * 5.0, uv.y * 9.0 + fi * 4.0), sp.x > p.x ? 1.0 : -1.0);
            float tex = fbm (vec2 (x * 14.0, uv.y * 40.0));
            vec3 rock = mix (vec3 (0.025, 0.03, 0.05), vec3 (0.13, 0.12, 0.16), tex) * shade * (1.0 - 0.5 * smoothstep (0.0, 0.2, depthIn));
            rock *= 0.35 + 0.65 * L;
            if (i == 0) rock = mix (rock, vec3 (0.85, 0.88, 0.95) * (0.35 + 0.65 * L) * shade, smoothstep (0.03, 0.0, h - uv.y) * smoothstep (0.45, 0.6, fbm3 (vec2 (x * 9.0, uv.y * 25.0))));
            if (i == 2) { float trees = h - 0.012 * (0.6 + 0.8 * noise (vec2 (x * 140.0, 0.0))); if (uv.y > trees) rock = vec3 (0.01, 0.015, 0.02); }
            float rim = smoothstep (0.01, 0.0, h - uv.y) * (0.3 + 0.7 * uGlow[i]);
            float fog = (0.62 - 0.27 * fi) * (0.6 + 0.3 * uSpace) * (1.0 - 0.5 * smoothstep (0.0, 0.25, h - uv.y));
            vec3 air = mix (skyGrad (0.75, L), low, 0.35) * (0.75 - 0.15 * fi);   // aerial perspective: distant ranges turn blue
            col = mix (rock * (1.25 - 0.2 * fi), air, fog) + sc * rim * 0.15 * (1.0 - fi * 0.3);
        }
        // mist between the ranges (Wind)
        float mist = uPres[4] * smoothstep (0.07, 0.0, abs (uv.y - (layerY[i] - 0.06))) * fbm3 (vec2 (p.x * 3.0 - uMus * 0.03 * (1.0 + fi), uv.y * 10.0));
        col = mix (col, mix (low, skyGrad (0.6, L), 0.4) * 1.05 + 0.02, mist * 0.3);
    }
    // lake: mirrored sky with ripples and a glittering path
    float lakeTop = hor - 0.02;
    if (uv.y < lakeTop && uv.y > 0.07)
    {
        float dd = lakeTop - uv.y;
        float rip = (fbm3 (vec2 (p.x * 8.0, dd * 60.0 - uTime * 0.6)) - 0.5) * 0.04 * (0.4 + uPres[4]);
        vec3 refl = skyGrad (dd * 2.2 + rip, L) * 0.55;
        float glit = pow (noise (vec2 (p.x * 60.0, dd * 300.0 + uTime * 2.0)), 10.0) * exp (-abs (p.x - sp.x) * (6.0 - dd * 30.0));
        col = mix (refl, col, 0.15) + sc * glit * 1.4 * (0.4 + uGlow[2]);
    }
    // foreground meadow
    float gh = 0.07 + 0.025 * fbm3 (vec2 (p.x * 4.0 + uMus * 0.02, 1.0));
    if (uv.y < gh) col = mix (vec3 (0.02, 0.025, 0.03), low * 0.15, 0.4) * (0.6 + 0.4 * fbm (p * 40.0));
    return col;
}
)GLSL";

// ------------------------------------------------------------------ Starfield
const char* starfield = R"GLSL(
vec3 planetPal (float k, float t)
{
    if (k < 0.25) return mix (vec3 (0.05, 0.12, 0.25), vec3 (0.35, 0.6, 0.85), t);
    if (k < 0.5)  return mix (vec3 (0.25, 0.08, 0.04), vec3 (0.85, 0.55, 0.32), t);
    if (k < 0.75) return mix (vec3 (0.10, 0.05, 0.22), vec3 (0.6, 0.45, 0.85), t);
    return mix (vec3 (0.02, 0.12, 0.10), vec3 (0.35, 0.75, 0.62), t);
}
vec3 scene (vec2 uv)
{
    float asp = uRes.x / uRes.y;
    vec2 p = vec2 (uv.x * asp, uv.y);
    vec3 col = mix (vec3 (0.004, 0.006, 0.018), vec3 (0.015, 0.02, 0.05), uv.y);
    // faint nebular gas (Shimmer) behind everything
    float n1 = fbm (p * 1.4 + S + vec2 (uMus * 0.002, 0.0)), n2 = fbm (p * 2.7 - S.yx);
    col += vec3 (0.16, 0.08, 0.28) * pow (n1, 3.0) * 1.4 + vec3 (0.03, 0.14, 0.22) * pow (n2, 4.0) * (1.0 + 2.0 * uPres[3]);
    // three depths of stars, drifting with Evolution
    for (int i = 0; i < 3; i++)
    {
        float fi = float (i);
        vec2 q = (p + vec2 (uMus * (0.0015 + 0.003 * fi) * (0.5 + uEvo), 0.0)) * (45.0 + 40.0 * fi) + S * (fi + 1.0);
        col += vec3 (0.85, 0.9, 1.0) * stars (q, 0.10, 0.07) * (0.35 + 0.3 * fi);
    }
    // planet (Drone): lit sphere, cloud bands, atmosphere, rings
    float k = hash12 (S);
    vec2 c = vec2 (asp * (k < 0.5 ? 0.27 : 0.74), 0.02 + 0.25 * hash12 (S + 3.0));
    float R = 0.42 + 0.18 * hash12 (S + 5.0);
    vec2 d = (p - c) / R;
    float r2 = dot (d, d);
    vec3 ldir = normalize (vec3 (k < 0.5 ? 0.7 : -0.7, 0.45, 0.55));
    bool hasRing = hash12 (S + 9.0) < 0.6;
    float ca = cos (-0.3), sa = sin (-0.3);
    vec2 rd = vec2 (ca * d.x - sa * d.y, sa * d.x + ca * d.y);
    float re = length (vec2 (rd.x, rd.y / 0.24));
    float ringBand = hasRing ? smoothstep (1.35, 1.4, re) * smoothstep (1.95, 1.88, re) * (0.55 + 0.45 * fbm3 (vec2 (re * 40.0, 0.0))) : 0.0;
    vec3 ringCol = planetPal (k, 0.85) * 0.9;
    // ring behind the planet (upper half)
    if (ringBand > 0.0 && rd.y > 0.0 && r2 > 1.0) col = mix (col, ringCol * (0.4 + 0.6 * max (dot (vec3 (0.0, 0.0, 1.0), ldir), 0.2)), ringBand * 0.8);
    float atm = exp (-max (sqrt (r2) - 1.0, 0.0) * 12.0) * step (1.0, r2);
    col += planetPal (k, 0.9) * atm * (0.25 + 0.4 * uGlow[0]);
    if (r2 < 1.0)
    {
        float zz = sqrt (1.0 - r2);
        vec3 n = vec3 (d, zz);
        float dif = max (dot (n, ldir), 0.0);
        float lon = atan (n.x, n.z) + uTime * 0.008, lat = n.y;
        float bands = fbm (vec2 (lon * 1.5, lat * 9.0 + fbm3 (vec2 (lon * 3.0, lat * 4.0)) * 1.5) + S);
        vec3 base = planetPal (k, bands);
        float storm = smoothstep (0.72, 0.8, fbm3 (vec2 (lon * 4.0 + 2.0, lat * 6.0)));
        base = mix (base, planetPal (k, 1.0) * 1.2, storm * 0.5);
        vec3 pc = base * (0.03 + 1.25 * pow (dif, 0.9));
        pc += planetPal (k, 0.95) * pow (1.0 - zz, 3.0) * 0.9 * (0.4 + uGlow[0]);              // atmosphere rim
        // ring shadow on the planet
        col = pc;
    }
    // ring in front (lower half)
    if (ringBand > 0.0 && (rd.y <= 0.0 || r2 > 1.0) && !(rd.y > 0.0 && r2 > 1.0))
    {
        float lightOnRing = 0.35 + 0.65 * clamp (0.5 + 0.5 * rd.x * sign (ldir.x), 0.0, 1.0);
        col = mix (col, ringCol * lightOnRing, ringBand * 0.85);
    }
    // a small moon (Pad) orbiting slowly
    float ma = uEvoT * 0.02 + S.x;
    vec2 mc = c + vec2 (cos (ma) * R * 2.3, R * 0.85 + sin (ma) * R * 0.35);
    float mr = R * 0.11 * (0.7 + 0.3 * uPres[1]);
    vec2 md = (p - mc) / mr;
    float m2 = dot (md, md);
    if (m2 < 1.0)
    {
        vec3 mn = vec3 (md, sqrt (1.0 - m2));
        float mdif = max (dot (mn, ldir), 0.0);
        float crat = fbm (md * 3.0 + S);
        col = vec3 (0.75, 0.72, 0.8) * (0.6 + 0.4 * crat) * (0.02 + mdif);
    }
    return col;
}
)GLSL";

// ------------------------------------------------------------------ Underwater
const char* underwater = R"GLSL(
float caustic (vec2 p, float t)
{
    vec2 i = p;
    float c = 1.0, inten = 0.005;
    for (int n = 0; n < 4; n++)
    {
        float tt = t * (1.0 - (3.5 / float (n + 1)));
        i = p + vec2 (cos (tt - i.x) + sin (tt + i.y), sin (tt - i.y) + cos (tt + i.x));
        c += 1.0 / length (vec2 (p.x / (sin (i.x + tt) / inten), p.y / (cos (i.y + tt) / inten)));
    }
    c /= 4.0;
    c = 1.17 - pow (c, 1.4);
    return clamp (pow (abs (c), 8.0), 0.0, 1.0);
}
// organic web of refracted light (caustics): bright where two slow noise fields cross their middle value
float web (vec2 q)
{
    return smoothstep (0.07, 0.0, abs (fbm3 (q) - 0.5)) * 0.7 + smoothstep (0.05, 0.0, abs (fbm3 (q * 1.7 + 3.1) - 0.5)) * 0.5;
}
// a layer of giant kelp: olive-golden stipes with alternating blades, rounded shading, back-lit translucent edges,
// fogged by distance (layer 0 far ... 2 near)
void kelp (inout vec3 col, vec2 p, vec2 uv, float layer, float bed, float asp, float L)
{
    float scale = 0.5 + 0.25 * layer;
    float fog = 0.78 - 0.32 * layer;
    for (int k = 0; k < 7; k++)
    {
        float fk = float (k) + layer * 13.0;
        if (hash12 (S + vec2 (fk, 9.0)) < 0.25) continue;
        float x0 = ((float (k) + 0.15 + 0.7 * hash12 (S + vec2 (fk, 1.0))) / 7.0 * 1.1 - 0.05) * asp;
        float hk = (0.4 + 0.5 * hash12 (S + vec2 (fk, 2.0))) * (0.55 + 0.55 * uPres[1]) * (0.55 + 0.45 * scale);
        float base = bed + 0.03 * (2.0 - layer);
        float yy = (uv.y - base) / hk;
        if (yy < 0.0 || yy > 1.0) continue;
        float sway = (sin (uTime * 0.45 + fk * 1.3 + yy * 2.2) * 0.05 + sin (uTime * 0.21 + fk) * 0.025) * yy * yy;
        float wdt = (0.004 + 0.006 * scale) * (1.0 - 0.45 * yy);
        float leaf = 0.5 + 0.5 * smoothstep (-0.6, 0.9, sin (yy * (16.0 + 5.0 * hash12 (vec2 (fk, 4.0))) + fk * 3.0));   // smooth blades
        float side = sin (yy * 11.0 + fk);                              // blades lean to alternate sides, smoothly
        float w = wdt * (0.8 + 2.2 * leaf);
        float dx = p.x - x0 - sway - side * w * 0.35 * leaf;
        float aa = 1.5 / uRes.y;   // anti-aliased edge, whatever the resolution
        float a = smoothstep (w + aa, w - aa, abs (dx)) * smoothstep (1.0, 0.92, yy);
        if (a <= 0.0) continue;
        float across = clamp (dx / w, -1.0, 1.0);
        float shade = 0.5 + 0.5 * sqrt (1.0 - across * across);
        float back = smoothstep (0.55, 1.0, abs (across));
        vec3 c = mix (vec3 (0.06, 0.09, 0.04), vec3 (0.3, 0.36, 0.13), 0.25 + 0.75 * yy) * shade;
        c += vec3 (0.22, 0.42, 0.14) * back * (0.3 + 0.7 * uv.y) * (0.4 + 0.6 * L);
        c *= (0.45 + 0.75 * uv.y) * (0.55 + 0.6 * L);
        col = mix (col, mix (c, col, fog), a);
    }
}
vec3 scene (vec2 uv)
{
    float L = uLight, asp = uRes.x / uRes.y;
    vec2 p = vec2 (uv.x * asp, uv.y);
    vec3 deep = vec3 (0.003, 0.025, 0.05), mid = mix (vec3 (0.015, 0.13, 0.2), vec3 (0.04, 0.3, 0.4), L), top = mix (vec3 (0.05, 0.32, 0.42), vec3 (0.22, 0.66, 0.76), L);
    vec3 col = mix (deep, mid, smoothstep (0.0, 0.65, uv.y));
    col = mix (col, top, smoothstep (0.5, 1.0, uv.y));
    col *= 0.85 + 0.3 * fbm3 (vec2 (p.x * 1.5 + uTime * 0.01, uv.y * 2.0) + S);           // light scattered in the water
    // the surface from below: a bright, rippling window with a web of refracted light
    float sy = smoothstep (0.84, 1.0, uv.y);
    float sw = web (vec2 (p.x * 5.0, (1.0 - uv.y) * 22.0) + vec2 (uTime * 0.15, uTime * 0.25));
    float window = exp (-pow ((uv.x - 0.5 - 0.15 * sin (uTime * 0.03)) * 1.7, 2.0));
    col += mix (top, vec3 (0.85, 1.0, 1.0), 0.6) * sy * sy * (0.35 + 0.65 * window) * (0.25 + 0.9 * sw) * (0.5 + 0.6 * L);
    // volumetric light rays (Shimmer)
    float xs = p.x + (1.0 - uv.y) * 0.35;
    float rays = pow (noise (vec2 (xs * 4.0 + S.x, uTime * 0.1)), 3.0) + 0.6 * pow (noise (vec2 (xs * 9.0 - S.y, uTime * 0.17)), 4.0);
    col += vec3 (0.5, 0.88, 1.0) * rays * pow (uv.y, 1.3) * (0.16 + 0.42 * uPres[3]) * (0.5 + 0.7 * L);
    float bed = 0.21 + 0.05 * fbm3 (vec2 (p.x * 1.6 + S.x, 0.3)) + 0.015 * sin (p.x * 2.7);
    kelp (col, p, uv, 0.0, bed, asp, L);
    // a school of small fish far away, drifting across (Bass moves it)
    {
        vec2 sc = vec2 (fract (uMus * 0.003 + hash12 (S + 4.0)) * (asp + 0.8) - 0.4, 0.5 + 0.12 * sin (uTime * 0.05 + S.x));
        float fish = 0.0;
        for (int f = 0; f < 24; f++)
        {
            float ff = float (f);
            vec2 o = (vec2 (hash12 (vec2 (ff, 1.3)), hash12 (vec2 (ff, 7.1))) - 0.5) * vec2 (0.3, 0.09);
            o += 0.008 * vec2 (sin (uTime * 0.6 + ff), cos (uTime * 0.8 + ff * 1.7));
            vec2 q = p - sc - o;
            float body = length (q / vec2 (0.013, 0.0045));
            float tail = length ((q + vec2 (0.016, 0.0)) / vec2 (0.005, 0.005 + 0.004 * sin (uTime * 6.0 + ff)));
            fish = max (fish, max (smoothstep (1.0, 0.6, body), smoothstep (1.0, 0.6, tail) * step (q.x, -0.011)));
        }
        col = mix (col, col * 0.5 + vec3 (0.02, 0.05, 0.06) * L, fish * (0.45 + 0.3 * uPres[6]));
    }
    kelp (col, p, uv, 1.0, bed, asp, L);
    // marine snow drifting (Texture)
    for (int i = 0; i < 2; i++)
    {
        float fi = float (i);
        vec2 q = (p + vec2 (uMus * 0.004 * (1.0 + fi), -uTime * 0.006 * (1.0 + fi))) * (40.0 + 30.0 * fi) + S;
        col += vec3 (0.6, 0.95, 0.9) * stars (q, 0.12, 0.06) * (0.1 + 0.3 * uPres[2]) * (0.6 + 0.4 * uGlow[2]);
    }
    // jellyfish (Drone pulses, Pad tints): translucent glowing bells with trailing tentacles
    for (int j = 0; j < 4; j++)
    {
        float fj = float (j);
        float sp = 0.0025 + 0.002 * hash12 (S + fj);
        float life = fract (hash12 (S + fj + 9.0) + uMus * sp);
        vec2 jc = vec2 ((0.14 + 0.24 * fj) * asp + sin (uTime * 0.12 + fj * 2.0) * 0.05, 0.3 + 0.55 * life + 0.02 * sin (uTime * 0.8 + fj));
        float vis = smoothstep (0.0, 0.1, life) * smoothstep (1.0, 0.9, life);   // fades in below, out near the surface
        float beat = 0.5 + 0.5 * sin (uTime * (1.1 + 0.25 * fj) + fj * 1.7);
        float R = (0.055 + 0.03 * hash12 (vec2 (fj, 2.0))) * (0.85 + 0.3 * uPres[0]);
        vec3 jcol = fj == 0.0 ? vec3 (1.0, 0.45, 0.85) : (fj == 1.0 ? vec3 (0.45, 1.0, 0.9) : (fj == 2.0 ? vec3 (0.72, 0.62, 1.0) : vec3 (1.0, 0.6, 0.75)));
        // bell in its own frame (q.y up), pulsing: it narrows and lengthens on each beat
        vec2 q = (p - jc) / R;
        q.x /= 1.0 - 0.14 * beat;
        q.y /= 0.85 + 0.12 * beat;
        float rimY = -0.28 * q.x * q.x + 0.025 * sin (q.x * 26.0 + uTime * 2.0) * q.x * q.x;   // scalloped margin
        float dome = length (vec2 (q.x, max (q.y, 0.0) * 1.1));
        float inBell = smoothstep (1.0, 0.94, dome) * smoothstep (rimY - 0.03, rimY + 0.05, q.y);
        float edge = inBell * pow (dome, 5.0);                                          // gel is brighter where you look through more of it
        float canals = inBell * pow (abs (cos (atan (q.x, q.y + 0.4) * 8.0)), 24.0) * 0.35 * smoothstep (0.2, 0.9, dome);
        float gonads = smoothstep (0.3, 0.05, length (vec2 (abs (q.x) - 0.26, q.y - 0.3))) * inBell * 1.3;
        float marginGlow = smoothstep (0.06, 0.0, abs (q.y - rimY)) * step (abs (q.x), 1.0) * (0.6 + 0.4 * beat);
        // oral arms: three ruffled ribbons from the centre
        float tt = clamp (-q.y / 3.2, 0.0, 1.0);
        float arms = 0.0;
        for (int k = -1; k <= 1; k++)
        {
            float ax = q.x - float (k) * 0.16 - sin (uTime * 0.9 - tt * 7.0 + fj + float (k)) * 0.22 * tt;
            float w = 0.11 * (1.0 - 0.7 * tt) * (0.75 + 0.25 * sin (tt * 40.0 + float (k) * 2.0));
            arms += smoothstep (w, w * 0.2, abs (ax));
        }
        arms *= step (q.y, 0.0) * smoothstep (1.0, 0.25, tt);
        // long fine tentacles hanging from the margin
        float t2 = clamp ((rimY - q.y) / 5.5, 0.0, 1.0);
        float tx = q.x + sin (uTime * 1.2 - t2 * 6.0 + fj * 2.0) * 0.35 * t2;
        float ten = step (q.y, rimY) * step (abs (tx), 0.95) * smoothstep (1.0, 0.0, t2) * pow (abs (sin (tx * 22.0)), 40.0);
        float halo = exp (-length (q) * 2.2) * 0.18;
        // translucent: the bell tints what is behind it a little, the light is added on top
        col = mix (col, col * (0.75 + 0.5 * jcol), inBell * 0.45 * vis);
        vec3 light = jcol * (edge * 0.9 + inBell * 0.07 + canals + gonads * 0.45 + marginGlow * 0.5 + arms * 0.4 + ten * 0.35 + halo);
        col += light * vis * (0.7 + 0.5 * uGlow[0]) * (0.75 + 0.35 * beat);
    }
    // seabed: rippled sand, dark rocks, a caustic web dancing on it, melting into the water far away
    {
        float a = smoothstep (0.0, 0.012, bed - uv.y);
        if (a > 0.0)
        {
            float d = clamp ((bed - uv.y) / bed, 0.0, 1.0);
            vec2 sp2 = vec2 (p.x * (3.0 + 6.0 * d), uv.y * 40.0);
            float ripple = 0.5 + 0.5 * sin (sp2.y * 1.2 + fbm3 (sp2 * 0.3) * 6.0 + p.x * 4.0);
            vec3 sand = mix (vec3 (0.08, 0.16, 0.16), vec3 (0.34, 0.42, 0.35), 0.3 + 0.35 * fbm (p * vec2 (10.0, 30.0) + S) + 0.25 * ripple * d);
            float rock = smoothstep (0.58, 0.64, fbm3 (vec2 (p.x * 3.0, uv.y * 9.0) + S * 2.0));
            sand = mix (sand, vec3 (0.03, 0.06, 0.07) * (0.6 + 0.6 * fbm (p * 20.0)), rock * 0.85);
            float cw = web (vec2 (p.x * 7.0, uv.y * 18.0) + vec2 (uTime * 0.12, -uTime * 0.09));
            sand += vec3 (0.35, 0.75, 0.75) * cw * (0.22 + 0.4 * L) * (1.0 - rock * 0.7) * (0.4 + 0.6 * d);
            sand *= 0.55 + 0.6 * L;
            col = mix (col, mix (sand, col, (1.0 - d) * 0.7 + 0.08), a);
        }
    }
    kelp (col, p, uv, 2.0, bed - 0.05, asp, L);
    // out-of-focus specks right in front of the camera
    {
        vec2 bq = (p + vec2 (uTime * 0.004, -uTime * 0.012)) * 5.0;
        vec2 id = floor (bq), fq = fract (bq) - 0.5;
        if (hash12 (id + S) > 0.8)
        {
            vec2 o = (hash22 (id) - 0.5) * 0.6;
            col += vec3 (0.55, 0.85, 0.9) * smoothstep (0.13, 0.02, length (fq - o)) * 0.05 * (0.5 + L);
        }
    }
    col = mix (col, deep, (1.0 - uv.y) * 0.2);
    return col;
}
)GLSL";

// ------------------------------------------------------------------ Neon city
const char* neon = R"GLSL(
vec3 signColour (float h)
{
    if (h < 0.2) return vec3 (1.0, 0.25, 0.65);
    if (h < 0.4) return vec3 (0.25, 0.95, 1.0);
    if (h < 0.6) return vec3 (1.0, 0.7, 0.25);
    if (h < 0.8) return vec3 (0.65, 0.42, 1.0);
    return vec3 (1.0, 0.3, 0.3);
}
// one layer of buildings: returns colour + alpha (inside a building)
vec4 city (vec2 p, vec2 uv, float layer, out vec3 glowAdd)
{
    glowAdd = vec3 (0.0);
    float cw = 0.06 + 0.05 * layer;
    float x = p.x / cw + layer * 17.0 + uMus * 0.02 * (1.0 + layer);
    float id = floor (x), fx = fract (x);
    float gap = 0.06;
    float hgt = 0.18 + (0.55 - 0.15 * layer) * pow (hash12 (vec2 (id, layer)), 1.4);
    float inB = step (gap, fx) * step (fx, 1.0 - gap) * step (uv.y, hgt);
    // neon sign glow from this and the neighbouring buildings
    for (int n = -1; n <= 1; n++)
    {
        float nid = id + float (n);
        float sh = hash12 (vec2 (nid, layer + 0.5));
        if (sh > 0.45 && layer > 0.5)
        {
            float nh = 0.18 + (0.55 - 0.15 * layer) * pow (hash12 (vec2 (nid, layer)), 1.4);
            float sy = nh * (0.55 + 0.3 * hash12 (vec2 (nid, 4.0)));
            float sxc = (nid + 0.5 - layer * 17.0 - uMus * 0.02 * (1.0 + layer)) * cw;
            vec2 sd = vec2 (abs (p.x - sxc) / (cw * 0.35), abs (uv.y - sy) / 0.02);
            float flick = step (0.03, fract (sin (uTime * 3.0 + nid) * 43.7)) * (0.7 + 0.3 * uPres[1]);
            vec3 scol = signColour (fract (sh * 7.0));
            float box = smoothstep (1.05, 0.95, max (sd.x, sd.y));
            glowAdd += scol * (box * 1.2 + exp (-length (sd * vec2 (0.35, 0.12)) * 1.6) * 0.18) * flick;
        }
    }
    // windows
    float wx = floor (fx * 7.0), wy = floor (uv.y * (90.0 - 25.0 * layer));
    float wl = step (0.72 - 0.08 * layer, hash12 (vec2 (id * 13.0 + wx, wy + layer * 91.0)));
    float wm = step (0.3, fract (fx * 7.0)) * step (0.35, fract (uv.y * (90.0 - 25.0 * layer)));
    vec3 wc = mix (vec3 (1.0, 0.82, 0.55), vec3 (0.5, 0.9, 1.0), step (0.8, hash12 (vec2 (id, wy))));
    vec3 body = mix (vec3 (0.03, 0.04, 0.09), vec3 (0.10, 0.12, 0.22), 0.3 * (2.0 - layer)) * (0.7 + 0.3 * fbm3 (vec2 (fx * 4.0, uv.y * 20.0)));
    vec3 c = body + wc * wl * wm * (0.7 - 0.15 * layer);
    return vec4 (c, inB);
}
vec3 scene (vec2 uv)
{
    float L = uLight, asp = uRes.x / uRes.y;
    vec2 p = vec2 (uv.x * asp, uv.y);
    vec3 glowC = mix (vec3 (0.75, 0.32, 0.18), vec3 (0.55, 0.15, 0.45), 0.5 + 0.5 * sin (uTime * 0.05));
    vec3 col = mix (glowC * (0.35 + 0.35 * L), vec3 (0.012, 0.016, 0.04), pow (uv.y, 0.7));
    col += glowC * 0.25 * fbm (vec2 (p.x * 1.2 + uTime * 0.01, uv.y * 3.0) + S) * (1.0 - uv.y);   // smog
    // searchlights (Shimmer)
    for (int b = 0; b < 3; b++)
    {
        float fb = float (b);
        float bx = asp * (0.2 + 0.3 * fb), ang = sin (uTime * (0.11 + 0.04 * fb) + fb * 2.0) * 0.5;
        vec2 dir = vec2 (sin (ang), cos (ang));
        vec2 rp = p - vec2 (bx, 0.1);
        float along = dot (rp, dir), across = abs (rp.x * dir.y - rp.y * dir.x);
        col += vec3 (0.8, 0.92, 1.0) * exp (-across * (60.0 / (0.3 + max (along, 0.0))))   /* behind the lamp 'along' < 0 gave inf * 0 = NaN: black wedges */ * step (0.0, along) * 0.12 * (0.3 + uPres[3]);
    }
    // three layers of skyline, far to near, with atmospheric fog
    vec3 glowSum = vec3 (0.0);
    for (int l = 0; l < 3; l++)
    {
        vec3 ga;
        vec4 b = city (p, uv, float (l), ga);
        float fog = 0.55 - 0.22 * float (l);
        col = mix (col, mix (b.rgb, glowC * 0.4, fog), b.a);
        glowSum += ga;
    }
    col += glowSum * 0.9;
    // wet street reflecting the signs
    if (uv.y < 0.09)
    {
        float rip = (fbm3 (vec2 (p.x * 30.0, uv.y * 80.0 - uTime)) - 0.5) * 0.02;
        vec3 ga; vec4 b = city (vec2 (p.x + rip, 0.0), vec2 (uv.x, 0.18 - uv.y * 1.2), 2.0, ga);
        col = vec3 (0.01, 0.012, 0.02) + ga * 0.45 + b.rgb * 0.15 + glowC * 0.08;
    }
    // rain
    for (int l = 0; l < 3; l++)   // far (fine, slow) to near (thicker, fast); every column has its own speed and phase
    {
        float fl = float (l);
        float cols = 150.0 - 40.0 * fl;
        float cx = p.x * cols + uv.y * (5.0 + 3.0 * fl);              // slight slant
        float cid = floor (cx), fx = fract (cx);
        float h1 = hash12 (vec2 (cid, 3.0 + fl)), h2 = hash12 (vec2 (cid, 7.0 + fl));
        float rate = (0.9 + 0.8 * h1) * (0.8 + 0.45 * fl);           // screen heights per second
        float tt = uv.y * (2.0 + h2) + uTime * rate * (2.0 + h2) + h2 * 17.0;
        float did = floor (tt), fy = fract (tt);
        float len = 0.16 + 0.12 * fl;
        float exists = step (0.62 - 0.08 * uPres[4], hash12 (vec2 (cid + 0.37, did + fl * 31.0)));
        float streak = exists * smoothstep (0.09 + 0.05 * fl, 0.0, abs (fx - 0.5)) * step (fy, len) * (1.0 - fy / len);
        col += vec3 (0.6, 0.72, 1.0) * streak * (0.1 + 0.07 * fl) * (0.7 + 0.6 * uPres[4]);
    }
    // low fog (Wind)
    col = mix (col, glowC * 0.55, uPres[4] * 0.35 * smoothstep (0.35, 0.0, uv.y) * fbm3 (vec2 (p.x * 2.0 - uMus * 0.02, uv.y * 4.0)));
    return col;
}
)GLSL";

// ------------------------------------------------------------------ Ocean
const char* ocean = R"GLSL(
vec3 sky (vec2 p, float y, float hor, vec2 sp, vec3 sc, float L)
{
    vec3 c = skyGrad ((y - hor) / (1.0 - hor), L);
    float sd = length (p - sp);
    c += sc * 0.02 / (sd * sd + 0.012) * 0.35;
    vec2 cp = vec2 (p.x * 1.1 + uMus * 0.004, y * 5.0) + S;
    float d1 = fbm (cp), d2 = fbm (cp + normalize (sp - p) * 0.05);
    float cl = smoothstep (0.5, 0.85, d1) * smoothstep (hor, hor + 0.2, y);
    float lit = clamp ((d1 - d2) * 7.0 + 0.5, 0.0, 1.0);
    c = mix (c, mix (skyGrad (0.0, L) * 0.5, sc * mix (0.6, 1.2, L), lit), cl * 0.9);
    return c;
}
vec3 scene (vec2 uv)
{
    float L = uLight, asp = uRes.x / uRes.y;
    vec2 p = vec2 (uv.x * asp, uv.y);
    float hor = 0.48;
    float ph = fract (uEvoT / 420.0 + fract (uSeed * 0.0137));
    vec2 sp = vec2 ((0.15 + 0.7 * ph) * asp, hor + 0.04 + sin (ph * 3.14159) * 0.36);
    vec3 sc = mix (vec3 (0.85, 0.9, 1.05), vec3 (1.3, 0.92, 0.55), step (0.5, L));
    vec3 col;
    if (uv.y > hor)
    {
        col = sky (p, uv.y, hor, sp, sc, L);
        col += vec3 (stars (p * 110.0 + S, 0.08, 0.08)) * clamp ((0.6 - L) * 2.2, 0.0, 1.0);
        col = mix (col, sc * 1.6, smoothstep (0.04, 0.034, length (p - sp)));
        // crepuscular rays from the sun
        vec2 v = p - sp;
        float ang = atan (v.y, v.x);
        col += sc * pow (noise (vec2 (ang * 12.0, uTime * 0.05)), 3.0) * 0.12 * exp (-length (v) * 2.0) * smoothstep (0.35, 0.6, L);
        // distant islands (Drone)
        float isl = hor + max (0.0, fbm3 (vec2 (p.x * 2.5 + S.x, 0.0)) - 0.45) * 0.25 * (0.5 + uPres[0]);
        if (uv.y < isl) col = mix (skyGrad (0.0, L) * 0.35, vec3 (0.02, 0.03, 0.05), 0.5);
    }
    else
    {
        // perspective water: waves from fbm, fresnel reflection of the sky, sun glitter
        float dz = hor - uv.y + 0.002;
        float depth = 0.05 / dz;
        vec2 wp = vec2 (p.x * depth * 0.6, depth) + vec2 (uMus * 0.01, -uTime * 0.25);
        float swell = 0.5 + 0.6 * uPres[1] + 0.5 * uGlow[4];
        float e = 0.02;
        float h0 = fbm (wp * 1.5), hx = fbm (wp * 1.5 + vec2 (e, 0.0)), hy = fbm (wp * 1.5 + vec2 (0.0, e));
        vec3 n = normalize (vec3 ((h0 - hx) * swell * 3.0, 1.0, (h0 - hy) * swell * 3.0));
        float fres = pow (1.0 - max (n.y, 0.0) * 0.9, 3.0) * 0.6 + 0.25;
        float ry = hor + dz * 1.2 + n.x * 0.15 + n.z * 0.1;
        vec3 refl = sky (p + vec2 (n.x * 0.2, 0.0), clamp (ry, hor, 1.0), hor, sp, sc, L);
        vec3 water = mix (vec3 (0.01, 0.05, 0.09), vec3 (0.04, 0.16, 0.22), L) * (0.5 + 0.5 * (1.0 - dz * 2.0));
        col = mix (water, refl, fres);
        float glit = pow (max (0.0, noise (wp * 18.0 + uTime) * (0.6 + n.x * 3.0)), 6.0) * exp (-abs (p.x - sp.x) * (2.0 + 6.0 * dz)) * (sp.y - hor + 0.1);
        col += sc * glit * 3.0;
        // foam crests (Texture)
        float foam = smoothstep (0.68, 0.8, fbm (wp * 3.0 + 5.0)) * (0.3 + 0.7 * uPres[2]) * (1.0 - dz * 1.5);
        col = mix (col, vec3 (0.85, 0.9, 0.95) * (0.5 + 0.5 * L), foam * 0.35);
        // the shore with a foam line that comes and goes
        float shore = 0.08 + 0.02 * sin (uTime * 0.45) * swell + 0.01 * fbm3 (vec2 (p.x * 6.0, 0.0));
        if (uv.y < shore + 0.01)
        {
            float fl = smoothstep (0.012, 0.0, abs (uv.y - shore)) * (0.6 + 0.4 * noise (vec2 (p.x * 40.0, uTime)));
            vec3 sand = mix (vec3 (0.12, 0.09, 0.06), vec3 (0.45, 0.36, 0.26), L) * (0.7 + 0.3 * fbm (p * 30.0));
            col = uv.y < shore ? sand * 0.6 + refl * 0.08 : col;
            col += vec3 (0.9) * fl * 0.5;
        }
    }
    // haze on the horizon
    col = mix (col, skyGrad (0.0, L), exp (-abs (uv.y - hor) * 40.0) * 0.4);
    return col;
}
)GLSL";

// ------------------------------------------------------------------ Aurora
const char* aurora = R"GLSL(
vec3 auroraAt (vec2 p, float y)
{
    vec3 a = vec3 (0.0);
    for (int i = 0; i < 18; i++)
    {
        float fi = float (i);
        float base = 0.52 + fi * 0.018;
        float n = fbm3 (vec2 (p.x * 1.1 + fi * 0.09 + uTime * 0.025, uTime * 0.04 + fi * 0.13) + S);
        float yb = base + (n - 0.5) * 0.35;
        float dy = y - yb;
        float rays = 0.35 + 0.65 * pow (noise (vec2 (p.x * 55.0 + fi * 3.0 + n * 6.0, uTime * 0.3)), 2.0);
        float band = smoothstep (-0.01, 0.01, dy) * exp (-dy * 5.5) * rays;
        vec3 c = mix (vec3 (0.12, 1.0, 0.5), vec3 (0.7, 0.25, 1.0), smoothstep (0.02, 0.3, dy) * 0.9);
        a += c * band * 0.07;
    }
    return a;
}
vec3 scene (vec2 uv)
{
    float L = uLight, asp = uRes.x / uRes.y;
    vec2 p = vec2 (uv.x * asp, uv.y);
    float hor = 0.34;
    vec3 col = mix (vec3 (0.02, 0.05, 0.11), vec3 (0.006, 0.01, 0.03), uv.y) * (0.8 + 0.6 * L);
    col += vec3 (stars (p * 120.0 + S, 0.11, 0.08)) * (0.9 - 0.4 * L);
    float power = 0.45 + 0.6 * uPres[3] + 0.3 * uPres[1] + 0.5 * uGlow[3];
    col += auroraAt (p, uv.y) * power;
    // crescent moon
    vec2 mc = vec2 (asp * (0.15 + 0.7 * hash12 (S)), 0.84);
    float md = length (p - mc);
    col += vec3 (0.8, 0.85, 1.0) * 0.004 / (md * md + 0.002);
    col = mix (col, vec3 (0.92, 0.95, 1.0), smoothstep (0.03, 0.026, md) * smoothstep (0.022, 0.03, length (p - mc - vec2 (0.012, 0.008))));
    // snowy mountains lit by the moon and the aurora
    for (int i = 0; i < 2; i++)
    {
        float fi = float (i);
        float x = p.x * (1.0 + 0.8 * fi) + S.y + fi * 9.0 + uMus * 0.002 * (1.0 + fi);
        float h = hor + 0.12 - 0.07 * fi + (0.2 - 0.06 * fi) * ridged (vec2 (x, fi)) * (0.5 + 0.5 * uPres[i]);
        if (uv.y < h)
        {
            float lightSide = rockShade (vec2 (x * 4.0, uv.y * 8.0 + fi * 3.0), 1.0);
            float snowMask = smoothstep (0.42, 0.58, fbm (vec2 (x * 7.0, uv.y * 18.0) + S) + 0.45 - (h - uv.y) * 3.0);
            vec3 rockC = vec3 (0.05, 0.07, 0.12) * (0.6 + 0.6 * lightSide);
            vec3 snowC = vec3 (0.7, 0.78, 0.92) * (0.35 + 0.75 * lightSide);
            vec3 m = mix (rockC, snowC, snowMask);
            m *= 0.45 + 0.45 * L;
            m += vec3 (0.1, 0.55, 0.35) * power * 0.1 * snowMask;
            col = mix (m, vec3 (0.05, 0.09, 0.16), 0.35 - 0.2 * fi);
        }
    }
    // frozen lake reflecting the aurora
    if (uv.y < hor - 0.02)
    {
        float dd = hor - 0.02 - uv.y;
        float rip = (fbm3 (vec2 (p.x * 10.0, dd * 40.0)) - 0.5) * 0.02;
        vec3 refl = auroraAt (p + vec2 (rip, 0.0), hor + dd * 1.6 + 0.1) * power * 0.55;
        vec3 ice = mix (vec3 (0.03, 0.06, 0.1), vec3 (0.12, 0.18, 0.25), fbm (p * vec2 (3.0, 12.0) + S));
        col = ice + refl;
    }
    // snowfall (Wind)
    for (int k = 0; k < 2; k++)
    {
        float fk = float (k);
        vec2 q = vec2 (p.x + sin (uTime * 0.4 + p.y * 5.0) * 0.02, p.y + uTime * (0.03 + 0.02 * fk)) * (30.0 + 25.0 * fk) + S;
        col += vec3 (0.9) * stars (q, 0.06 + 0.12 * uPres[4], 0.1) * 0.5;
    }
    return col;
}
)GLSL";
} // namespace

// ------------------------------------------------------------------ GpuScenes
bool GpuScenes::init (juce::OpenGLContext& c)
{
    ctx = &c;
    const char* bodies[numScenes] {};
    bodies[scValley] = valley; bodies[scStarfield] = starfield; bodies[scUnderwater] = underwater;
    bodies[scNeon] = neon; bodies[scOcean] = ocean; bodies[scAurora] = aurora;
    for (int i = 0; i < numScenes; ++i)
    {
        auto p = std::make_unique<juce::OpenGLShaderProgram> (c);
        const juce::String fs = juce::String (prelude) + bodies[i] + mainTail;
        if (! p->addVertexShader (juce::OpenGLHelpers::translateVertexShaderToV3 (vertexShader))
            || ! p->addFragmentShader (juce::OpenGLHelpers::translateFragmentShaderToV3 (fs))
            || ! p->link())
        {
            error = "scene " + juce::String (i) + ": " + p->getLastError();
            DBG (error);
            release();
            return false;
        }
        prog[i] = std::move (p);
    }
    const GLfloat quad[] { -1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f };
    glGenBuffers (1, &vbo);
    glBindBuffer (GL_ARRAY_BUFFER, vbo);
    glBufferData (GL_ARRAY_BUFFER, sizeof (quad), quad, GL_STATIC_DRAW);
    glBindBuffer (GL_ARRAY_BUFFER, 0);
    return true;
}

void GpuScenes::release()
{
    for (auto& p : prog) p.reset();
    if (vbo != 0 && ctx != nullptr) glDeleteBuffers (1, &vbo);
    vbo = 0;
}

void GpuScenes::render (const SceneUniforms& u, int w, int h)
{
    auto* p = prog[juce::jlimit (0, numScenes - 1, u.scene)].get();
    if (p == nullptr || vbo == 0) return;
    glViewport (0, 0, w, h);
    glDisable (GL_BLEND);
    glDisable (GL_CULL_FACE);     // both triangles of the full-screen quad, whatever the winding
    glDisable (GL_DEPTH_TEST);
    glDisable (GL_SCISSOR_TEST);
    p->use();
    p->setUniform ("uRes", (GLfloat) w, (GLfloat) h);
    p->setUniform ("uTime", u.time); p->setUniform ("uMus", u.mus); p->setUniform ("uEvoT", u.evoT);
    p->setUniform ("uLight", u.light); p->setUniform ("uEvo", u.evo); p->setUniform ("uDensity", u.density);
    p->setUniform ("uSpace", u.space); p->setUniform ("uEnergy", u.energy); p->setUniform ("uRun", u.run);
    p->setUniform ("uFrozen", u.frozen); p->setUniform ("uSeed", u.seed); p->setUniform ("uFade", u.fade);
    p->setUniform ("uPres", u.pres, kNumLayers);
    p->setUniform ("uGlow", u.glow, kNumLayers);
    const GLint pos = glGetAttribLocation (p->getProgramID(), "position");
    glBindBuffer (GL_ARRAY_BUFFER, vbo);
    glEnableVertexAttribArray ((GLuint) pos);
    glVertexAttribPointer ((GLuint) pos, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glDrawArrays (GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray ((GLuint) pos);
    glBindBuffer (GL_ARRAY_BUFFER, 0);
}

// ------------------------------------------------------------------ OffscreenGpu
struct OffscreenGpu::Host : public juce::Component
{
    juce::OpenGLContext context;
    GpuScenes scenes;
    bool initialised = false, good = false;
    Host()
    {
        setSize (64, 64);
        setOpaque (true);
        addToDesktop (juce::ComponentPeer::windowIsTemporary | juce::ComponentPeer::windowIgnoresKeyPresses);
        setTopLeftPosition (-20000, -20000);   // a real window, kept out of sight
        setVisible (true);
        context.setComponentPaintingEnabled (false);
        context.setContinuousRepainting (false);
        context.attachTo (*this);
    }
    ~Host() override
    {
        context.detach();   // releases the GL objects with the context
    }
};

OffscreenGpu& OffscreenGpu::get() { static OffscreenGpu o; return o; }

namespace
{
// runs fn on the GL thread while the message loop keeps pumping (the GL thread may need the message thread); false on timeout
template <typename Fn>
bool runOnGl (juce::OpenGLContext& ctx, Fn&& fn, int timeoutMs)
{
    auto done = std::make_shared<std::atomic<bool>> (false);
    ctx.executeOnGLThread ([fn, done] (juce::OpenGLContext& c) mutable { fn (c); done->store (true); }, false);
    ctx.triggerRepaint();
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (! done->load() && juce::Time::getMillisecondCounter() < end)
    {
        if (juce::MessageManager::getInstance()->isThisTheMessageThread()) juce::MessageManager::getInstance()->runDispatchLoopUntil (2);
        else juce::Thread::sleep (1);
        ctx.triggerRepaint();
    }
    return done->load();
}
}

bool OffscreenGpu::available()
{
    if (tried) return ok;
    tried = true;
    if (gpuStatus().load() < 0) return ok = false;
    host = std::make_unique<Host>();
    for (int i = 0; i < 200 && ! host->context.isAttached(); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    if (! host->context.isAttached()) { host.reset(); return ok = false; }
    auto good = std::make_shared<bool> (false);
    auto* h = host.get();
    const bool ran = runOnGl (host->context, [h, good] (juce::OpenGLContext& c) { *good = h->scenes.init (c); h->initialised = true; }, 8000);
    ok = ran && *good;
    if (ran && ! *good) gpuStatus() = -1;
    if (! ok) host.reset();
    return ok;
}

juce::Image OffscreenGpu::render (const SceneUniforms& u, int w, int h)
{
    if (! available()) return {};
    juce::Image img (juce::Image::ARGB, w, h, false, juce::SoftwareImageType());
    bool done = false;
    auto* hst = host.get();
    const bool ran = runOnGl (host->context, [&, hst] (juce::OpenGLContext& c)
    {
        juce::OpenGLFrameBuffer fb;
        if (! fb.initialise (c, w, h)) return;
        fb.makeCurrentRenderingTarget();
        hst->scenes.render (u, w, h);
        const juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
        std::vector<juce::PixelARGB> px ((size_t) (w * h));
        fb.readPixels (px.data(), { 0, 0, w, h }, juce::OpenGLFrameBuffer::RowOrder::fromTopDown);
        for (int y = 0; y < h; ++y)
            std::memcpy (bd.getLinePointer (y), px.data() + (size_t) y * (size_t) w, (size_t) w * 4);
        fb.releaseAsRenderingTarget();
        done = true;
    }, 5000);
    return ran && done ? img : juce::Image();
}

void OffscreenGpu::shutdown() { host.reset(); tried = false; ok = false; }
}
