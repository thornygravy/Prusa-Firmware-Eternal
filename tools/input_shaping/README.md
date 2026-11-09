# Input shaper tests (`Firmware/input_shaping.h`)

Tools to check the ZV input shaper without a printer.

## Host unit test
Step conservation (random speeds, many direction reversals, several frequencies and damping ratios),
queue overflow, purge and parameter checks, with the ISR modelled as the firmware runs it (one tick per
`step_loops` steps, echoes flushed every tick).

    g++ -std=c++14 -O1 -Wall -Wextra -o host_test host_test.cpp && ./host_test

## Cycle benchmark on a simulated ATmega2560 (simavr)
Runs the same header on an AVR and counts cycles per ISR tick, shaped against stock-style stepping.
Needs `avr-gcc`, `simavr` and `libsimavr-dev`.

    avr-g++ -std=gnu++14 -mmcu=atmega2560 -DF_CPU=16000000L -Os -fno-exceptions -fno-rtti -o avr_bench.elf avr_bench.cpp
    gcc -I/usr/include/simavr -I/usr/include/simavr/avr simavr_harness.c -o simavr_harness -lsimavr -lelf -lm
    ./simavr_harness avr_bench.elf

The numbers are the cost of the shaper alone; they do not include the stock stepper ISR. To see the real duty
cycle of the whole ISR on a printer build with `-DDEBUG_ISR_LOAD` and read it from `M503`.
