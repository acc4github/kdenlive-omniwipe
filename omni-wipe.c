#include <stdlib.h>
#include <stdint.h>
#include <frei0r.h>
#include <math.h>

#define _USE_MATH_DEFINES
#include <math.h>

#define CURVE_LUT_SIZE 2048
#define REFERENCE_WIDTH 1920.0

typedef struct {
    int width, height;
    double resolution_scale;

    double position;
    int direction_axis;
    double direction_wheel;

    double speed_curve;
    double gentle_arrival;

    int border_type;
    double bar_width;
    double bar_hue, bar_sat, bar_val;
    double border_blur;
    int limit_range_to_clip;
    int invert;

    int bounds_calculated;
    int out_min_x, out_min_y, out_max_x, out_max_y;
    int in_min_x, in_min_y, in_max_x, in_max_y;
    int combined_min_x, combined_min_y, combined_max_x, combined_max_y;

    double wipe_dx, wipe_dy;
    double start_proj, end_proj;
    uint32_t bar_color_packed;

    double curve_lut[CURVE_LUT_SIZE];
    double last_speed_curve;
} omni_wipe_t;

/* Forward declarations */
static void calculate_content_bounds(const uint32_t* buf, int w, int h,
                                     int* min_x, int* min_y, int* max_x, int* max_y);
static void build_curve_lut(omni_wipe_t *inst);
static double curve_lookup(const double *lut, double t);
static double reversed_linear(omni_wipe_t *inst, double t);   // NEW
static double get_progress(omni_wipe_t *inst, double p);
static void get_wipe_vector(int axis, double wheel, double *dx, double *dy);
static void compute_projection_range(omni_wipe_t *inst, double *start_p, double *end_p);

static inline uint32_t hsv_to_rgba(double h_deg, double s, double v);
static double smoothstep(double edge0, double edge1, double x);
static inline uint32_t get_pixel(const uint32_t *buf, int w, int h, int x, int y,
                                 int minx, int miny, int maxx, int maxy);
static inline uint32_t blend_rgba(uint32_t c1, uint32_t c2, double t);

/* ==========================================================================
   Plugin Interface
   ========================================================================== */

int f0r_init() { return 1; }
void f0r_deinit() {}

void f0r_get_plugin_info(f0r_plugin_info_t *info) {
    info->name = "OmniWipe";
    info->author = "acc4commissions and Grok 4.3";
    info->plugin_type = F0R_PLUGIN_TYPE_MIXER2;
    info->color_model = F0R_COLOR_MODEL_PACKED32;
    info->frei0r_version = FREI0R_MAJOR_VERSION;
    info->major_version = 0;
    info->minor_version = 2;               // bumped version
    info->num_params = 13;
    info->explanation = "Versatile directional wipe with content-aware range, colored bar, soft feathering, and improved gentle arrival.";
}

void f0r_get_param_info(f0r_param_info_t *info, int idx) {
    const char* names[] = {"position", "limit_range_to_clip", "direction_axis", "direction_wheel", "speed_curve", "gentle_arrival",
                           "border_type", "bar_width", "bar_color_hue", "bar_color_saturaion",
                           "bar_color_value", "border_blur", "invert"};
    const char* expl[] = {"Wipe position (progress)", "Limit the bar to clips", "Wipe Direction Axis", "Wipe Direction Wheel",
                          "Speed Curve (%)", "Gentle Arrival (%)", "Border Type", "Bar Width (px at 1920)", "Bar Color Hue", "Bar Color Saturation", "Bar Color Value", "Blur",
                          "Invert"};

    info->name = names[idx];
    info->explanation = expl[idx];
    info->type = (idx == 1 || idx == 12) ? F0R_PARAM_BOOL : F0R_PARAM_DOUBLE;
}

f0r_instance_t f0r_construct(unsigned int w, unsigned int h) {
    omni_wipe_t *inst = calloc(1, sizeof(omni_wipe_t));
    if (!inst) return NULL;

    inst->width = w;
    inst->height = h;
    inst->resolution_scale = (double)w / REFERENCE_WIDTH;

    inst->bar_width = 8.0;
    inst->bar_hue = 0.0;
    inst->bar_sat = 100.0;
    inst->bar_val = 100.0;
    inst->border_blur = 12.0;
    inst->limit_range_to_clip = 1;
    inst->last_speed_curve = -1.0;

    return (f0r_instance_t)inst;
}

void f0r_destruct(f0r_instance_t i) { free(i); }

void f0r_set_param_value(f0r_instance_t i, f0r_param_t p, int idx) {
    omni_wipe_t *inst = (omni_wipe_t*)i;
    double v = *(double*)p;

    switch (idx) {
        case 0:  inst->position = v; break;
        case 1: inst->limit_range_to_clip = (v > 0.5) ? 1 : 0; inst->bounds_calculated = 0; break;
        case 2:
            inst->direction_axis = (int)(v + 0.5);
            if (inst->direction_axis < 0) inst->direction_axis = 0;
            if (inst->direction_axis > 2) inst->direction_axis = 2;
            break;
        case 3:  inst->direction_wheel = v; break;
        case 4:  inst->speed_curve = v; break;
        case 5:  inst->gentle_arrival = v; break;
        case 6:
            inst->border_type = (int)(v + 0.5);
            if (inst->border_type < 0) inst->border_type = 0;
            if (inst->border_type > 1) inst->border_type = 1;
            break;
        case 7:  inst->bar_width = v; break;
        case 8:  inst->bar_hue = v; break;
        case 9:  inst->bar_sat = v; break;
        case 10: inst->bar_val = v; break;
        case 11: inst->border_blur = v; break;
        case 12: inst->invert = (v > 0.5) ? 1 : 0; inst->bounds_calculated = 0; break;
    }
}

void f0r_get_param_value(f0r_instance_t i, f0r_param_t p, int idx) {
    omni_wipe_t *inst = (omni_wipe_t*)i;
    double *out = (double*)p;
    switch (idx) {
        case 0: *out = inst->position; break;
        case 1: *out = inst->limit_range_to_clip; break;
        case 2: *out = inst->direction_axis; break;
        case 3: *out = inst->direction_wheel; break;
        case 4: *out = inst->speed_curve; break;
        case 5: *out = inst->gentle_arrival; break;
        case 6: *out = inst->border_type; break;
        case 7: *out = inst->bar_width; break;
        case 8: *out = inst->bar_hue; break;
        case 9: *out = inst->bar_sat; break;
        case 10: *out = inst->bar_val; break;
        case 11: *out = inst->border_blur; break;
        case 12: *out = inst->invert; break;
    }
}

/* ==========================================================================
   Core Helpers
   ========================================================================== */

static void calculate_content_bounds(const uint32_t* buf, int w, int h,
                                     int* min_x, int* min_y, int* max_x, int* max_y) {
    int left = w, right = -1, top = h, bottom = -1;

    for (int x = 0; x < w; ++x) {
        const uint8_t* p = (const uint8_t*)&buf[(h/2) * w + x];
        if (p[0] || p[1] || p[2] || p[3]) {
            if (x < left) left = x;
            if (x > right) right = x;
        }
    }

    for (int y = 0; y < h; ++y) {
        const uint8_t* p = (const uint8_t*)&buf[y * w + (w/2)];
        if (p[0] || p[1] || p[2] || p[3]) {
            if (y < top) top = y;
            if (y > bottom) bottom = y;
        }
    }

    *min_x = (left <= right) ? left : 0;
    *max_x = (left <= right) ? right : w - 1;
    *min_y = (top <= bottom) ? top : 0;
    *max_y = (top <= bottom) ? bottom : h - 1;
}

static void build_curve_lut(omni_wipe_t *inst) {
    double c = inst->speed_curve;
    if (c <= 0.0) {
        for (int i = 0; i < CURVE_LUT_SIZE; ++i)
            inst->curve_lut[i] = i / (double)(CURVE_LUT_SIZE - 1);
    } else {
        double exp_val = 1.0 + (c / 100.0) * 9.9; // Speed Curve logarithmic curve scaling
        for (int i = 0; i < CURVE_LUT_SIZE; ++i) {
            double t = i / (double)(CURVE_LUT_SIZE - 1);
            inst->curve_lut[i] = pow(t, exp_val);
        }
    }
    inst->last_speed_curve = c;
}

static double curve_lookup(const double *lut, double t) {
    if (t <= 0.0) return 0.0;
    if (t >= 1.0) return 1.0;
    double idx = t * (CURVE_LUT_SIZE - 1);
    int i = (int)idx;
    if (i >= CURVE_LUT_SIZE - 1) return lut[CURVE_LUT_SIZE - 1];
    double frac = idx - i;
    return lut[i] * (1.0 - frac) + lut[i + 1] * frac;
}

/* OmniSwipe-style gentle arrival when speed_curve == 0 */
static double reversed_linear(omni_wipe_t *inst, double t) {
    double strength = 1.0 + (inst->gentle_arrival / 100.0) * 9.9; //Gentle Arrival (When the Speed Curve = 0) logarithmic curve scaling
    return 1.0 - pow(1.0 - t, strength);
}

static double get_progress(omni_wipe_t *inst, double p) {
    if (p <= 0.0) return 0.0;
    if (p >= 1.0) return 1.0;

    if (fabs(inst->speed_curve - inst->last_speed_curve) > 0.0001)
        build_curve_lut(inst);

    if (inst->gentle_arrival <= 0.001)
        return curve_lookup(inst->curve_lut, p);

    if (inst->speed_curve <= 0.0)
        return reversed_linear(inst, p);

    /* Combined curve + gentle arrival */
    double g = inst->gentle_arrival / 100.0;
    double main_end = 1.0 - g;

    if (p <= main_end)
        return main_end * curve_lookup(inst->curve_lut, p / main_end);

    double zone_t = (p - main_end) / g;
    return main_end + (1.0 - curve_lookup(inst->curve_lut, 1.0 - zone_t)) * g;
}

static void get_wipe_vector(int axis, double wheel, double *dx, double *dy) {
    double rad = (axis * 90.0 + wheel) * M_PI / 180.0;
    *dx = cos(rad);
    *dy = sin(rad);
}

static void compute_projection_range(omni_wipe_t *inst, double *start_p, double *end_p) {
    int minx = inst->out_min_x, maxx = inst->out_max_x;
    int miny = inst->out_min_y, maxy = inst->out_max_y;
    double dx = inst->wipe_dx, dy = inst->wipe_dy;

    double p1 = minx*dx + miny*dy, p2 = maxx*dx + miny*dy;
    double p3 = minx*dx + maxy*dy, p4 = maxx*dx + maxy*dy;

    double minp = fmin(fmin(p1,p2), fmin(p3,p4));
    double maxp = fmax(fmax(p1,p2), fmax(p3,p4));

    if (maxp - minp < 1.0) {
        minp = 0.0;
        maxp = inst->width * fabs(dx) + inst->height * fabs(dy);
    }
    *start_p = minp;
    *end_p = maxp;
}

static inline uint32_t hsv_to_rgba(double h_deg, double s, double v) {
    double h = fmod(h_deg * (360.0 / 120.0), 360.0) / 60.0;
    double c = v * s;
    double x = c * (1.0 - fabs(fmod(h, 2.0) - 1.0));
    double m = v - c;
    double r=0, g=0, b=0;

    if (h < 1.0)      { r = c; g = x; }
    else if (h < 2.0) { r = x; g = c; }
    else if (h < 3.0) { g = c; b = x; }
    else if (h < 4.0) { g = x; b = c; }
    else if (h < 5.0) { r = x; b = c; }
    else              { r = c; b = x; }

    uint8_t rr = (uint8_t)((r + m) * 255.0);
    uint8_t gg = (uint8_t)((g + m) * 255.0);
    uint8_t bb = (uint8_t)((b + m) * 255.0);
    return (0xFFu << 24) | (rr << 16) | (gg << 8) | bb;
}

static double smoothstep(double edge0, double edge1, double x) {
    x = fmax(0.0, fmin(1.0, (x - edge0) / (edge1 - edge0)));
    return x * x * (3.0 - 2.0 * x);
}

static inline uint32_t get_pixel(const uint32_t *buf, int w, int h, int x, int y,
                                 int minx, int miny, int maxx, int maxy) {
    if (x < minx || x > maxx || y < miny || y > maxy || x < 0 || x >= w || y < 0 || y >= h) return 0;
    return buf[y * w + x];
}

static inline uint32_t blend_rgba(uint32_t c1, uint32_t c2, double t) {
    uint8_t a1=(c1>>24)&0xFF, r1=(c1>>16)&0xFF, g1=(c1>>8)&0xFF, b1=c1&0xFF;
    uint8_t a2=(c2>>24)&0xFF, r2=(c2>>16)&0xFF, g2=(c2>>8)&0xFF, b2=c2&0xFF;
    uint8_t a = (uint8_t)(a1*(1-t) + a2*t);
    uint8_t r = (uint8_t)(r1*(1-t) + r2*t);
    uint8_t g = (uint8_t)(g1*(1-t) + g2*t);
    uint8_t b = (uint8_t)(b1*(1-t) + b2*t);
    return (a<<24) | (r<<16) | (g<<8) | b;
}

/* ==========================================================================
   Main Render
   ========================================================================== */

void f0r_update2(f0r_instance_t i, double time, const uint32_t *in1, const uint32_t *in2,
                 const uint32_t *in3, uint32_t *out) {
    omni_wipe_t *inst = (omni_wipe_t*)i;
    double p = fmax(0.0, fmin(1.0, inst->position));

    const uint32_t *clip_out = inst->invert ? in1 : in2;
    const uint32_t *clip_in  = inst->invert ? in2 : in1;

    int w = inst->width, h = inst->height;

    if (!inst->bounds_calculated) {
        if (inst->limit_range_to_clip) {
            calculate_content_bounds(clip_out, w, h, &inst->out_min_x, &inst->out_min_y, &inst->out_max_x, &inst->out_max_y);
            calculate_content_bounds(clip_in,  w, h, &inst->in_min_x,  &inst->in_min_y,  &inst->in_max_x,  &inst->in_max_y);
            inst->combined_min_x = fmin(inst->out_min_x, inst->in_min_x);
            inst->combined_min_y = fmin(inst->out_min_y, inst->in_min_y);
            inst->combined_max_x = fmax(inst->out_max_x, inst->in_max_x);
            inst->combined_max_y = fmax(inst->out_max_y, inst->in_max_y);
        } else {
            inst->out_min_x = inst->in_min_x = 0; inst->out_max_x = inst->in_max_x = w-1;
            inst->out_min_y = inst->in_min_y = 0; inst->out_max_y = inst->in_max_y = h-1;
            inst->combined_min_x = 0; inst->combined_max_x = w-1;
            inst->combined_min_y = 0; inst->combined_max_y = h-1;
        }
        inst->bounds_calculated = 1;
    }

    get_wipe_vector(inst->direction_axis, inst->direction_wheel, &inst->wipe_dx, &inst->wipe_dy);
    compute_projection_range(inst, &inst->start_proj, &inst->end_proj);
    inst->bar_color_packed = hsv_to_rgba(inst->bar_hue, inst->bar_sat/100.0, inst->bar_val/100.0);

    double scale = inst->resolution_scale;
    double eff_bar = inst->bar_width * scale;
    double half_bar = eff_bar * 0.5;
    double blur_rad = fmax(2.0, inst->border_blur * scale * 5);

    double prog = get_progress(inst, p);
    double main_proj = inst->start_proj + prog * (inst->end_proj - inst->start_proj);
    double bar_proj = main_proj;

    if (inst->border_type == 1) {
        double pad = half_bar + blur_rad * 1.2;
        bar_proj = (inst->start_proj - pad) + prog * ((inst->end_proj - inst->start_proj) + 2*pad);
    }

    for (int y = 0; y < h; ++y) {
        int row = y * w;
        for (int x = 0; x < w; ++x) {
            double proj = x * inst->wipe_dx + y * inst->wipe_dy;
            double dist = proj - bar_proj;

            uint32_t col_in  = get_pixel(clip_in,  w, h, x, y, inst->in_min_x,  inst->in_min_y,  inst->in_max_x,  inst->in_max_y);
            uint32_t col_out = get_pixel(clip_out, w, h, x, y, inst->out_min_x, inst->out_min_y, inst->out_max_x, inst->out_max_y);

            double mix_t = (inst->border_type == 1) ? (dist < 0.0 ? 0.0 : 1.0)
                          : (inst->border_blur > 0.5 ? smoothstep(-1.0, 1.0, dist / blur_rad) : (dist < 0.0 ? 0.0 : 1.0));

            uint32_t base = blend_rgba(col_in, col_out, mix_t);
            uint32_t final = base;

            if (inst->border_type == 1 && eff_bar > 0.1) {
                int limited = inst->limit_range_to_clip &&
                    (x < inst->combined_min_x || x > inst->combined_max_x ||
                     y < inst->combined_min_y || y > inst->combined_max_y);

                if (!limited) {
                    double bar_dist = fabs(dist);
                    double bar_mix = 0.0;
                    if (bar_dist <= half_bar) {
                        bar_mix = 1.0;
                    } else if (inst->border_blur > 0.5) {
                        double edge = bar_dist - half_bar;
                        if (edge < blur_rad) {
                            bar_mix = 1.0 - smoothstep(0.0, 1.0, edge / blur_rad);
                        }
                    }

                    if (bar_mix > 0.01) {
                        uint8_t bar_a = (uint8_t)(255.0 * bar_mix);
                        uint8_t br = (inst->bar_color_packed >> 16) & 0xFF;
                        uint8_t bg = (inst->bar_color_packed >> 8) & 0xFF;
                        uint8_t bb = inst->bar_color_packed & 0xFF;

                        uint8_t cr = (base >> 16) & 0xFF, cg = (base >> 8) & 0xFF, cb = base & 0xFF;

                        uint8_t fr = (uint8_t)((cr * (255 - bar_a) + br * bar_a) / 255);
                        uint8_t fg = (uint8_t)((cg * (255 - bar_a) + bg * bar_a) / 255);
                        uint8_t fb = (uint8_t)((cb * (255 - bar_a) + bb * bar_a) / 255);
                        uint8_t fa = (uint8_t)fmax((base >> 24) & 0xFF, bar_a);

                        final = (fa << 24) | (fr << 16) | (fg << 8) | fb;
                    }
                }
            }
            out[row + x] = final;
        }
    }
}
