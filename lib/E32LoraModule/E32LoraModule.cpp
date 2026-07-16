#include "E32LoraModule.h"
#include "Logger.h"

static constexpr char TAG[] = "E32LoraModule";

E32LoraModule::E32LoraModule(const Config& config) : m_config{config}
{}

void E32LoraModule::init()
{
    pinMode(m_config.pinM0,  OUTPUT);
    pinMode(m_config.pinM1,  OUTPUT);
    pinMode(m_config.pinAux, INPUT_PULLUP);

    m_config.serial.begin(m_config.baudRate, SERIAL_8N1, m_config.pinRx, m_config.pinTx);

    setMode(Mode::Normal);
    ESP_LOGI(TAG, "E32 initialised — Normal mode, baud: %lu", (unsigned long)m_config.baudRate);
}

void E32LoraModule::setMode(Mode mode)
{
    switch (mode)
    {
        case Mode::Normal:
            digitalWrite(m_config.pinM0, LOW);
            digitalWrite(m_config.pinM1, LOW);
            break;
        case Mode::WakeUp:
            digitalWrite(m_config.pinM0, HIGH);
            digitalWrite(m_config.pinM1, LOW);
            break;
        case Mode::PowerSaving:
            digitalWrite(m_config.pinM0, LOW);
            digitalWrite(m_config.pinM1, HIGH);
            break;
        case Mode::Config:
            digitalWrite(m_config.pinM0, HIGH);
            digitalWrite(m_config.pinM1, HIGH);
            break;
    }
    delay(POST_MODE_DELAY_MS);
    waitForAux();
}

bool E32LoraModule::available()
{
    return m_config.serial.available() > 0;
}

size_t E32LoraModule::read(uint8_t* buf, size_t maxLen)
{
    size_t count{0};
    while (m_config.serial.available() > 0 && count < maxLen)
    {
        buf[count++] = static_cast<uint8_t>(m_config.serial.read());
    }
    return count;
}

bool E32LoraModule::send(const uint8_t* data, size_t len)
{
    waitForAux(); // Ensure module is idle before transmitting

    size_t written = m_config.serial.write(data, len);
    if (written != len)
    {
        ESP_LOGE(TAG, "send() wrote %u of %u bytes", (unsigned)written, (unsigned)len);
        return false;
    }
    m_config.serial.flush(); // Wait until all bytes have left the TX buffer
    waitForAux();             // Wait for the module to finish over-the-air transmission

    ESP_LOGD(TAG, "Sent %u byte(s)", (unsigned)len);
    return true;
}

void E32LoraModule::program(const uint8_t* cfg, size_t len)
{
    setMode(Mode::Config);

    ESP_LOGI(TAG, "Writing %u config byte(s) to E32", (unsigned)len);
    for (size_t i = 0; i < len; ++i)
    {
        m_config.serial.write(cfg[i]);
        ESP_LOGD(TAG, "  cfg[%u] = 0x%02X", (unsigned)i, cfg[i]);
    }
    delay(100);

    // Drain any echo / response bytes
    while (m_config.serial.available() > 0)
        m_config.serial.read();

    setMode(Mode::Normal);
    ESP_LOGI(TAG, "E32 programming complete");
}


/******************
* Private methods *
*******************/

void E32LoraModule::waitForAux()
{
    const unsigned long deadline{millis() + AUX_TIMEOUT_MS};
    while (digitalRead(m_config.pinAux) == LOW && millis() < deadline)
        delay(1);

    if (millis() >= deadline)
        ESP_LOGW(TAG, "AUX timeout — module may be busy");

    delay(POST_MODE_DELAY_MS);
}
