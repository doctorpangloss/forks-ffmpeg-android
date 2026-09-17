#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define FLOAT_EPS 0.000001
#define REFERENCE_WHITE_ALT 203.0
#define ST2084_MAX_LUMINANCE 10000.0

enum {
    SURFACE_TONEMAP_NONE     = 0,
    SURFACE_TONEMAP_LINEAR   = 1,
    SURFACE_TONEMAP_GAMMA    = 2,
    SURFACE_TONEMAP_CLIP     = 3,
    SURFACE_TONEMAP_REINHARD = 4,
    SURFACE_TONEMAP_HABLE    = 5,
    SURFACE_TONEMAP_MOBIUS   = 6,
    SURFACE_TONEMAP_BT2390   = 7,
};

static double pq_oetf_1(double x)
{
    const double m1 = 0.1593017578125;
    const double m2 = 78.84375;
    const double c1 = 0.8359375;
    const double c2 = 18.8515625;
    const double c3 = 18.6875;
    double xp = pow(fmax(x, 0.0), m1);
    return pow((c1 + c2 * xp) / (1.0 + c3 * xp), m2);
}

static double pq_eotf_1(double x)
{
    const double m1 = 0.1593017578125;
    const double m2 = 78.84375;
    const double c1 = 0.8359375;
    const double c2 = 18.8515625;
    const double c3 = 18.6875;
    double p = pow(fmin(fmax(x, 0.0), 1.0), 1.0 / m2);
    return pow(fmax(p - c1, 0.0) / fmax(c2 - c3 * p, FLOAT_EPS), 1.0 / m1);
}

static double hable1(double x)
{
    const double A = 0.15;
    const double B = 0.50;
    const double C = 0.10;
    const double D = 0.20;
    const double E = 0.02;
    const double F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

static double mobius1(double x, double j, double peak)
{
    double a, b;

    if (x <= j)
        return x;
    a = -j * j * (peak - 1.0) / (j * j - 2.0 * j + peak);
    b = (j * j - 2.0 * j * peak + peak) / fmax(peak - 1.0, FLOAT_EPS);
    return (b * b + 2.0 * b * j + j * j) / (b - a) * (x + a) / (x + b);
}

static double bt2390_1(double s, double knee_offset, double peak)
{
    double peak_pq = pq_oetf_1(peak * REFERENCE_WHITE_ALT / ST2084_MAX_LUMINANCE);
    double scale = peak_pq > 0.0 ? 1.0 / peak_pq : 1.0;
    double dst_peak = 1.0;
    double s_pq = pq_oetf_1(s * REFERENCE_WHITE_ALT / ST2084_MAX_LUMINANCE) * scale;
    double max_lum = pq_oetf_1(dst_peak * REFERENCE_WHITE_ALT / ST2084_MAX_LUMINANCE) * scale;
    double ks = (1.0 + knee_offset) * max_lum - knee_offset;
    double tb = (s_pq - ks) / fmax(1.0 - ks, FLOAT_EPS);
    double tb2 = tb * tb;
    double tb3 = tb2 * tb;
    double pb = (2.0 * tb3 - 3.0 * tb2 + 1.0) * ks +
                (tb3 - 2.0 * tb2 + tb) * (1.0 - ks) +
                (-2.0 * tb3 + 3.0 * tb2) * max_lum;
    double sig = s_pq < ks ? s_pq : pb;
    return pq_eotf_1(sig * peak_pq) * ST2084_MAX_LUMINANCE / REFERENCE_WHITE_ALT;
}

static double default_param(int algorithm, double param)
{
    if (!isnan(param)) {
        if (algorithm == SURFACE_TONEMAP_REINHARD)
            return (1.0 - param) / param;
        if (algorithm == SURFACE_TONEMAP_BT2390)
            return fmin(fmax(param, 0.5), 2.0);
        return param;
    }

    switch (algorithm) {
    case SURFACE_TONEMAP_GAMMA:
        return 1.8;
    case SURFACE_TONEMAP_MOBIUS:
        return 0.3;
    case SURFACE_TONEMAP_BT2390:
        return 1.0;
    default:
        return 1.0;
    }
}

static double map_sig(int algorithm, double sig, double peak, double param)
{
    switch (algorithm) {
    case SURFACE_TONEMAP_NONE:
        return sig;
    case SURFACE_TONEMAP_LINEAR:
        return sig * param / peak;
    case SURFACE_TONEMAP_GAMMA:
        return sig > 0.05 ? pow(sig / peak, 1.0 / param) :
               sig * pow(0.05 / peak, 1.0 / param) / 0.05;
    case SURFACE_TONEMAP_CLIP:
        return fmin(fmax(sig * param, 0.0), 1.0);
    case SURFACE_TONEMAP_REINHARD:
        return sig / (sig + param) * (peak + param) / peak;
    case SURFACE_TONEMAP_HABLE:
        return hable1(sig) / hable1(peak);
    case SURFACE_TONEMAP_MOBIUS:
        return mobius1(sig, param, peak);
    default:
        return bt2390_1(sig, param, peak);
    }
}

static int assert_close(const char *name, double actual, double expected, double epsilon)
{
    double delta = fabs(actual - expected);

    if (delta > epsilon) {
        fprintf(stderr, "%s: got %.12f expected %.12f delta %.12f\n",
                name, actual, expected, delta);
        return 1;
    }

    return 0;
}

int main(void)
{
    static const struct {
        int algorithm;
        double sig;
        double peak;
        double param;
        double expected;
        const char *name;
    } vectors[] = {
        { SURFACE_TONEMAP_LINEAR,   10.0, 100.0, NAN, 0.100000000000, "linear" },
        { SURFACE_TONEMAP_GAMMA,    10.0, 100.0, NAN, 0.278255940221, "gamma" },
        { SURFACE_TONEMAP_CLIP,      2.0, 100.0, NAN, 1.000000000000, "clip" },
        { SURFACE_TONEMAP_REINHARD, 10.0, 100.0, 0.5, 0.918181818182, "reinhard" },
        { SURFACE_TONEMAP_HABLE,    10.0, 100.0, NAN, 0.780981075798, "hable" },
        { SURFACE_TONEMAP_MOBIUS,   10.0, 100.0, NAN, 0.957188206856, "mobius" },
        { SURFACE_TONEMAP_BT2390,    0.01, 100.0, NAN, 0.009165716001, "bt2390-black" },
        { SURFACE_TONEMAP_BT2390,   10.0, 100.0, NAN, 0.747368911943, "bt2390-highlight" },
    };
    unsigned i;
    int failed = 0;

    failed |= assert_close("default-gamma", default_param(SURFACE_TONEMAP_GAMMA, NAN), 1.8, 1e-12);
    failed |= assert_close("default-mobius", default_param(SURFACE_TONEMAP_MOBIUS, NAN), 0.3, 1e-12);
    failed |= assert_close("default-bt2390", default_param(SURFACE_TONEMAP_BT2390, NAN), 1.0, 1e-12);
    failed |= assert_close("clamp-bt2390-low", default_param(SURFACE_TONEMAP_BT2390, 0.1), 0.5, 1e-12);
    failed |= assert_close("clamp-bt2390-high", default_param(SURFACE_TONEMAP_BT2390, 3.0), 2.0, 1e-12);

    for (i = 0; i < sizeof(vectors) / sizeof(vectors[0]); i++) {
        double param = default_param(vectors[i].algorithm, vectors[i].param);
        double mapped = map_sig(vectors[i].algorithm, vectors[i].sig, vectors[i].peak, param);
        failed |= assert_close(vectors[i].name, mapped, vectors[i].expected, 1e-9);
    }

    return failed ? 1 : 0;
}
