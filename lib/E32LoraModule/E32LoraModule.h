#pragma once

#include <Arduino.h>
#include <cstdint>
#include <cstddef>

/**
 * @class E32LoraModule
 * @brief Hardware abstraction for the EBYTE E32 series LoRa UART module.
 *
 * The E32 communicates over UART and exposes three control signals:
 *   - M0, M1 — select operating mode (see Mode enum)
 *   - AUX     — driven LOW by the module when busy; HIGH when ready
 *
 * Modes:
 *   Normal      M0=0 M1=0  Transparent/fixed-point UART↔LoRa forwarding
 *   WakeUp      M0=1 M1=0  Preamble + Normal mode (wakes power-saving nodes)
 *   PowerSaving M0=0 M1=1  Receive-only with duty cycle sleep (UART buffered)
 *   Config      M0=1 M1=1  AT-command configuration via Serial
 *
 * Typical usage:
 *   E32LoraModule lora{E32LoraModule::Config{...}};
 *   lora.init();
 *   // in loop:
 *   if (lora.available()) {
 *       uint8_t buf[64];
 *       size_t len = lora.read(buf, sizeof(buf));
 *       // handle buf[0..len-1]
 *   }
 *   lora.send(data, len);
 */
class E32LoraModule
{
public:
    enum class Mode
    {
        Normal,       ///< M0=0, M1=0 — standard send/receive
        WakeUp,       ///< M0=1, M1=0 — adds wake-up preamble before each transmission
        PowerSaving,  ///< M0=0, M1=1 — low-power listening
        Config        ///< M0=1, M1=1 — module accepts configuration commands
    };

    struct Config
    {
        uint8_t  pinM0;
        uint8_t  pinM1;
        uint8_t  pinAux;
        int8_t   pinRx;      ///< ESP32 GPIO connected to E32 TX
        int8_t   pinTx;      ///< ESP32 GPIO connected to E32 RX
        uint32_t baudRate;
        HardwareSerial& serial; ///< Which hardware serial port to use (Serial2 recommended)
    };

    explicit E32LoraModule(const Config& config);

    /**
     * @brief Configures GPIO directions and starts the UART.
     *        Leaves the module in Normal mode.
     *        Call once from setup().
     */
    void init();

    /**
     * @brief Switch to the given operating mode.
     *        Waits for AUX to go HIGH before returning.
     */
    void setMode(Mode mode);

    /**
     * @brief Returns true if one or more bytes are waiting in the UART RX buffer.
     */
    bool available();

    /**
     * @brief Reads up to maxLen bytes into buf.
     * @return Number of bytes actually read.
     */
    size_t read(uint8_t* buf, size_t maxLen);

    /**
     * @brief Sends len bytes from data over LoRa.
     *        Waits for AUX to confirm the module has finished transmitting.
     * @return true if all bytes were written to the UART TX buffer.
     */
    bool send(const uint8_t* data, size_t len);

    /**
     * @brief Applies E32 configuration bytes (enters Config mode, writes, returns to Normal).
     *
     * Use 0xC2 as the first byte for RAM-only (temporary) configuration — safe to call
     * on every boot without wearing out the module's non-volatile flash.
     * Use 0xC0 only if you intentionally want to persist the settings permanently.
     *
     * @param cfg  Pointer to 6-byte configuration array (see E32 datasheet).
     *             Byte layout: [head(0xC0/0xC2), addH, addL, sped, chan, option]
     */
    void program(const uint8_t* cfg, size_t len);

private:
    /**
     * @brief Blocks until AUX goes HIGH (module ready), with a safety guard timeout.
     */
    void waitForAux();

    Config m_config;

    static constexpr unsigned long AUX_TIMEOUT_MS {3000}; ///< Max wait for AUX HIGH
    static constexpr unsigned long POST_MODE_DELAY_MS {2};  ///< E32 datasheet recommended settle
};
