/// @file
/// @brief Drop-in replacement for the MMU2 class when the firmware is built with NO_MMU.
/// @details Every method does nothing and reports "not available", and Enabled() is a compile-time
/// false, so the compiler removes all code guarded by `if (mmu2.Enabled())`. The firmware then behaves
/// like the stock firmware with "MMU Mode: Off", but none of the MMU protocol, error-screen and
/// tool-change code is linked. The interface below mirrors the methods the rest of the firmware calls.
#pragma once

#include <stdint.h>
#include "mmu2_state.h"
#include "mmu2/error_codes.h"
#include "mmu2/buttons.h"

namespace MMU2 {

enum : uint8_t {
    FILAMENT_UNKNOWN = 0xffU
};

struct Version {
    uint8_t major, minor, build;
};

class MMU2 {
public:
    enum ResetForm : uint8_t {
        Software = 0,
        ResetPin = 1,
        CutThePower = 2,
        EraseEEPROM = 42,
    };

    void Start() {}
    void Stop() {}
    void Status() {}

    static constexpr xState State() { return xState::Stopped; }
    static constexpr bool Enabled() { return false; }

    void Tune() {}
    void Reset(ResetForm) {}
    void PowerOff() {}
    void PowerOn() {}
    bool ReadRegister(uint8_t) { return false; }
    bool WriteRegister(uint8_t, uint16_t) { return false; }
    void mmu_loop() {}

    bool tool_change(uint8_t) { return false; }
    bool tool_change(char, uint8_t) { return false; }
    bool unload() { return false; }
    bool load_filament(uint8_t) { return false; }
    bool load_filament_to_nozzle(uint8_t) { return false; }
    bool eject_filament(uint8_t, bool = true) { return false; }
    bool cut_filament(uint8_t, bool = true) { return false; }
    bool loading_test(uint8_t) { return false; }
    bool set_filament_type(uint8_t, uint8_t) { return false; }
    void get_statistics() {}
    void Button(uint8_t) {}
    void Home(uint8_t) {}

    static constexpr uint8_t get_current_tool() { return FILAMENT_UNKNOWN; }
    static constexpr uint8_t get_tool_change_tool() { return FILAMENT_UNKNOWN; }

    static constexpr bool FindaDetectsFilament() { return false; }
    static constexpr bool MMU_PRINT_SAVED() { return false; }
    static constexpr uint16_t ToolChangeCounter() { return 0; }
    static constexpr uint16_t TMCFailures() { return 0; }
    static constexpr uint16_t GetLastReadRegisterValue() { return 0; }
    static constexpr Version GetMMUFWVersion() { return { 0, 0, 0 }; }

    void ClearToolChangeCounter() {}
    void ClearTMCFailures() {}
    void InvokeErrorScreen(ErrorCode) {}
    static constexpr Buttons GetPrinterButtonOperation() { return Buttons::NoButton; }
    void ClearPrinterButtonOperation() {}
};

/// One empty object (1 byte of RAM) so that existing `mmu2.Foo()` call sites compile unchanged.
inline MMU2 mmu2;

} // namespace MMU2
