// Captain's Compass — QMC5883L magnetometer driver on Wire1.
// See docs/tdd-issue-002-captains-compass.md §10.

#include "Magnetometer.h"

#include <Arduino.h>
#include <Wire.h>
#include <math.h>

Magnetometer::InitResult Magnetometer::begin() {
    Wire1.begin();
    Wire1.beginTransmission(I2C_ADDR);
    if (Wire1.endTransmission() != 0) return NOT_FOUND;

    uint8_t chipId = 0;
    if (!readReg(REG_CHIP_ID, chipId)) return NOT_FOUND;
    if (chipId != CHIP_ID_VAL) return INIT_FAILED;

    if (!writeReg(REG_SETPERIOD, SETPERIOD_INIT)) return INIT_FAILED;
    if (!writeReg(REG_CTRL1, CTRL1_INIT)) return INIT_FAILED;
    if (!writeReg(REG_CTRL2, CTRL2_INIT)) return INIT_FAILED;

    _ready = true;
    return OK;
}

bool Magnetometer::read(int16_t &x, int16_t &y, int16_t &z) {
    if (!_ready) return false;

    const uint32_t deadline = millis() + DRDY_TIMEOUT_MS;
    uint8_t status = 0;
    while (millis() < deadline) {
        if (!readReg(REG_STATUS, status)) return false;
        if (status & STATUS_DRDY) break;
    }
    if (!(status & STATUS_DRDY)) return false;

    uint8_t buf[6];
    if (!burstRead(REG_X_LSB, buf, sizeof(buf))) return false;

    x = static_cast<int16_t>((buf[1] << 8) | buf[0]);
    y = static_cast<int16_t>((buf[3] << 8) | buf[2]);
    z = static_cast<int16_t>((buf[5] << 8) | buf[4]);
    return true;
}

float Magnetometer::heading() {
    int16_t rx = 0, ry = 0, rz = 0;
    if (!read(rx, ry, rz)) return 0.0f;
    const float cx = static_cast<float>(rx - _calX);
    const float cy = static_cast<float>(ry - _calY);
    float h = atan2f(cy, cx) * 180.0f / static_cast<float>(M_PI);
    if (h < 0.0f) h += 360.0f;
    return h;
}

void Magnetometer::setCalibration(int16_t ox, int16_t oy, int16_t oz) {
    _calX = ox;
    _calY = oy;
    _calZ = oz;
}

void Magnetometer::getCalibration(int16_t &ox, int16_t &oy, int16_t &oz) const {
    ox = _calX;
    oy = _calY;
    oz = _calZ;
}

bool Magnetometer::writeReg(uint8_t reg, uint8_t val) {
    Wire1.beginTransmission(I2C_ADDR);
    Wire1.write(reg);
    Wire1.write(val);
    return Wire1.endTransmission() == 0;
}

bool Magnetometer::readReg(uint8_t reg, uint8_t &val) {
    Wire1.beginTransmission(I2C_ADDR);
    Wire1.write(reg);
    if (Wire1.endTransmission(false) != 0) return false;  // repeated start
    if (Wire1.requestFrom(I2C_ADDR, (uint8_t)1) != 1) return false;
    val = Wire1.read();
    return true;
}

bool Magnetometer::burstRead(uint8_t startReg, uint8_t *buf, uint8_t len) {
    Wire1.beginTransmission(I2C_ADDR);
    Wire1.write(startReg);
    if (Wire1.endTransmission(false) != 0) return false;
    if (Wire1.requestFrom(I2C_ADDR, len) != len) return false;
    for (uint8_t i = 0; i < len; ++i) buf[i] = Wire1.read();
    return true;
}
