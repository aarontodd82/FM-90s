#ifndef OPL3_DUO_LOGGED_H
#define OPL3_DUO_LOGGED_H

#include <OPL3Duo.h>
#include "opl_register_log.h"

/**
 * OPL3DuoLogged - OPL3Duo subclass that logs all register writes
 *
 * This class wraps OPL3Duo and intercepts all register write operations
 * to log them to the global g_oplLog for real-time visualization in the GUI.
 *
 * All register writes flow through the write() method in the OPL3 base class,
 * so we only need to override that one method.
 */
class OPL3DuoLogged : public OPL3Duo {
public:
    OPL3DuoLogged() : OPL3Duo() {}

    OPL3DuoLogged(byte a2, byte a1, byte a0, byte latch, byte reset)
        : OPL3Duo(a2, a1, a0, latch, reset) {}

    /**
     * Override write() to log all register writes
     *
     * The write() method is called by all higher-level functions
     * (setChipRegister, setChannelRegister, setOperatorRegister, etc.)
     * so intercepting here captures everything.
     *
     * @param bank - 0 for primary register set, 1 for OPL3 extended registers
     * @param reg - Register address (0x00-0xFF)
     * @param value - Value to write (0x00-0xFF)
     */
    virtual void write(byte bank, byte reg, byte value) override {
        // Determine which chip this write is for
        // Bank bit is incorporated into the register address for chip determination
        byte chip = (bank == 1) ? 0 : 1;  // Bank 0 = chip 1, Bank 1 = chip 0

        // The library uses bank to select between low/high register sets
        // For OPL3, bank 0 = registers 0x00-0xFF, bank 1 = registers 0x100-0x1FF
        short fullReg = bank ? (reg | 0x100) : reg;

        // Log the register write
        g_oplLog.logWrite(chip, fullReg, value);

        // Call parent implementation to actually perform the write
        OPL3Duo::write(bank, reg, value);
    }
};

#endif // OPL3_DUO_LOGGED_H
