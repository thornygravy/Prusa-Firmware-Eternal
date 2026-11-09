// Host test for Firmware/input_shaping.h. The ISR is modelled the way the firmware runs it:
// one tick per step_loops steps, tick period = loops / step rate, echoes flushed at every tick.
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <random>
#include "../../Firmware/input_shaping.h"
using namespace shaping;

struct Rec { double t; int dir; };
static std::vector<Rec> g_out; static double g_t; static long g_pos; static int g_dirtoggles; static int g_lastdir;
struct P { static void emit(Axis& a, int8_t dir) {
    if (dir != a.dir) { a.dir = dir; ++g_dirtoggles; }
    g_out.push_back({g_t, dir}); g_pos += dir; } };

static int fails = 0;
#define CHECK(c, ...) do{ if(!(c)){ ++fails; printf("  FAIL: " __VA_ARGS__); printf("\n"); } }while(0)

// run a list of planned steps (time in s, direction) through the shaper with a given ISR model
// returns planned net steps
static long run(Axis& a, Clock& c, const std::vector<Rec>& steps, double isr_period_s_min, int max_loops, double drain_s) {
    g_out.clear(); g_pos = 0; g_dirtoggles = 0;
    long planned = 0; size_t i = 0; double t = 0.0, last = 0.0;
    double end = steps.empty() ? 0 : steps.back().t + drain_s;
    while (t <= end) {
        // elapsed since last tick, in 0.5us timer ticks
        uint32_t ticks = (uint32_t)llround((t - last) * 2e6); if (ticks > 65535) ticks = 65535; // OCR1A is 16 bit
        advance(c, (uint16_t)ticks); last = t; g_t = t;
        flush<P>(a, c.now8);
        // steps belonging to this tick: all planned steps up to the next tick time
        double next = t + isr_period_s_min;
        int loops = 0; int run_dir = 0, run_n = 0;
        while (i < steps.size() && steps[i].t < next && loops < max_loops) {
            if (run_n && steps[i].dir != run_dir) { shaping::steps<P>(a, c.now8, (int8_t)run_dir, run_n); run_n = 0; }
            run_dir = steps[i].dir; ++run_n; planned += steps[i].dir; ++i; ++loops;
        }
        if (run_n) shaping::steps<P>(a, c.now8, (int8_t)run_dir, run_n);
        t = next;
    }
    return planned;
}

static void setup(Axis& a, Clock& c, float f, float z) { a = Axis{}; c = Clock{}; configure(a, f, z); a.dir = 1; a.active = true; }

int main() {
    std::mt19937 rng(12345);
    // ---- 1. conservation with random speeds and many direction reversals -------------------
    printf("1. conservation (random moves with reversals, several frequencies / damping)\n");
    for (float f : {15.f, 35.f, 40.f, 60.f, 120.f}) for (float z : {0.0f, 0.05f, 0.1f, 0.3f}) {
        Axis a; Clock c; setup(a, c, f, z);
        std::vector<Rec> steps; double t = 0; int dir = 1;
        for (int seg = 0; seg < 60; ++seg) {
            int n = 20 + rng() % 400; double rate = 500 + rng() % 12000;    // steps/s
            if (rng() % 3 == 0) dir = -dir;
            for (int k = 0; k < n; ++k) { t += 1.0 / rate; steps.push_back({t, dir}); }
        }
        long planned = run(a, c, steps, 1.0/8000, 2, 0.2);
        CHECK(g_pos == planned, "f=%.0f z=%.2f: physical net %ld != planned %ld", f, z, g_pos, planned);
        CHECK(a.count == 0, "f=%.0f z=%.2f: queue not empty (%d)", f, z, a.count);
        CHECK(a.de == 0,    "f=%.0f z=%.2f: accumulator %d not back to 0", f, z, a.de);
    }
    // ---- 2. every planned step must be backed by a physical step in the same direction mix ---
    printf("2. unidirectional move: physical steps == planned steps, all forward, count exact\n");
    {   Axis a; Clock c; setup(a, c, 40, 0.1f);
        std::vector<Rec> steps; for (int k = 1; k <= 5000; ++k) steps.push_back({k / 10000.0, 1});
        long planned = run(a, c, steps, 1.0/10000, 1, 0.1);
        int bwd = 0; for (auto& r : g_out) if (r.dir < 0) ++bwd;
        CHECK((long)g_out.size() == planned && g_pos == planned && bwd == 0, "steps %zu planned %ld backward %d", g_out.size(), planned, bwd);
        printf("   planned %ld, physical %zu, backward %d, DIR toggles %d\n", planned, g_out.size(), bwd, g_dirtoggles);
    }
    // ---- 3. tiny queue at a high rate: must degrade (early echoes), never lose a step ---------
    printf("3. queue overflow: very high rate, queue holds only %d ticks\n", kQueue);
    {   Axis a; Clock c; setup(a, c, 20, 0.1f);                     // 25 ms delay needs 25ms*10kHz = 250 tick entries
        std::vector<Rec> steps; for (int k = 1; k <= 20000; ++k) steps.push_back({k / 40000.0, 1});
        long planned = run(a, c, steps, 1.0/10000, 4, 0.2);
        CHECK(g_pos == planned && a.count == 0, "net %ld planned %ld count %d", g_pos, planned, a.count);
        printf("   planned %ld, physical %ld (queue full the whole time -> early echoes)\n", planned, g_pos);
    }
    // ---- 4. purge: abort mid move; counted position must equal the steps actually emitted -----
    printf("4. purge after abort\n");
    {   Axis a; Clock c; setup(a, c, 40, 0.1f);
        std::vector<Rec> steps; for (int k = 1; k <= 3000; ++k) steps.push_back({k / 10000.0, 1});
        std::vector<Rec> first(steps.begin(), steps.begin() + 1500);
        run(a, c, first, 1.0/10000, 1, 0.0);
        long pos_at_abort = g_pos; purge(a);
        CHECK(a.count == 0 && a.de == 0 && !busy(a), "purge left state");
        printf("   physical %ld of 1500 planned at abort (the rest was dropped)\n", pos_at_abort);
    }
    // ---- 5. frequency / zeta -> parameters -----------------------------------------------------
    printf("5. parameters\n");
    {   Axis a{}; 
        CHECK(configure(a, 40, 0.1f) && a.delay == 781 && a.delay8 == 6248 && a.f2 == 54 && a.f1 == 74, "40Hz/0.1: delay %u f1 %u f2 %u", a.delay, a.f1, a.f2);
        CHECK(configure(a, 35, 0.0f) && a.f1 == 64 && a.f2 == 64, "zeta 0 must split 64/64");
        CHECK(!configure(a, 5, 0.1f) && !configure(a, 500, 0.1f), "out-of-range frequency must be refused");
        CHECK(configure(a, 0, 0.1f) && a.delay == 0, "F0 must switch off");
        // compare the fixed point fit with the exact K/(1+K)
        double worst = 0; for (double z = 0.01; z < 0.6; z += 0.01) {
            double K = exp(-z * M_PI / sqrt(1 - z*z)); double exact = 128.0 * K / (1 + K);
            worst = fmax(worst, fabs(echo_weight((float)z) - exact)); }
        printf("   worst |f2 - 128*K/(1+K)| for zeta 0.01..0.6: %.2f (of 128)\n", worst);
        CHECK(worst < 1.5, "polynomial fit error %.2f", worst);
    }
    printf(fails ? "\n%d CHECK(S) FAILED\n" : "\nALL CHECKS PASSED\n", fails);
    return fails != 0;
}
