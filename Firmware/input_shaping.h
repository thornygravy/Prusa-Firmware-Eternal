//! @file
//! @brief ZV input shaper for the X and Y axes (EXPERIMENTAL)
//!
//! Based on the integrated ZV shaper of Marlin 2.1.2 (MarlinFirmware/Marlin, GPL-3.0), reworked for
//! Prusa's stepper ISR. Only this header knows the algorithm; stepper.cpp supplies the pin handling.
//!
//! How it works
//! ------------
//! A ZV shaper replaces one move by two half-amplitude copies: one now, one delayed by half the
//! ringing period. The two excite the frame in opposite phase, so the vibration cancels. It is done
//! on the step pulses themselves:
//!
//!  * The steps the planner asks for ("primary" steps) are remembered in a queue, one entry per stepper
//!    ISR tick: time, number of steps (the ISR makes up to 4 per tick) and direction.
//!  * Every axis owns a second Bresenham accumulator `de`, counting in 1/128 of a step. A primary
//!    step adds `f1` (its share of the amplitude), the same step "echoed" half a period later adds
//!    `f2`. `f1 + f2 == 128`, so every planned step adds up to exactly one physical step.
//!  * Whenever `de` reaches +-64 a real step pulse is emitted and `de` is brought back by 128.
//!
//! The amplitudes are f1/128 = 1/(1+K) and f2/128 = K/(1+K), K = exp(-zeta*pi/sqrt(1-zeta^2)).
//!
//! Differences to Marlin
//! ---------------------
//!  * The echo clock only ticks inside the stepper interrupt, and due echoes are applied on the next
//!    tick. No extra timer wake-ups are needed; the cost is an echo that is up to one ISR period
//!    late (100 us at 10 kHz), about 1.4 degrees of phase at 40 Hz.
//!  * One queue per axis, one 2 byte entry per ISR tick (not per step): about twice as cheap on the 8 bit CPU
//!    and half the RAM of a per-step queue.
//!  * When a queue is full the oldest echo is applied early instead of overflowing: shaping
//!    gets less effective at very high step rates, it never loses a step.
#pragma once
#include <stdint.h>

#ifndef FORCE_INLINE
#define FORCE_INLINE __attribute__((always_inline)) inline
#endif

#ifndef SHAPING_QUEUE_SIZE
#define SHAPING_QUEUE_SIZE 128 //!< echoes buffered per axis = ISR ticks within half a ringing period (2 B each)
#endif
static_assert(SHAPING_QUEUE_SIZE >= 16 && SHAPING_QUEUE_SIZE <= 255, "SHAPING_QUEUE_SIZE must fit in uint8_t");

namespace shaping {

constexpr uint8_t  kQueue         = SHAPING_QUEUE_SIZE;
constexpr uint8_t  kMaxPerTick    = 8;      //!< echoes (ticks) applied per axis and ISR tick at most
constexpr uint8_t  kMaxSteps      = 4;      //!< steps per tick one entry can hold (step_loops is at most 4)
constexpr float    kMinFrequency  = 10.0f;  //!< [Hz] lowest accepted shaper frequency
constexpr float    kMaxFrequency  = 200.0f; //!< [Hz]

//! State of one shaped axis. Plain data, lives in .bss (all zero = shaping off until configure()).
struct Axis {
    //! entry = time | (steps - 1) << 1 | (direction > 0). Time is in 16 us units kept in the upper 13
    //! bits (see Clock), so entry ages compare with plain 16 bit arithmetic and need no shifting.
    uint16_t q[kQueue];
    uint16_t* head;      //!< oldest entry
    uint16_t* tail;      //!< next free entry
    uint8_t  count;      //!< entries in the queue
    uint16_t delay;      //!< echo delay in 16 us units (half the ringing period); 0 = shaping off
    uint16_t delay8;     //!< the same scaled like the clock, to compare with entry ages
    int16_t  de;         //!< secondary Bresenham accumulator, 1/128 step
    uint8_t  f1;         //!< weight of the live step, out of 128
    uint8_t  f2;         //!< weight of the echo, out of 128
    int8_t   dir;        //!< direction the DIR pin currently selects (+1/-1, 0 = unknown)
    bool     active;     //!< the running block is shaped
    float    freq;       //!< [Hz] as set by M593 (for reporting)
    float    zeta;       //!< damping ratio as set by M593 (for reporting)
};

//! Time base: 16 us units, stored x8 so that the low 3 bits are free for the entry flags. It is advanced from
//! the stepper timer (0.5 us ticks). 16 bits wrap after 131 ms, far more than the longest echo delay (50 ms).
struct Clock {
    uint16_t now8;
    uint8_t  frac;       //!< leftover timer ticks, 0..31
};

//! Add @p ticks timer ticks (0.5 us each) that elapsed since the previous call.
FORCE_INLINE void advance(Clock& c, uint16_t ticks) {
    uint16_t t = uint16_t(c.frac + ticks);
    if (t < ticks) t = 0xFFFF;                   // (cannot happen with the 2000 tick idle interval)
    c.now8 += uint16_t(t >> 2) & 0xFFF8;         // = (t / 32) * 8
    c.frac  = uint8_t(t & 31);
}

//! @return true while the axis has echoes pending
FORCE_INLINE bool busy(const Axis& a) { return *(volatile const uint8_t*)&a.count != 0; }

//! Drop all pending echoes (used after an abort). The positions already counted stay valid, they
//! count the steps that were really made.
inline void purge(Axis& a) { a.head = a.tail = a.q; a.count = 0; a.de = 0; }

//! Add @p w (1/128 step, signed) to the accumulator and emit a pulse through the policy for every +-64
//! it crosses. Policy `P` must provide `static void P::emit(Axis&, int8_t dir)`.
template<class P>
FORCE_INLINE void apply(Axis& a, int16_t w) {
    int16_t de = a.de + w;
    if (de >= 64) {
        do { de -= 128; P::emit(a, +1); } while (de >= 64);
    } else {
        // (not a second loop after the first: a forward step can leave de at exactly -64)
        while (de <= -64) { de += 128; P::emit(a, -1); }
    }
    a.de = de;
}

//! Apply the entry @p e (the oldest one) now: its echo.
template<class P>
FORCE_INLINE void echo_inline(Axis& a, uint16_t e) {
    if (++a.head == a.q + kQueue) a.head = a.q;
    --a.count;
    const int16_t w = int16_t(uint8_t(((e >> 1) & 3) + 1) * a.f2);
    apply<P>(a, (e & 1) ? w : -w);
}

//! The same as a call, for the rare "queue full" case in steps().
template<class P>
__attribute__((noinline)) void echo_early(Axis& a) { echo_inline<P>(a, *a.head); }

//! @p n steps (1..kMaxSteps) of the block in direction @p dir, made in one ISR tick at time @p now8.
template<class P>
FORCE_INLINE void steps(Axis& a, uint16_t now8, int8_t dir, uint8_t n) {
    if (a.count == kQueue) echo_early<P>(a);          // full: apply the oldest echo early
    *a.tail = uint16_t((now8 & 0xFFF8) | (uint8_t(n - 1) << 1) | (dir > 0 ? 1 : 0));
    if (++a.tail == a.q + kQueue) a.tail = a.q;
    ++a.count;
    const int16_t w = int16_t(n * a.f1);
    apply<P>(a, dir > 0 ? w : -w);
}

//! Apply every echo that is due at time @p now8.
template<class P>
FORCE_INLINE void flush(Axis& a, uint16_t now8) {
    uint8_t budget = kMaxPerTick;
    while (a.count && budget) {
        const uint16_t e = *a.head;
        if (uint16_t(now8 - (e & 0xFFF8)) < a.delay8) break;
        echo_inline<P>(a, e);
        --budget;
    }
}

//! f2 (echo weight, out of 128) for damping ratio @p zeta: 128 * K/(1+K) as the 1:7 fixed point
//! polynomial fit used by Marlin.
inline uint8_t echo_weight(float zeta) {
    if (zeta <= 0.0f) return 64;
    if (zeta >= 1.0f) return 0;
    const float z2 = zeta * zeta, z3 = z2 * zeta;
    float f = 64.44056192f - 99.02008832f * zeta - 7.58095488f * z2 + 43.073216f * z3;
    if (f < 0.0f) f = 0.0f;
    return uint8_t(f);
}

//! Configure an axis. Frequency 0 turns shaping off. The caller guarantees that no block runs,
//! that the queue is empty and that the stepper interrupt is not touching the axis (see M593).
//! @return false if the frequency is out of range (nothing changed)
inline bool configure(Axis& a, float freq, float zeta) {
    if (freq != 0.0f && (freq < kMinFrequency || freq > kMaxFrequency)) return false;
    if (zeta < 0.0f) zeta = 0.0f;
    if (zeta > 0.99f) zeta = 0.99f;
    a.f2 = echo_weight(zeta);
    a.f1 = uint8_t(128 - a.f2);
    a.freq = freq;
    a.zeta = zeta;
    a.delay = (freq == 0.0f) ? 0 : uint16_t(31250.0f / freq + 0.5f); // 1/(2f) in 16 us units
    a.delay8 = uint16_t(a.delay << 3);
    a.head = a.tail = a.q; a.count = 0; a.de = 0;
    return true;
}

} // namespace shaping
