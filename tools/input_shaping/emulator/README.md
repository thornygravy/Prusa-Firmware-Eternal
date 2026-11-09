# Running the real firmware in simavr

`fwrun.c` boots the firmware ELF (`build_gen/MK3S/MK3S_ENGLISH`) in the simavr ATmega2560 model, feeds it G-code
over the serial port, and writes every step/direction pin edge with its cycle number. There is no printer model, so
the harness papers over what is missing:

* thermistor inputs get room-temperature voltages (zero would trigger a MINTEMP kill) and AVCC/AREF are 5 V;
* the UART "transmit buffer empty" flag is forced on, and simavr's UART poll-sleep is turned off;
* the encoder button is pressed for 200 ms by a `CLICK` line (the missing XFLASH and the first-run dialogs wait for it);
* `SEED="0x0F5F:00"` writes EEPROM bytes (0x0F5F = EEPROM_WIZARD_ACTIVE) to skip the first-run wizard;
* `POKE <addr> <val>` writes a RAM byte. A real printer has `check_endstops` false after homing; the simulator cannot
  home (stallGuard), so poke it to 0 (`avr-nm -C ELF | grep check_endstops`) to emulate the homed state, otherwise
  the shaper correctly stays off (endstops on = homing/probing = never shaped);
* `PEEK16=<addr>` prints a 16 bit variable at the end (e.g. `SP_min` of a `-DDEBUG_STACK_MONITOR` build).

    gcc -O2 -I/usr/include/simavr -I/usr/include/simavr/avr fwrun.c -o fwrun -lsimavr -lelf -lm
    SEED="0x0F5F:00" ./fwrun MK3S_ENGLISH example_shaped_moves.txt events.txt 19

Script lines are `<seconds> <G-code | CLICK | POKE addr val>`; `boot_clicks.txt` is the click sequence needed to get
through the boot dialogs. Events are `<cycle> <id> <level>`: ids 0-3 = X/Y/Z/E step (PC0-PC3, double-edge: every edge
is a step), 10/11 = X/Y direction (PL0/PL1; X high = negative, Y high = positive).

Build with `-DDEBUG_ISR_LOAD` and send `M503` before and after a move to read the stepper ISR duty cycle.
