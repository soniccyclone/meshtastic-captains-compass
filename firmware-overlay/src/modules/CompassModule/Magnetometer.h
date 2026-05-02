// Captain's Compass — QMC5883L magnetometer driver on Wire1.
// See docs/tdd-issue-002-captains-compass.md §10.
//
// The Adafruit nRF52 BSP auto-creates Wire1 from PIN_WIRE1_SDA/SCL
// (defined in upstream variants/nrf52840/heltec_mesh_node_t114/variant.h),
// so we just call Wire1.begin() and use it directly.

#pragma once

#include <stdint.h>

class Magnetometer {
public:
    enum InitResult { OK, NOT_FOUND, INIT_FAILED };

    InitResult begin();          // call once; initializes Wire1 and chip
    bool       read(int16_t &x, int16_t &y, int16_t &z);  // raw, blocking on DRDY
    float      heading();        // calibrated heading, degrees [0, 360)
    bool       isReady() const { return _ready; }

    // Hard-iron offsets (subtracted from raw before heading calc)
    void setCalibration(int16_t ox, int16_t oy, int16_t oz);
    void getCalibration(int16_t &ox, int16_t &oy, int16_t &oz) const;

private:
    static constexpr uint8_t  I2C_ADDR        = 0x0D;
    static constexpr uint8_t  CHIP_ID_VAL     = 0xFF;
    static constexpr uint16_t DRDY_TIMEOUT_MS = 10;

    static constexpr uint8_t REG_X_LSB    = 0x00;
    static constexpr uint8_t REG_STATUS   = 0x06;
    static constexpr uint8_t REG_CTRL1    = 0x09;
    static constexpr uint8_t REG_CTRL2    = 0x0A;
    static constexpr uint8_t REG_SETPERIOD = 0x0B;
    static constexpr uint8_t REG_CHIP_ID  = 0x0D;

    static constexpr uint8_t CTRL1_INIT     = 0x1D;  // continuous, 200Hz, 8G, OSR512
    static constexpr uint8_t CTRL2_INIT     = 0x40;  // ROL_PTR
    static constexpr uint8_t SETPERIOD_INIT = 0x01;
    static constexpr uint8_t STATUS_DRDY    = 0x01;

    bool    _ready = false;
    int16_t _calX = 0;
    int16_t _calY = 0;
    int16_t _calZ = 0;

    bool    writeReg(uint8_t reg, uint8_t val);
    bool    readReg(uint8_t reg, uint8_t &val);
    bool    burstRead(uint8_t startReg, uint8_t *buf, uint8_t len);
};
