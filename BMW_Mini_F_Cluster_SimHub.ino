// BMW/Mini F-series cluster driver for SimHub telemetry
// Arduino UNO + Seeed CAN-BUS Shield V2 + coryjfowler mcp_can library
//
// CAN message definitions based on the CarCluster project by Andrej Rolih
// https://github.com/r00li/CarCluster - licensed under GPL-3.0, as is this file.
// See README.md for wiring and SimHub configuration.

#include <SPI.h>
#include <mcp_can.h>

#define IS_CAR_MINI false        // true = Mini F5x cluster, false = BMW F-series
#define SPI_CS_PIN 9             // Seeed shield V2 = D9, V1.2 = D10
#define SERIAL_BAUD 115200
#define DATA_TIMEOUT_MS 1500     // fall back to idle when telemetry stops
#define BACKLIGHT 100            // cluster illumination 0-100%
#define DRIVE_MODE 2             // 1=Traction 2=Comfort 4=Sport 5=Sport+ 6=DSC off 7=Eco pro
#define SHIFT_LIGHT_ON_DSC false // flash DSC triangle as redline shift light

#if IS_CAR_MINI
  #define MAX_RPM_CLUSTER 7000
#else
  #define MAX_RPM_CLUSTER 6000
#endif
#define MAX_SPEED_CLUSTER 260

#define lo8(x) (uint8_t)((x) & 0xFF)
#define hi8(x) (uint8_t)(((x) >> 8) & 0xFF)

MCP_CAN CAN(SPI_CS_PIN);

// BMW uses SAE J1850 CRC8 (poly 0x1D, init 0xFF) with a message-specific final XOR
static uint8_t crcTable[256];

void crcBegin() {
  for (int dividend = 0; dividend < 256; ++dividend) {
    uint8_t remainder = dividend;
    for (uint8_t bit = 8; bit > 0; --bit) {
      remainder = (remainder & 0x80) ? (uint8_t)((remainder << 1) ^ 0x1D) : (uint8_t)(remainder << 1);
    }
    crcTable[dividend] = remainder;
  }
}

uint8_t getCrc8(const uint8_t message[], int nBytes, uint8_t finalXor) {
  uint8_t remainder = 0xFF;
  for (int b = 0; b < nBytes; ++b) remainder = crcTable[message[b] ^ remainder];
  return remainder ^ finalXor;
}

void sendWithCrc(unsigned long id, const uint8_t* payload, uint8_t len, uint8_t finalXor) {
  uint8_t buf[8];
  buf[0] = getCrc8(payload, len, finalXor);
  for (uint8_t i = 0; i < len; i++) buf[i + 1] = payload[i];
  CAN.sendMsgBuf(id, 0, len + 1, buf);
}

uint8_t counter4Bit = 0;
uint8_t accCounter = 0;
uint8_t count = 0;
uint16_t distanceTravelledCounter = 0;
unsigned long lastNeedleUpdate = 0;
unsigned long lastFastUpdate = 0;
unsigned long lastSlowUpdate = 0;

// telemetry state
int gSpeed = 0;
int gRpm = 0;
int gGear = 13;                 // 1-9 = M1-M9, 10 = P, 11 = R, 12 = N, 13 = D
int gCoolant = 90;
int gFuel = 100;
bool gLeftBlinker = false;
bool gRightBlinker = false;
bool gHandbrake = false;
bool gPausedOrStopped = false;
bool gTraction = false;
bool gHighBeam = false;
bool gOilWarn = false;
bool gShiftLight = false;
unsigned long lastDataTime = 0;
unsigned long messagesReceived = 0;

// needle values eased toward targets, sent at 50 Hz
float smoothSpeed = 0;
float smoothRpm = 800;

#define MSG_BUF_LEN 330
char msgBuf[MSG_BUF_LEN];
uint8_t msgPos = 0;

long findJsonInt(const char* msg, const char* key, long fallback) {
  char pattern[12];
  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  const char* p = strstr(msg, pattern);
  if (!p) return fallback;
  p += strlen(pattern);
  while (*p == ':' || *p == ' ' || *p == '"') p++;
  return atol(p);
}

char findJsonChar(const char* msg, const char* key, char fallback) {
  char pattern[12];
  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  const char* p = strstr(msg, pattern);
  if (!p) return fallback;
  p += strlen(pattern);
  while (*p == ':' || *p == ' ' || *p == '"') p++;
  return *p;
}

int mapGearCharToLocal(char c) {
  switch (c) {
    case '1' ... '9': return c - '0';
    case 'P': return 10;
    case 'R': return 11;
    case 'N': return 12;
    case 'D': return 13;
    default:  return 13;
  }
}

void processMessage(const char* msg) {
  if (findJsonInt(msg, "action", -1) != 10) return;

  long spe = findJsonInt(msg, "spe", 0);
  long rpm = findJsonInt(msg, "rpm", 0);
  long oit = findJsonInt(msg, "oit", 90);
  long wtr = findJsonInt(msg, "wtr", 0);
  long fue = findJsonInt(msg, "fue", 100);

  if (spe < 0) spe = -spe;
  gSpeed = (spe > MAX_SPEED_CLUSTER) ? MAX_SPEED_CLUSTER : (int)spe;
  gRpm = (rpm > MAX_RPM_CLUSTER) ? MAX_RPM_CLUSTER : (int)rpm;
  gGear = mapGearCharToLocal(findJsonChar(msg, "gea", 'D'));
  gCoolant = constrain((int)(wtr > 10 ? wtr : oit), 50, 150);
  gFuel = constrain((int)fue, 0, 100);
  gLeftBlinker  = findJsonInt(msg, "lft", 0) != 0;
  gRightBlinker = findJsonInt(msg, "rit", 0) != 0;
  gHandbrake    = findJsonInt(msg, "hnb", 0) != 0;
  gTraction     = findJsonInt(msg, "tra", 0) != 0;

  // ShowLights comes in as flag names ("DL_FULLBEAM, DL_SIGNAL_L, ...").
  // DL_TC means "system present" in BeamNG, not intervention - not used for the lamp.
  gHighBeam   = strstr(msg, "DL_FULLBEAM") != NULL;
  gShiftLight = strstr(msg, "DL_SHIFT")    != NULL;
  gOilWarn    = (strstr(msg, "DL_OILWARN") != NULL) || (strstr(msg, "DL_BATTERY") != NULL);
  if (strstr(msg, "DL_HANDBRAKE")) gHandbrake = true;

  gPausedOrStopped = (findJsonInt(msg, "pau", 0) != 0) || (findJsonInt(msg, "run", 1) == 0);

  lastDataTime = millis();
  messagesReceived++;
}

void readSerial() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c != '\n' && msgPos < MSG_BUF_LEN - 1) {
      msgBuf[msgPos++] = c;
    } else {
      msgBuf[msgPos] = '\0';
      msgPos = 0;
      if (msgBuf[0] != '\0') processMessage(msgBuf);
    }
  }
}

void sendIgnitionStatus(bool ignition) {
  uint8_t d[] = { (uint8_t)(0x80 | counter4Bit), (uint8_t)(ignition ? 0x8A : 0x08), 0xDD, 0xF1, 0x01, 0x30, 0x06 };
  sendWithCrc(0x12F, d, 7, 0x44);
}

void sendSpeed(float s) {
  uint16_t calc = (uint16_t)(s * 64.01);
  uint8_t d[] = { (uint8_t)(0xC0 | counter4Bit), lo8(calc), hi8(calc), (uint8_t)(s < 0.5 ? 0x81 : 0x91) };
  sendWithCrc(0x1A1, d, 4, 0xA9);
}

void sendRPM(int rpm, int gear) {
  uint8_t calculatedGear = 0;
  if (gear >= 1 && gear <= 9)      calculatedGear = gear + 4;
  else if (gear == 11)             calculatedGear = 2;
  else if (gear == 12)             calculatedGear = 1;
  uint8_t rpmValue = map(rpm, 0, 6900, 0x00, 0x2B);
  uint8_t d[] = { (uint8_t)(0x60 | counter4Bit), rpmValue, 0xC0, 0xF0, calculatedGear, 0xFF, 0xFF };
  sendWithCrc(0x0F3, d, 7, 0x7A);
}

void sendAutomaticTransmission(int gear) {
  uint8_t sel = 0;
  if (gear >= 1 && gear <= 9)  sel = 0x81;
  else if (gear == 10)         sel = 0x20;
  else if (gear == 11)         sel = 0x40;
  else if (gear == 12)         sel = 0x60;
  else if (gear == 13)         sel = 0x80;
  uint8_t d[] = { counter4Bit, sel, 0xFC, 0xFF };
  sendWithCrc(0x3FD, d, 4, 0xD6);
}

void sendBasicDriveInfo(int engineTemperature) {
  { uint8_t d[] = { (uint8_t)(0xF0 | counter4Bit), 0xFE, 0xFF, 0x14 };            // ABS ok
    sendWithCrc(0x36E, d, 4, 0xD8); }
  { uint8_t d[] = { count, 0xFF };                                                 // alive counter
    CAN.sendMsgBuf(0x0D7, 0, 2, d); }
  { uint8_t d[] = { (uint8_t)(0xF0 | counter4Bit), 0xFE, 0xFF, 0x14 };            // power steering ok
    sendWithCrc(0x2A7, d, 4, 0x9E); }
  { uint8_t d[] = { (uint8_t)(0xF0 | counter4Bit), 0xE0, 0xE0, 0xE1, 0x00, 0xEC, 0x01 };  // cruise control
    sendWithCrc(0x289, d, 7, 0x82); }
  { uint8_t d[] = { (uint8_t)(0x40 | counter4Bit), 0x40, 0x55, 0xFD, 0xFF, 0xFF, 0xFF };  // airbag ok
    sendWithCrc(0x19B, d, 7, 0xFF); }
  { uint8_t d[] = { (uint8_t)(0xE0 | counter4Bit), 0xF1, 0xF0, 0xF2, 0xF2, 0xFE };        // seatbelt ok
    sendWithCrc(0x297, d, 6, 0x28); }
  { uint8_t d[] = { (uint8_t)(0xF0 | counter4Bit), 0xA2, 0xA0, 0xA0 };            // TPMS ok
    sendWithCrc(0x369, d, 4, 0xC5); }
  { uint8_t d[] = { (uint8_t)(0x10 | counter4Bit), 0x82, 0x4E, 0x7E, (uint8_t)(engineTemperature + 50), 0x05, 0x89 };
    sendWithCrc(0x3F9, d, 7, 0xF1); }                                              // oil status
  { uint8_t d[] = { 0x3E, (uint8_t)engineTemperature, 0x64, 0x64, 0x64, 0x01, 0xF1 };
    sendWithCrc(0x2C4, d, 7, 0xB2); }                                              // engine temperature
}

void sendParkBrake(bool active) {
  uint8_t d[] = { (uint8_t)(0xF0 | counter4Bit), 0x38, 0x00, (uint8_t)(active ? 0x15 : 0x14) };
  sendWithCrc(0x36F, d, 4, 0x17);
}

void sendFuel(int fuelPercent) {
#if IS_CAR_MINI
  const uint8_t out0 = 22, out50 = 7, out100 = 3;
#else
  const uint8_t out0 = 37, out50 = 18, out100 = 4;
#endif
  uint8_t fuelRaw = (fuelPercent <= 50) ? map(fuelPercent, 0, 50, out0, out50)
                                        : map(fuelPercent, 50, 100, out50, out100);
#if IS_CAR_MINI
  uint8_t d[] = { 0x00, 0x00, hi8(fuelRaw), lo8(fuelRaw), 0x00 };
#else
  uint8_t d[] = { hi8(fuelRaw), lo8(fuelRaw), hi8(fuelRaw), lo8(fuelRaw), 0x00 };
#endif
  CAN.sendMsgBuf(0x349, 0, 5, d);
}

void sendDistanceTravelled(int s) {
  { uint8_t d[] = { count, 0xFF, 0x64, 0x64, 0x64, 0x01, 0xF1 };
    sendWithCrc(0x2C4, d, 7, 0xC6); }
  { uint8_t d[] = { (uint8_t)(0xF0 | counter4Bit), lo8(distanceTravelledCounter), hi8(distanceTravelledCounter), 0xF2 };
    sendWithCrc(0x2BB, d, 4, 0xDE); }
  distanceTravelledCounter += (uint16_t)(s * 2.9);
}

void sendBlinkers(bool left, bool right) {
  uint8_t st = (!left && !right) ? 0x80 : (uint8_t)(0x81 | (left << 4) | (right << 5));
  uint8_t d[] = { st, 0xF0 };
  CAN.sendMsgBuf(0x1F6, 0, 2, d);
}

void sendLights(bool mainLights, bool highBeam, bool rearFog, bool frontFog) {
  uint8_t st = (highBeam << 1) | (mainLights << 2) | (frontFog << 5) | (rearFog << 6);
  uint8_t d[] = { st, 0xC0, 0xF7 };
  CAN.sendMsgBuf(0x21A, 0, 3, d);
}

void sendBacklightBrightness(uint8_t pct) {
  uint8_t d[] = { (uint8_t)map(pct, 0, 100, 0, 253), 0xFF };
  CAN.sendMsgBuf(0x202, 0, 2, d);
}

// Check-control messages on 0x5C0: 15=door open, 34=check engine, 215=DSC, 71=handbrake red (Mini)
void sendAlerts(bool doorOpen, bool dsc, bool handbrakeRed, bool checkEngine) {
  { uint8_t d[] = { 0x40, 0x0F, 0x00, (uint8_t)(doorOpen ? 0x29 : 0x28), 0xFF, 0xFF, 0xFF, 0xFF };
    CAN.sendMsgBuf(0x5C0, 0, 8, d); }
  { uint8_t d[] = { 0x40, 215, 0x00, (uint8_t)(dsc ? 0x29 : 0x28), 0xFF, 0xFF, 0xFF, 0xFF };
    CAN.sendMsgBuf(0x5C0, 0, 8, d); }
  { uint8_t d[] = { 0x40, 34, 0x00, (uint8_t)(checkEngine ? 0x29 : 0x28), 0xFF, 0xFF, 0xFF, 0xFF };
    CAN.sendMsgBuf(0x5C0, 0, 8, d); }
#if IS_CAR_MINI
  { uint8_t d[] = { 0x40, 71, 0x00, (uint8_t)(handbrakeRed ? 0x29 : 0x28), 0xFF, 0xFF, 0xFF, 0xFF };
    CAN.sendMsgBuf(0x5C0, 0, 8, d); }
#else
  (void)handbrakeRed;
#endif
}

void sendDriveMode(uint8_t mode) {
  uint8_t d[] = { (uint8_t)(0xF0 | counter4Bit), 0x00, 0x00, mode, 0x11, 0xC0 };
  sendWithCrc(0x3A7, d, 6, 0x4A);
}

void sendAcc() {
  uint8_t d[] = { (uint8_t)(0xF0 | accCounter), 0x5C, 0x70, 0x00, 0x00 };
  sendWithCrc(0x33B, d, 5, 0x6B);
  accCounter += 4;
  if (accCounter > 0x0E) accCounter -= 0x0F;
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  pinMode(SPI_CS_PIN, OUTPUT);
  crcBegin();

  while (CAN_OK != CAN.begin(MCP_ANY, CAN_500KBPS, MCP_16MHZ)) {
    delay(500);
  }
  CAN.setMode(MCP_NORMAL);
}

void loop() {
  readSerial();

  unsigned long now = millis();

  bool noData = (lastDataTime == 0) || (now - lastDataTime > DATA_TIMEOUT_MS);
  float targetSpeed = noData ? 0 : (float)gSpeed;
  float targetRpm   = noData ? 800 : (float)gRpm;

  // needles at 50 Hz with easing for smooth movement
  if (now - lastNeedleUpdate >= 20) {
    lastNeedleUpdate = now;

    smoothSpeed += (targetSpeed - smoothSpeed) * 0.22;
    smoothRpm   += (targetRpm   - smoothRpm)   * 0.22;
    if (smoothSpeed < 0.3 && targetSpeed == 0) smoothSpeed = 0;

    sendSpeed(smoothSpeed);
    sendRPM((int)smoothRpm, gGear);

    counter4Bit++;
    if (counter4Bit >= 14) counter4Bit = 0;
  }

  if (now - lastFastUpdate >= 100) {
    lastFastUpdate = now;

    sendIgnitionStatus(true);
    sendBasicDriveInfo(gCoolant);
    sendAutomaticTransmission(gGear);
    sendFuel(gFuel);
    sendParkBrake(gHandbrake);
    sendDistanceTravelled((int)smoothSpeed);
    sendAlerts(gPausedOrStopped && !noData, gTraction || (SHIFT_LIGHT_ON_DSC && gShiftLight), gHandbrake, gOilWarn);
    sendAcc();

    count++;
    if (count >= 254) count = 0;
  }

  if (now - lastSlowUpdate >= 500) {
    lastSlowUpdate = now;
    sendLights(true, gHighBeam, false, false);
    sendBlinkers(gLeftBlinker, gRightBlinker);
    sendBacklightBrightness(BACKLIGHT);
    sendDriveMode(DRIVE_MODE);

    Serial.print(F("STATUS msgs="));
    Serial.print(messagesReceived);
    Serial.print(F(" spe="));
    Serial.print((int)smoothSpeed);
    Serial.print(F(" rpm="));
    Serial.print((int)smoothRpm);
    Serial.println(noData ? F(" (no data)") : F(" (live)"));
  }
}
