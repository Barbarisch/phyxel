// ============================================================================================
// atmosphere.glsl — the GPU half of THE physical sky model. Sibling of lighting.glsl.
//
// This mirrors engine/include/graphics/Atmosphere.h + Atmosphere.cpp. The CPU copy derives the
// per-frame LIGHT (sun colour, sky ambient colour, haze endpoints); this copy renders what you
// SEE. They must agree, or the sky and the lighting drift apart again — which is the exact
// failure the shared lighting model was created to end.
//
// ⚠️ THE DUPLICATE IS GUARDED, NOT TRUSTED. `AtmosphereTest.ShaderConstantsMatchTheCppModel`
// parses THIS FILE and asserts every constant below equals its C++ counterpart. Keep the
// declarations in the plain `const float kName = <number>;` / `const vec3 kName = vec3(a,b,c)`
// form the test's regex reads, and change values in BOTH files.
//
// ⚠️ DIRECTION CONVENTION: `toSun` / `toMoon` point FROM the viewer TOWARDS the body, so
// `toSun.y == sin(elevation)`. The UBO's `sunDirection` is the opposite (the way light travels,
// downward at noon). Flip at the call site: `toSun = normalize(-ubo.sunDirection)`.
//
// SAMPLING: coarser than the CPU model on purpose — this runs per pixel. Steps are QUADRATICALLY
// spaced so samples cluster near the viewer where the air is dense; uniform steps spend most of
// their samples in near-vacuum and produce a banded, wrong-coloured horizon at low sun, which is
// precisely the case sunrise and sunset care about.
// ============================================================================================

#ifndef PHYXEL_ATMOSPHERE_GLSL
#define PHYXEL_ATMOSPHERE_GLSL

// ---- Shared constants. MIRRORED IN engine/include/graphics/Atmosphere.h (parity is tested). ----
const float kPlanetRadius       = 6360000.0;
const float kAtmosphereRadius   = 6420000.0;

const vec3  kRayleighScattering = vec3(5.802e-6, 13.558e-6, 33.100e-6);
const float kRayleighScaleHeight = 8000.0;

const float kMieScattering      = 3.996e-6;
const float kMieExtinction      = 4.400e-6;
const float kMieScaleHeight     = 1200.0;
const float kMieAnisotropy      = 0.80;

// Ozone. Absorption only, in a tent layer around 25 km. This is what keeps the post-sunset sky
// BLUE instead of muddy grey — the single most-often-omitted term in an otherwise correct model.
const vec3  kOzoneAbsorption    = vec3(0.650e-6, 1.881e-6, 0.085e-6);
const float kOzoneCenter        = 25000.0;
const float kOzoneWidth         = 15000.0;

const vec3  kSolarIrradiance    = vec3(1.0, 0.97, 0.92);
// Apparent size of the sun and moon: a deliberate STYLIZED choice, 5x life size. At true size both
// are ~0.5 deg -- a ten-pixel dot -- and the moon's per-pixel phase terminator is invisible. See the
// full note in Atmosphere.h. kSun/kMoonPhysicalAngularRadius are the real values, kept because the
// HORIZON FADE must stay physical: how fast sunlight dies as the sun sets cannot depend on how big
// we chose to draw it.
const float kSunSizeScale             = 5.0;
const float kSunPhysicalAngularRadius = 0.004675;
const float kMoonPhysicalAngularRadius = 0.004525;
const float kSunAngularRadius   = 0.023375;   // = kSunPhysicalAngularRadius  * kSunSizeScale
const float kMoonAngularRadius  = 0.022625;   // = kMoonPhysicalAngularRadius * kSunSizeScale
const float kMoonAlbedo         = 0.12;
const vec3  kMoonlightTint      = vec3(0.62, 0.78, 1.0);
const float kMoonlightScale     = 0.25;

// ---- Look constants: rendering-only, no CPU counterpart, deliberately not physical ------------
// The sun's true radiance is its irradiance divided by its solid angle — around 14,000x a lit
// diffuse surface. Until there is an exposure + tonemap stage that can hold that range, the disc
// is drawn at an explicit LOOK brightness. Labelled honestly so nobody later "corrects" it toward
// physics and blows the frame out. It is still MULTIPLIED by the atmospheric transmittance, so a
// setting sun's disc reddens on its own.
const float kSunDiscBrightness  = 24.0;
const float kMoonDiscBrightness = 2.2;
// Disc edge softening for anti-aliasing, as an ABSOLUTE ANGLE in radians (~1.3 px at a typical
// field of view). It used to be a FRACTION of the radius, which was fine at life size but scales
// with the disc: at 5x the stylized size that fraction became a 5x wider blur in angle, turning a
// crisp sun into a soft blob. An absolute angle keeps the edge the same sharpness at any disc size.
const float kDiscEdgeAngle      = 0.0009;

const int   kViewSteps = 12;
const int   kSunSteps  = 5;

const float kPhxPi = 3.14159265359;

// ---- Geometry ---------------------------------------------------------------------------------

// Far positive root of a ray against a sphere centred on the coordinate origin (the planet's
// centre). Returns -1 on a miss.
float phxRaySphereFar(vec3 origin, vec3 dir, float radius) {
    float b = dot(origin, dir);
    float c = dot(origin, origin) - radius * radius;
    float disc = b * b - c;
    if (disc < 0.0) return -1.0;
    return -b + sqrt(disc);
}

// Does the ray reach the ground? Only inward-pointing rays (b < 0) can.
bool phxHitsPlanet(vec3 origin, vec3 dir) {
    float b = dot(origin, dir);
    float c = dot(origin, origin) - kPlanetRadius * kPlanetRadius;
    return (b * b - c) >= 0.0 && b < 0.0;
}

vec3 phxViewerPosition(float altitudeM) {
    return vec3(0.0, kPlanetRadius + max(altitudeM, 1.0), 0.0);
}

float phxAltitudeOf(vec3 p) { return length(p) - kPlanetRadius; }

// Rayleigh / Mie / ozone densities at an altitude, packed to keep the register count down.
vec3 phxDensities(float altitudeM) {
    float h = max(altitudeM, 0.0);
    return vec3(exp(-h / kRayleighScaleHeight),
                exp(-h / kMieScaleHeight),
                max(0.0, 1.0 - abs(h - kOzoneCenter) / kOzoneWidth));
}

vec3 phxExtinction(float altitudeM) {
    vec3 d = phxDensities(altitudeM);
    return kRayleighScattering * d.x + vec3(kMieExtinction) * d.y + kOzoneAbsorption * d.z;
}

// Quadratic step schedule: segment [t0,t1] for step i over a ray of length `far`.
void phxStepRange(int i, int steps, float far, out float t0, out float t1) {
    float a = float(i) / float(steps);
    float b = float(i + 1) / float(steps);
    t0 = far * a * a;
    t1 = far * b * b;
}

// Optical depth from p along dir until the ray leaves the atmosphere.
vec3 phxOpticalDepthOut(vec3 p, vec3 dir, int steps) {
    float far = phxRaySphereFar(p, dir, kAtmosphereRadius);
    if (far <= 0.0) return vec3(0.0);
    vec3 tau = vec3(0.0);
    for (int i = 0; i < steps; ++i) {
        float t0, t1;
        phxStepRange(i, steps, far, t0, t1);
        tau += phxExtinction(phxAltitudeOf(p + dir * (0.5 * (t0 + t1)))) * (t1 - t0);
    }
    return tau;
}

// ---- Phase functions -------------------------------------------------------------------------

float phxPhaseRayleigh(float mu) { return (3.0 / (16.0 * kPhxPi)) * (1.0 + mu * mu); }

// Cornette-Shanks: the strong forward lobe that becomes the glow around a low sun.
float phxPhaseMie(float mu, float g) {
    float g2 = g * g;
    return (3.0 * (1.0 - g2) * (1.0 + mu * mu))
         / (8.0 * kPhxPi * (2.0 + g2) * pow(1.0 + g2 - 2.0 * g * mu, 1.5));
}

// ---- Direct light ----------------------------------------------------------------------------

// Smooth the horizon crossing over roughly the sun's angular diameter, so the key light fades
// instead of snapping off between two frames.
float phxHorizonFade(float sinElevation) {
    // PHYSICAL radius, not the stylized drawn one -- see the size note above.
    float band = kSunPhysicalAngularRadius * 2.0;
    return smoothstep(0.0, 1.0, clamp((sinElevation + band) / (2.0 * band), 0.0, 1.0));
}

vec3 phxTransmittanceToSun(vec3 toSun, float altitudeM) {
    float fade = phxHorizonFade(toSun.y);
    if (fade <= 0.0) return vec3(0.0);
    return exp(-phxOpticalDepthOut(phxViewerPosition(altitudeM), toSun, kSunSteps)) * fade;
}

// ---- Sky radiance ----------------------------------------------------------------------------

/// Single-scattered sky radiance looking along `dir`. Downward rays are marched only to the
/// ground, so a below-horizon direction gives the short dense near-ground scatter rather than a
/// full-shell integral.
vec3 phxSkyRadiance(vec3 dir, vec3 toSun, float altitudeM) {
    dir = normalize(dir);
    vec3 p = phxViewerPosition(altitudeM);

    float far = phxRaySphereFar(p, dir, kAtmosphereRadius);
    if (far <= 0.0) return vec3(0.0);
    if (phxHitsPlanet(p, dir)) {
        float b = dot(p, dir);
        float c = dot(p, p) - kPlanetRadius * kPlanetRadius;
        float disc = b * b - c;
        if (disc >= 0.0) far = min(far, max(0.0, -b - sqrt(disc)));
    }

    float mu = dot(dir, toSun);
    float pr = phxPhaseRayleigh(mu);
    float pm = phxPhaseMie(mu, kMieAnisotropy);

    vec3 tauView = vec3(0.0);
    vec3 sumR = vec3(0.0);
    vec3 sumM = vec3(0.0);

    for (int i = 0; i < kViewSteps; ++i) {
        float t0, t1;
        phxStepRange(i, kViewSteps, far, t0, t1);
        float ds = t1 - t0;
        vec3 s = p + dir * (0.5 * (t0 + t1));
        float h = phxAltitudeOf(s);
        vec3 d = phxDensities(h);

        tauView += phxExtinction(h) * ds;

        // Samples inside the planet's own shadow contribute nothing. This is what makes twilight
        // drain from the horizon upward instead of the whole sky dimming together.
        if (phxHitsPlanet(s, toSun)) continue;

        vec3 t = exp(-tauView - phxOpticalDepthOut(s, toSun, kSunSteps));
        sumR += t * d.x * ds;
        sumM += t * d.y * ds;
    }

    return kSolarIrradiance * (sumR * kRayleighScattering * pr + sumM * vec3(kMieScattering) * pm);
}

// ---- Celestial bodies ------------------------------------------------------------------------

/// Antialiased coverage of a disc of angular radius `radius` centred on `bodyDir`.
float phxDiscCoverage(vec3 dir, vec3 bodyDir, float radius) {
    float cosAng = dot(normalize(dir), bodyDir);
    // Work in angle rather than cosine so the soft edge has a constant angular width.
    float ang = acos(clamp(cosAng, -1.0, 1.0));
    return 1.0 - smoothstep(radius - kDiscEdgeAngle, radius + kDiscEdgeAngle, ang);
}

/// The sun's disc, already reddened by the same transmittance that colours the directional light,
/// so the disc and the light it casts can never disagree.
vec3 phxSunDisc(vec3 dir, vec3 toSun, float altitudeM) {
    float cov = phxDiscCoverage(dir, toSun, kSunAngularRadius);
    if (cov <= 0.0) return vec3(0.0);
    // Limb darkening: the solar disc is measurably dimmer at its edge, which is what stops it
    // reading as a flat sticker.
    float ang = acos(clamp(dot(normalize(dir), toSun), -1.0, 1.0));
    float r = clamp(ang / kSunAngularRadius, 0.0, 1.0);
    float limb = 0.6 + 0.4 * sqrt(max(0.0, 1.0 - r * r));
    return kSolarIrradiance * phxTransmittanceToSun(toSun, altitudeM)
         * (kSunDiscBrightness * cov * limb);
}

/// The moon's disc, with its phase derived from GEOMETRY rather than passed in: the moon is a
/// sphere lit by the sun, so for each point on the visible disc we reconstruct the surface normal
/// and test it against the sun direction. The terminator then curves correctly on its own, and the
/// phase automatically agrees with wherever the sun and moon actually are — a full moon rises at
/// sunset because the geometry says so, not because a parameter was set.
vec3 phxMoonDisc(vec3 dir, vec3 toMoon, vec3 toSun, float altitudeM) {
    float cov = phxDiscCoverage(dir, toMoon, kMoonAngularRadius);
    if (cov <= 0.0) return vec3(0.0);

    // Build a tangent frame on the moon disc and find where in it this ray lands.
    vec3 up = abs(toMoon.y) > 0.99 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
    vec3 tx = normalize(cross(up, toMoon));
    vec3 ty = cross(toMoon, tx);
    vec3 d = normalize(dir);
    // Small-angle: offsets in radians, normalised to the disc radius.
    float u = dot(d, tx) / kMoonAngularRadius;
    float v = dot(d, ty) / kMoonAngularRadius;
    float r2 = u * u + v * v;
    if (r2 > 1.0) return vec3(0.0);

    // Surface normal of the sphere at that point, in the same frame (+toMoon is toward us).
    vec3 n = normalize(tx * u + ty * v - toMoon * sqrt(max(0.0, 1.0 - r2)));
    float lit = max(0.0, dot(n, toSun));

    // A soft terminator: the real one is not razor sharp, and a hard step aliases badly at this
    // angular size. Earthshine keeps the dark limb faintly visible, as it is in life.
    float shade = smoothstep(0.0, 0.12, lit) * (0.06 + 0.94 * lit);
    // The moon is grey rock: its own colour is albedo-scaled sunlight, and it is reddened by the
    // atmosphere on the way to us exactly like the sun is.
    return kSolarIrradiance * kMoonAlbedo * kMoonDiscBrightness * shade * cov
         * phxTransmittanceToSun(toMoon, altitudeM);
}

/// A GENERIC celestial body disc — the one function that draws suns and moons alike.
///   bodyDir   unit vector TOWARD the body
///   radius    drawn angular radius (radians)
///   discColor colour x brightness
///   litDir    unit vector toward whatever illuminates it (ignored when not reflective)
///   reflective 1 = has a phase (a moon), 0 = emits its own light (a star)
/// Reflective bodies get their terminator from GEOMETRY: the sphere's normal at each pixel tested
/// against litDir. Nothing passes a phase in, so the drawn phase can never disagree with where the
/// bodies actually are.
vec3 phxBodyDisc(vec3 dir, vec3 bodyDir, float radius, vec3 discColor,
                 vec3 litDir, float reflective, float altitudeM) {
    float cov = phxDiscCoverage(dir, bodyDir, radius);
    if (cov <= 0.0) return vec3(0.0);

    vec3 d = normalize(dir);
    float ang = acos(clamp(dot(d, bodyDir), -1.0, 1.0));

    float shade;
    if (reflective > 0.5) {
        // Tangent frame on the disc; reconstruct the sphere normal at this pixel.
        vec3 up = abs(bodyDir.y) > 0.99 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
        vec3 tx = normalize(cross(up, bodyDir));
        vec3 ty = cross(bodyDir, tx);
        float u = dot(d, tx) / radius;
        float v = dot(d, ty) / radius;
        float r2 = u * u + v * v;
        if (r2 > 1.0) return vec3(0.0);
        vec3 n = normalize(tx * u + ty * v - bodyDir * sqrt(max(0.0, 1.0 - r2)));
        float lit = max(0.0, dot(n, litDir));
        // Soft terminator (the real one is not razor sharp and a hard step aliases badly at this
        // angular size) plus a little earthshine so the dark limb stays faintly present.
        shade = smoothstep(0.0, 0.12, lit) * (0.06 + 0.94 * lit);
    } else {
        // Limb darkening: a star is measurably dimmer at its edge, which is what stops it reading
        // as a flat sticker.
        float r = clamp(ang / radius, 0.0, 1.0);
        shade = 0.6 + 0.4 * sqrt(max(0.0, 1.0 - r * r));
    }

    // Every body is reddened by the same transmittance that colours the light it casts, so a setting
    // sun and the light it throws can never disagree.
    return discColor * shade * cov * phxTransmittanceToSun(bodyDir, altitudeM);
}

/// Everything behind the geometry: the scattered sky plus every body.
/// `toSun` drives the SKY's scattering (the primary star); the bodies draw themselves.
vec3 phxAtmosphereBodies(vec3 dir, vec3 toSun, float altitudeM,
                         vec4 dirRadius[4], vec4 disc[4], vec4 litDir[4], int count) {
    vec3 c = phxSkyRadiance(dir, toSun, altitudeM);
    for (int i = 0; i < count && i < 4; ++i) {
        c += phxBodyDisc(dir, dirRadius[i].xyz, dirRadius[i].w, disc[i].rgb,
                         litDir[i].xyz, disc[i].w, altitudeM);
    }
    return c;
}

#endif // PHYXEL_ATMOSPHERE_GLSL
