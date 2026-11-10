// Run the real firmware ELF in simavr: serial in/out, step/dir pin trace.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sim_avr.h"
#include "sim_elf.h"
#include "avr_ioport.h"
#include "avr_uart.h"
#include "avr_adc.h"
#include "avr_eeprom.h"
static avr_t* avr; static FILE* ev; static int quiet; static volatile int ok_flag; static char lbuf[96]; static int ll;
static void on_pin(struct avr_irq_t* irq, uint32_t v, void* p) { fprintf(ev, "%llu %d %u\n", (unsigned long long)avr->cycle, (int)(intptr_t)p, v); }
static void on_tx(struct avr_irq_t* irq, uint32_t v, void* p) { if (v == 10) { if (ll >= 2 && lbuf[0] == 'o' && lbuf[1] == 'k') ok_flag = 1; ll = 0; } else if (v != 13 && ll < 90) lbuf[ll++] = (char)v; if (!quiet) { putchar((int)v); fflush(stdout); } }
// script: lines "<seconds> <gcode>" ; sent character by character
struct cmd { double t; char* s; } cmds[512]; int ncmd;
int main(int argc, char** argv) {
    // usage: fwrun firmware.elf script.txt events.txt seconds [quiet]
    elf_firmware_t f; memset(&f, 0, sizeof f);
    if (elf_read_firmware(argv[1], &f)) { fprintf(stderr, "cannot read elf\n"); return 1; }
    strcpy(f.mmcu, "atmega2560"); f.frequency = 16000000;
    double seconds = atof(argv[4]); quiet = argc > 5;
    ev = fopen(argv[3], "w");
    FILE* sc = fopen(argv[2], "r"); char line[512];
    while (sc && fgets(line, sizeof line, sc)) { double t; int n; if (sscanf(line, "%lf %n", &t, &n) >= 1) { line[strcspn(line, "\r\n")] = 0; cmds[ncmd].t = t; cmds[ncmd].s = strdup(line + n); ++ncmd; } }
    avr = avr_make_mcu_by_name("atmega2560"); avr_init(avr); avr_load_firmware(avr, &f);
    // optional EEPROM seeding: SEED="0x0F5F:00,0x0CA6:ff"
    if (getenv("SEED")) { char* sd = strdup(getenv("SEED")); for (char* tok = strtok(sd, ","); tok; tok = strtok(NULL, ",")) {
        unsigned a, v; if (sscanf(tok, "%x:%x", &a, &v) == 2) { uint8_t b = (uint8_t)v; avr_eeprom_desc_t d = { .ee = &b, .offset = a, .size = 1 }; avr_ioctl(avr, AVR_IOCTL_EEPROM_SET, &d); } } }
    avr->log = 0; avr->frequency = 16000000; avr->vcc = avr->avcc = avr->aref = 5000;
    { uint32_t fl = 0; avr_ioctl(avr, AVR_IOCTL_UART_GET_FLAGS('0'), &fl); fl &= ~AVR_UART_FLAG_POLL_SLEEP; avr_ioctl(avr, AVR_IOCTL_UART_SET_FLAGS('0'), &fl); }
    for (int b = 0; b < 4; ++b) avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('C'), b), on_pin, (void*)(intptr_t)b);      // X Y Z E step
    for (int b = 0; b < 2; ++b) avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('L'), b), on_pin, (void*)(intptr_t)(10 + b)); // X Y dir
    avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_UART_GETIRQ('0'), UART_IRQ_OUTPUT), on_tx, NULL);
    int adc_mv[16] = {0}; adc_mv[0] = 4750; adc_mv[2] = 4750; adc_mv[3] = 4750; adc_mv[6] = 4668; adc_mv[4] = 1800; adc_mv[8] = 1500; adc_mv[9] = 1800;
    for (int ch = 0; ch < 16; ++ch) avr_raise_irq(avr_io_getirq(avr, AVR_IOCTL_ADC_GETIRQ, ADC_IRQ_ADC0 + ch), adc_mv[ch]);
    avr_irq_t* rx = avr_io_getirq(avr, AVR_IOCTL_UART_GETIRQ('0'), UART_IRQ_INPUT);
    avr_irq_t* btn = avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('H'), 6);                 // BTN_ENC (Mega pin 9), active low
    avr_raise_irq(btn, 1);
    avr_raise_irq(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('J'), 0), 1);                // SDCARDDETECT: no card
    avr_raise_irq(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('J'), 1), 1);                // BTN_EN2
    avr_raise_irq(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('J'), 2), 1);                // BTN_EN1
    uint64_t release_at = 0;
    uint64_t end = (uint64_t)(seconds * 16000000.0); int ci = 0; const char* cur = NULL; size_t pos = 0; uint64_t next_char = 0;
    uint64_t next_dbg = 0; int oksync = getenv("OKSYNC") != NULL, waiting = 0;
    static uint64_t prof[131072]; double pf_a = -1, pf_b = -1; const char* pf_out = NULL;
    if (getenv("PROFILE")) { char* p = strdup(getenv("PROFILE")); pf_a = atof(strtok(p, ",")); pf_b = atof(strtok(NULL, ",")); pf_out = strtok(NULL, ","); }
    const uint64_t charcyc = getenv("CHARCYC") ? strtoull(getenv("CHARCYC"), NULL, 10) : 4000;
    while (avr->cycle < end) {
        uint32_t pc0 = avr->pc; uint64_t c0 = avr->cycle;
        int st = avr_run(avr);
        if (pf_out && avr->cycle >= (uint64_t)(pf_a * 16e6) && avr->cycle < (uint64_t)(pf_b * 16e6)) prof[(pc0 >> 1) & 0x1FFFF] += avr->cycle - c0;
        avr->data[0xC0] |= (1 << 5); // UCSR0A.UDRE0: the transmit buffer is always ready (simavr quirk)
        if (getenv("PCTRACE") && avr->cycle >= next_dbg) { fprintf(stderr, "PC 0x%05x cycle %llu\n", avr->pc, (unsigned long long)avr->cycle); next_dbg = avr->cycle + 100000; } if (st == cpu_Done || st == cpu_Crashed) { fprintf(stderr, "\n[cpu state %d at %.3f s]\n", st, avr->cycle / 16e6); break; }
        if (ok_flag) { ok_flag = 0; if (waiting) { waiting = 0; fprintf(ev, "%llu 90 0\n", (unsigned long long)avr->cycle); } }
        if (release_at && avr->cycle >= release_at) { avr_raise_irq(btn, 1); release_at = 0; }
        if (!cur && !waiting && ci < ncmd && avr->cycle >= (uint64_t)(cmds[ci].t * 16e6)) {
            if (!strncmp(cmds[ci].s, "POKE ", 5)) { unsigned a, v; sscanf(cmds[ci].s + 5, "%x %x", &a, &v); avr->data[a] = (uint8_t)v; ++ci; }
            else if (!strcmp(cmds[ci].s, "CLICK")) { avr_raise_irq(btn, 0); release_at = avr->cycle + 3200000; ++ci; }   // press for 200 ms
            else { cur = cmds[ci].s; pos = 0; ++ci; }
        }
        if (cur && avr->cycle >= next_char) {
            if (pos <= strlen(cur)) { avr_raise_irq(rx, pos < strlen(cur) ? (uint8_t)cur[pos] : '\n'); ++pos; next_char = avr->cycle + charcyc; }
            else { cur = NULL; waiting = oksync; }
        }
    }
    if (pf_out) { FILE* pf = fopen(pf_out, "w"); for (int i = 0; i < 131072; ++i) if (prof[i]) fprintf(pf, "%x %llu\n", i * 2, (unsigned long long)prof[i]); fclose(pf); }
    if (getenv("PEEK16")) { unsigned a = strtoul(getenv("PEEK16"), NULL, 16); fprintf(stderr, "\n[PEEK16 0x%04x = 0x%04x]", a, avr->data[a] | (avr->data[a + 1] << 8)); }
    fprintf(stderr, "\n[done at %.3f s simulated]\n", avr->cycle / 16e6); fclose(ev); return 0;
}
