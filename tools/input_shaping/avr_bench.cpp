// AVR cycle benchmark of the shaper core. Marks are written to GPIOR0; the host (simavr) records avr->cycle.
#include <avr/io.h>
#include <avr/sleep.h>
#include <avr/interrupt.h>
#include "../../Firmware/input_shaping.h"
using namespace shaping;

static Axis ax, ay; static Clock clk;
static volatile long posx, posy;
// same work as the firmware policies: DIR on change, STEP toggle (double-edge: PIN write), count_position update
struct PX { static __attribute__((always_inline)) inline void emit(Axis& a, int8_t d) { if (d != a.dir) { a.dir = d; PORTB ^= 1; asm("nop"); asm("nop"); } PINA = 1; posx += d; } };
struct PY { static __attribute__((always_inline)) inline void emit(Axis& a, int8_t d) { if (d != a.dir) { a.dir = d; PORTB ^= 2; asm("nop"); asm("nop"); } PINA = 2; posy += d; } };

#define MARK() do { GPIOR0 = 1; } while (0)
template<int NX, int NY> static void tick_stock() {
    for (int i = 0; i < NX; ++i) { PINA = 1; posx += 1; }
    for (int i = 0; i < NY; ++i) { PINA = 2; posy += 1; }
}
template<int NX, int NY> static void tick_shaped() {
    advance(clk, 200);                       // 100 us elapsed
    flush<PX>(ax, clk.now8); flush<PY>(ay, clk.now8);
    if (NX) steps<PX>(ax, clk.now8, 1, NX);
    if (NY) steps<PY>(ay, clk.now8, 1, NY);
}
static void reset() { ax = Axis{}; ay = Axis{}; clk = Clock{}; configure(ax, 40, 0.1f); configure(ay, 40, 0.1f); ax.dir = ay.dir = 1; }

template<int NX, int NY> static void phase_stock(int n)  { MARK(); for (int i = 0; i < n; ++i) tick_stock<NX,NY>();  MARK(); }
template<int NX, int NY> static void phase_shaped(int n) {
    reset(); for (int i = 0; i < 300; ++i) tick_shaped<NX,NY>();    // warm up: fill the echo queue
    MARK(); for (int i = 0; i < n; ++i) tick_shaped<NX,NY>(); MARK();
}
int main() {
    DDRA = 0xFF; DDRB = 0xFF;
    const int N = 2000;
    phase_stock<2,2>(N);  phase_shaped<2,2>(N);     // diagonal, 2 steps per axis per tick
    phase_stock<2,0>(N);  phase_shaped<2,0>(N);     // X only, 2 steps per tick
    phase_stock<1,0>(N);  phase_shaped<1,0>(N);     // X only, 1 step per tick
    cli(); sleep_cpu();
}
