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
//!  * Every step the planner asks for ("primary" step) is remembered in a queue together with the
//!    time it happened and its direction.
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
//!  * One queue per axis (2 B per entry) instead of one shared queue: simpler and independent.
//!  * When a queue is full the oldest echo is applied early instead of overflowing: shaping
//!    gets less effective at very high step rates, it never loses a step.
#pragma once
#include <stdint.h>

#ifndef FORCE_INLINE
#define FORCE_INLINE __attribute__((always_inline)) inline
#endif

#ifndef SHAPING_QUEUE_SIZE
#define SHAPING_QUEUE_SIZE 160 //!< echoes buffered per axis (2 B each)
#endif
static_assert(SHAPING_QUEUE_SIZE >= 16 && SHAPING_QUEUE_SIZE <= 255, "SHAPING_QUEUE_SIZE must fit in uint8_t");

namespace shaping {

constexpr uint8_t  kQueue         = SHAPING_QUEUE_SIZE;
constexpr uint16_t kTimeMask      = 0x7FFF; //!< stored times are 15 bits, bit 0 of an entry is the direction
constexpr uint8_t  kMaxPerTick    = 8;      //!< echoes applied per axis and ISR tick at most
constexpr float    kMinFrequency  = 10.0f;  //!< [Hz] lowest accepted shaper frequency
constexpr float    kMaxFrequency  = 200.0f; //!< [Hz]

//! State of one shaped axis. Plain data, lives in .bss (all zero = shaping off).
struct Axis {
    uint16_t q[kQueue];  //!< (time << 1) | (direction > 0); time in units of 8 us
    uint8_t  head;       //!< oldest entry
    uint8_t  tail;       //!< next free entry
    uint8_t  count;      //!< entries in the queue
    uint16_t delay;      //!< echo delay in 8 us units (half the ringing period); 0 = shaping off
    int16_t  de;         //!< secondary Bresenham accumulator, 1/128 step
    uint8_t  f1;         //!< weight of the live step, out of 128
    uint8_t  f2;         //!< weight of the echo, out of 128
    int8_t   dir;        //!< direction the DIR pin currently selects (+1/-1, 0 = unknown)
    bool     active;     //!< the running block is shaped
    float    freq;       //!< [Hz] as set by M593 (for reporting)
    float    zeta;       //!< damping ratio as set by M593 (for reporting)
};

//! Time base: 8 us units, advanced from the stepper timer (0.5 us ticks).
struct Clock {
    uint16_t now;
    uint8_t  frac;       //!< leftover timer ticks, 0..15
};

//! Add @p ticks timer ticks (0.5 us each) that elapsed since the previous call.
FORCE_INLINE void advance(Clock& c, uint16_t ticks) {
    const uint32_t t = uint32_t(c.frac) + ticks;
    c.now  += uint16_t(t >> 4);
    c.frac  = uint8_t(t & 15);
}

//! @return true while the axis has echoes pending
FORCE_INLINE bool busy(const Axis& a) { return *(volatile const uint8_t*)&a.count != 0; }

//! Drop all pending echoes (used after an abort). The positions already counted stay valid, they
//! count the steps that were really made.
inline void purge(Axis& a) { a.head = a.tail = a.count = 0; a.de = 0; }

//! Add @p w (1/128 step, signed) to the accumulator; emit a pulse through the policy if it crossed +-64.
//! Policy `P` must provide `static void P::emit(Axis&, int8_t dir)`.
template<class P>
FORCE_INLINE void apply(Axis& a, int16_t w) {
    int16_t de = a.de + w;
    if (de >= 64)       { de -= 128; P::emit(a, +1); }
    else if (de <= -64) { de += 128; P::emit(a, -1); }
    a.de = de;
}

//! Apply the oldest echo now.
template<class P>
__attribute__((noinline)) void echo_one(Axis& a) {
    const uint16_t e = a.q[a.head];
    if (++a.head == kQueue) a.head = 0;
    --a.count;
    apply<P>(a, (e & 1) ? int16_t(a.f2) : -int16_t(a.f2));
}

//! A primary step of the block (the planner wants one step in direction @p dir) at time @p now.
template<class P>
FORCE_INLINE void step(Axis& a, uint16_t now, int8_t dir) {
    if (a.count == kQueue) echo_one<P>(a);            // full: apply the oldest echo early
    a.q[a.tail] = uint16_t(((now & kTimeMask) << 1) | (dir > 0 ? 1 : 0));
    if (++a.tail == kQueue) a.tail = 0;
    ++a.count;
    apply<P>(a, dir > 0 ? int16_t(a.f1) : -int16_t(a.f1));
}

//! Apply every echo that is due at time @p now.
template<class P>
FORCE_INLINE void flush(Axis& a, uint16_t now) {
    uint8_t budget = kMaxPerTick;
    while (a.count && budget) {
        const uint16_t age = uint16_t(now - (a.q[a.head] >> 1)) & kTimeMask;
        if (age < a.delay) break;
        echo_one<P>(a);
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

//! Configure an axis. Frequency 0 turns shaping off. The caller guarantees that no block runs
//! and that the stepper interrupt is not touching the axis (see M593).
//! @return false if the frequency is out of range (nothing changed)
inline bool configure(Axis& a, float freq, float zeta) {
    if (freq != 0.0f && (freq < kMinFrequency || freq > kMaxFrequency)) return false;
    if (zeta < 0.0f) zeta = 0.0f;
    if (zeta > 0.99f) zeta = 0.99f;
    a.f2 = echo_weight(zeta);
    a.f1 = uint8_t(128 - a.f2);
    a.freq = freq;
    a.zeta = zeta;
    a.delay = (freq == 0.0f) ? 0 : uint16_t(62500.0f / freq + 0.5f); // 1/(2f) in 8 us units
    return true;
}

} // namespace shaping
