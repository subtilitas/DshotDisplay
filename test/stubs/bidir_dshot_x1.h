// Host-test stub. Mirrors the signatures of the real header so the
// firmware can be compiled and exercised on a PC. Not used on device.
// verbatim public API from bastian2001/pico-bidir-dshot src/bidir_dshot_x1.h
#ifndef BIDIR_DSHOT_X1_H
#define BIDIR_DSHOT_X1_H
#include "hardware/pio.h"
#include <vector>
using std::vector;
enum class BidirDshotTelemetryType : uint8_t {
	ERPM, OTHER_VALUE, CHECKSUM_ERROR, NO_PACKET, VOLTAGE, CURRENT,
	TEMPERATURE, STATUS, STRESS, DEBUG_FRAME_1, DEBUG_FRAME_2,
};
#define ESC_STATUS_MAX_STRESS_MASK 0b00001111
#define ESC_STATUS_ERROR_MASK 0b00100000
#define ESC_STATUS_WARNING_MASK 0b01000000
#define ESC_STATUS_ALERT_MASK 0b10000000
class BidirDShotX1 {
public:
	static vector<BidirDShotX1 *> instances;
	BidirDShotX1() = delete;
	BidirDShotX1(uint8_t pin, uint32_t speed = 600, PIO pio = pio0, int8_t sm = -1);
	~BidirDShotX1();
	void sendThrottle(uint16_t throttle);
	void sendRaw11Bit(uint16_t data);
	void sendRaw12Bit(uint16_t data);
	bool checkTelemetryAvailable();
	BidirDshotTelemetryType getTelemetryErpm(uint32_t *erpm);
	BidirDshotTelemetryType getTelemetryPacket(uint32_t *value);
	BidirDshotTelemetryType getTelemetryRaw(uint32_t *value);
	static uint32_t convertFromRaw(uint32_t raw, BidirDshotTelemetryType type);
	bool initError() { return iError; }
private:
	PIO pio; uint8_t pin; uint8_t sm; uint32_t speed; uint8_t offset;
	bool iError = false;
	static uint16_t appendChecksum(uint16_t data);
};
#endif
