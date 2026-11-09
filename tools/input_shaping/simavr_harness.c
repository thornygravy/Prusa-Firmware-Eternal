#include <stdio.h>
#include <string.h>
#include "sim_avr.h"
#include "sim_elf.h"
static uint64_t marks[64]; static int nm;
static void on_mark(struct avr_t* avr, avr_io_addr_t addr, uint8_t v, void* p) { if (nm < 64) marks[nm++] = avr->cycle; avr->data[addr] = v; }
int main(int argc, char** argv) {
    elf_firmware_t f; memset(&f, 0, sizeof f);
    if (elf_read_firmware(argv[1], &f)) { fprintf(stderr, "cannot read elf\n"); return 1; }
    strcpy(f.mmcu, "atmega2560"); f.frequency = 16000000;
    avr_t* avr = avr_make_mcu_by_name("atmega2560"); if (!avr) return 2;
    avr_init(avr); avr_load_firmware(avr, &f);
    avr_register_io_write(avr, 0x3E, on_mark, NULL);       // GPIOR0
    int st; long guard = 0;
    while (nm < 12 && (st = avr_run(avr)) != cpu_Done && st != cpu_Crashed && ++guard < 500000000L) ;
    const char* names[] = {"diagonal, 2 steps/axis/tick", "X only, 2 steps/tick", "X only, 1 step/tick"};
    for (int i = 0; i + 3 < nm + 0 && i < 3; ++i) {}
    for (int k = 0; k < 3; ++k) {
        double stock = (marks[4*k+1]-marks[4*k]) / 2000.0, shaped = (marks[4*k+3]-marks[4*k+2]) / 2000.0;
        printf("%-30s stock %6.1f cycles/tick   shaped %6.1f cycles/tick   extra %+6.1f  (%.1f us at 16 MHz)\n", names[k], stock, shaped, shaped-stock, (shaped-stock)/16.0);
    }
    return 0;
}
