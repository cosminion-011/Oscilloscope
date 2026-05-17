#include <LovyanGFX.hpp>

#define ADC_PIN          25
#define BTN1_PIN         13
#define BTN2_PIN         12
#define BTN3_PIN         14
#define SCREEN_PWR_PIN   35

#define CORRECTION_VOLTAGE  1.3526f
#define V_REF               3.3f

#define SAMPLES     280
#define MEDIAN_WIN  3

#define GRAT_X      26
#define GRAT_Y      28
#define GRAT_W      290
#define GRAT_H      184
#define NUM_H_DIV   10
#define NUM_V_DIV   8
#define DIV_W       (GRAT_W / NUM_H_DIV)
#define DIV_H       (GRAT_H / NUM_V_DIV)

#define C_BG           TFT_BLACK
#define C_BEZEL        0x2104
#define C_GRAT_MAJOR   0x3186
#define C_GRAT_MINOR   0x18C2
#define C_CROSS        0x4A69
#define C_TRACE_CORE   0x07FF
#define C_TRACE_BRIGHT 0xA7FF
#define C_TEXT_MAIN    TFT_WHITE
#define C_TEXT_DIM     0x7BEF
#define C_TEXT_WARN    0xFDA0

#define DEBOUNCE_MS 30

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 _panel_instance;
  lgfx::Bus_SPI       _bus_instance;
public:
  LGFX(void) {
    {
      auto cfg = _bus_instance.config();
      cfg.spi_host   = VSPI_HOST;
      cfg.freq_write = 40000000;
      cfg.pin_sclk   = 18;
      cfg.pin_mosi   = 23;
      cfg.pin_miso   = 19;
      cfg.pin_dc     = 21;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }
    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs      = 4;
      cfg.pin_rst     = 2;
      cfg.panel_width  = 240;
      cfg.panel_height = 320;
      _panel_instance.config(cfg);
    }
    setPanel(&_panel_instance);
  }
};

LGFX        display;
LGFX_Sprite waveSprite(&display);
LGFX_Sprite topBarSprite(&display);

float        samples[SAMPLES];
unsigned long sampleTimes[SAMPLES];
int           sampleIndex    = 0;
bool          bufferFull     = false;
unsigned long lastSampleTime = 0;
unsigned long sampleIntervalMs = 0;
int           xCoords[SAMPLES];

bool          isFrozen      = false;
float         frozenSamples[SAMPLES];
unsigned long frozenTimes[SAMPLES];
int           frozenCount   = 0;
unsigned long freezeTime    = 0;

float rawHistory[MEDIAN_WIN];
int   histIndex = 0;
int   histCount = 0;

struct Button {
  uint8_t     pin;
  bool           lastState, pressed, released;
  unsigned long lastDebounce;
};
Button buttons[3];

float vMaxPresets[]       = {5.0f, 8.0f, 12.0f, 24.0f, 33.0f};
int   vDivIndex           = 0;

float totalTimePresets[]  = {0.5f, 1.0f, 5.0f, 10.0f, 15.0f, 30.0f, 60.0f};
const int NUM_T_PRESETS   = 7;
int   tDivIndex           = 1;

float lastVNow     = -1;
float lastVAvg     = -1;
float lastVMax     = -1;
bool  lastRunState = true;
float lastTotalTime = -1;

float median3(float a, float b, float c) {
  return max(min(a, b), min(max(a, b), c));
}

int valToY(float v) {
  float norm = constrain(v / vMaxPresets[vDivIndex], 0.0f, 1.0f);
  return (GRAT_H - 5) - (int)(norm * (GRAT_H - 10));
}

float readVoltage() {
  return analogRead(ADC_PIN) * (V_REF / 4095.0f) * 10.0f * CORRECTION_VOLTAGE;
}

float applyMedianFilter(float raw) {
  rawHistory[histIndex] = raw;
  histIndex = (histIndex + 1) % MEDIAN_WIN;
  if (histCount < MEDIAN_WIN) histCount++;
  if (histCount < MEDIAN_WIN) return raw;
  return median3(rawHistory[0], rawHistory[1], rawHistory[2]);
}

void recalcInterval() {
  sampleIntervalMs = (unsigned long)((totalTimePresets[tDivIndex] * 1000.0f) / SAMPLES);
  if (sampleIntervalMs < 1) sampleIntervalMs = 1;
}

void checkScreenPower() {
  static bool lastPwr = true;
  bool pwr = digitalRead(SCREEN_PWR_PIN);

  Serial.printf("G35 = %d\n", pwr);

  if (!lastPwr && pwr) {
    delay(300);
    ESP.restart();
  }
  lastPwr = pwr;
}

void initButtons() {
  buttons[0] = {BTN1_PIN, true, false, false, 0};
  buttons[1] = {BTN2_PIN, true, false, false, 0};
  buttons[2] = {BTN3_PIN, true, false, false, 0};
  for (int i = 0; i < 3; i++) pinMode(buttons[i].pin, INPUT_PULLUP);
}

void updateButtons() {
  unsigned long now = millis();
  for (int i = 0; i < 3; i++) {
    Button &b = buttons[i];
    bool r = digitalRead(b.pin);
    if (r != b.lastState) b.lastDebounce = now;
    if ((now - b.lastDebounce) > DEBOUNCE_MS) {
      if (r == LOW  && !b.pressed)  { b.pressed = true; }
      if (r == HIGH &&  b.pressed)  { b.released = true; b.pressed = false; }
    }
    b.lastState = r;
  }
}

void clearButtonFlags() {
  for (int i = 0; i < 3; i++) buttons[i].released = false;
}

void drawGraticule() {
  for (int i = 0; i <= NUM_H_DIV * 5; i++) {
    int x = i * DIV_W / 5;
    if (x >= GRAT_W) continue;
    for (int j = 0; j <= NUM_V_DIV * 5; j++) {
      int y = j * DIV_H / 5;
      if (y >= GRAT_H) continue;
      if (i % 5 == 0 || j % 5 == 0) continue;
      waveSprite.drawPixel(x, y, C_GRAT_MINOR);
    }
  }
  for (int i = 0; i <= NUM_H_DIV; i++) {
    int x = min(i * DIV_W, GRAT_W - 1);
    waveSprite.drawFastVLine(x, 0, GRAT_H,
      (i == NUM_H_DIV / 2) ? C_CROSS : C_GRAT_MAJOR);
  }
  for (int i = 0; i <= NUM_V_DIV; i++) {
    int y = min(i * DIV_H, GRAT_H - 1);
    waveSprite.drawFastHLine(0, y, GRAT_W,
      (i == NUM_V_DIV / 2) ? C_CROSS : C_GRAT_MAJOR);
  }
}

void drawStaticUI() {
  display.fillScreen(C_BG);
  display.drawRect(GRAT_X - 2, GRAT_Y - 2, GRAT_W + 4, GRAT_H + 4, C_BEZEL);

  display.setTextFont(0);
  display.setTextSize(1);
  display.setTextColor(C_TEXT_DIM);
  for (int i = 0; i <= NUM_V_DIV; i += 2) {
    float vLabel = vMaxPresets[vDivIndex] - (i * (vMaxPresets[vDivIndex] / NUM_V_DIV));
    int   y      = GRAT_Y + min(i * DIV_H, GRAT_H - 1) - 4;
    char  s[8];
    snprintf(s, sizeof(s), "%.0fV", vLabel);
    int tw = strlen(s) * 6;
    display.setCursor(max((GRAT_X - 2 - tw) / 2, 0), y);
    display.print(s);
  }

  float tTotal = totalTimePresets[tDivIndex];
  int   ty     = GRAT_Y + GRAT_H + 6;
  display.setTextColor(C_TEXT_DIM);

  auto printTime = [&](float t, int x) {
    display.setCursor(x, ty);
    if (t >= 1.0f) {
      if (t == (int)t) display.printf("-%ds",   (int)t);
      else             display.printf("-%.1fs",  t);
    } else {
      int ms = (int)(t * 1000);
      if ((t * 1000) == ms) display.printf("-%dms",  ms);
      else                  display.printf("-%.0fms", t * 1000);
    }
  };

  printTime(tTotal,        GRAT_X + 2);
  printTime(tTotal / 2.0f, GRAT_X + GRAT_W / 2 - 14);
  display.setCursor(GRAT_X + GRAT_W - 22, ty);
  display.print("0s");
}

void drawWaveform() {
  unsigned long now     = millis();
  float          totalMs = totalTimePresets[tDivIndex] * 1000.0f;

  float         *drawSamples = isFrozen ? frozenSamples : samples;
  unsigned long *drawTimes   = isFrozen ? frozenTimes   : sampleTimes;
  int            drawCount   = isFrozen ? frozenCount   : (bufferFull ? SAMPLES : sampleIndex);
  unsigned long  refTime     = isFrozen ? freezeTime    : now;

  float          visSamples[SAMPLES];
  unsigned long visAges[SAMPLES];
  int            visIdx = 0;

  for (int i = 0; i < drawCount; i++) {
    int idx = (isFrozen || !bufferFull) ? i : (sampleIndex + i) % SAMPLES;
    unsigned long age = refTime - drawTimes[idx];
    if (age <= (unsigned long)totalMs) {
      visSamples[visIdx] = drawSamples[idx];
      visAges[visIdx]    = age;
      visIdx++;
    }
  }

  if (visIdx < 2) return;

  for (int i = 0; i < visIdx; i++)
    xCoords[i] = map(visAges[i], 0, (unsigned long)totalMs, GRAT_W - 1, 0);

  for (int i = 1; i < visIdx; i++) {
    int x0 = xCoords[i - 1], x1 = xCoords[i];
    int y0 = valToY(visSamples[i - 1]), y1 = valToY(visSamples[i]);

    if (x0 >= 0 && x1 >= 0 && x0 < GRAT_W && x1 < GRAT_W) {
      waveSprite.drawLine(x0, y0,     x1, y1,     C_TRACE_CORE);
      waveSprite.drawLine(x0, y0 - 1, x1, y1 - 1, C_TRACE_CORE);
      if (abs(y1 - y0) < 2) {
        waveSprite.drawPixel(x1, y1,     C_TRACE_BRIGHT);
        waveSprite.drawPixel(x1, y1 - 1, C_TRACE_BRIGHT);
      }
    }
  }
}

void drawTopBar() {
  topBarSprite.fillSprite(C_BG);
  topBarSprite.setTextFont(0);
  topBarSprite.setTextSize(1);

  topBarSprite.setTextColor(isFrozen ? C_TEXT_WARN : C_TRACE_CORE);
  topBarSprite.setCursor(2, 2);
  topBarSprite.print(isFrozen ? "HOLD" : "RUN ");

  topBarSprite.setTextColor(C_TEXT_MAIN);
  topBarSprite.setCursor(36, 2);
  float t = totalTimePresets[tDivIndex];
  if (t >= 1.0f) topBarSprite.printf("%ds",  (int)t);
  else           topBarSprite.printf("%dms", (int)(t * 1000));

  int   n    = bufferFull ? SAMPLES : max(sampleIndex, 1);
  float vNow = samples[(sampleIndex - 1 + SAMPLES) % SAMPLES];
  float vMax = 0, vSum = 0;
  for (int i = 0; i < n; i++) {
    if (samples[i] > vMax) vMax = samples[i];
    vSum += samples[i];
  }
  float vAvg = vSum / n;

  topBarSprite.setTextColor(C_TEXT_WARN);
  topBarSprite.setCursor(130, 2);
  topBarSprite.print("NOW ");
  topBarSprite.setTextColor(C_TEXT_MAIN);
  topBarSprite.printf("%05.2f", vNow);

  topBarSprite.setTextColor(C_TRACE_CORE);
  topBarSprite.setCursor(190, 2);
  topBarSprite.print("AVG ");
  topBarSprite.setTextColor(C_TEXT_MAIN);
  topBarSprite.printf("%05.2f", vAvg);

  topBarSprite.setTextColor(TFT_RED);
  topBarSprite.setCursor(250, 2);
  topBarSprite.print("MAX ");
  topBarSprite.setTextColor(C_TEXT_MAIN);
  topBarSprite.printf("%05.2f", vMax);

  topBarSprite.pushSprite(4, 4);
}

bool topBarChanged() {
  int   n    = bufferFull ? SAMPLES : max(sampleIndex, 1);
  float vNow = samples[(sampleIndex - 1 + SAMPLES) % SAMPLES];
  float vMax = 0, vSum = 0;
  for (int i = 0; i < n; i++) {
    if (samples[i] > vMax) vMax = samples[i];
    vSum += samples[i];
  }
  float vAvg = vSum / n;
  float t    = totalTimePresets[tDivIndex];

  return (isFrozen != !lastRunState     ||
          t        != lastTotalTime     ||
          fabsf(vNow - lastVNow) > 0.01f ||
          fabsf(vAvg - lastVAvg) > 0.01f ||
          fabsf(vMax - lastVMax) > 0.01f);
}

void saveTopBarState() {
  int   n    = bufferFull ? SAMPLES : max(sampleIndex, 1);
  float vMax = 0, vSum = 0;
  for (int i = 0; i < n; i++) {
    if (samples[i] > vMax) vMax = samples[i];
    vSum += samples[i];
  }
  lastVNow     = samples[(sampleIndex - 1 + SAMPLES) % SAMPLES];
  lastVMax     = vMax;
  lastVAvg     = vSum / n;
  lastRunState = !isFrozen;
  lastTotalTime = totalTimePresets[tDivIndex];
}

void handleButtons() {
  bool changed = false, fullRedraw = false;

  if (buttons[0].released) {
    vDivIndex = (vDivIndex + 1) % 5;
    changed = fullRedraw = true;
  }

  if (buttons[1].released) {
    tDivIndex = (tDivIndex + 1) % NUM_T_PRESETS;
    recalcInterval();
    changed = fullRedraw = true;
  }

  if (buttons[2].released) {
    if (!isFrozen) {
      isFrozen   = true;
      freezeTime = millis();
      int count  = bufferFull ? SAMPLES : sampleIndex;
      frozenCount = count;
      for (int i = 0; i < count; i++) {
        int idx         = bufferFull ? (sampleIndex + i) % SAMPLES : i;
        frozenSamples[i] = samples[idx];
        frozenTimes[i]   = sampleTimes[idx];
      }
    } else {
      isFrozen = false;
    }
    changed = true;
  }

  if (changed) {
    if (fullRedraw) drawStaticUI();
    drawTopBar();
    saveTopBarState();
  }
  clearButtonFlags();
}

void setup() {
  Serial.begin(115200);

  initButtons();
  pinMode(SCREEN_PWR_PIN, INPUT);

  analogSetAttenuation(ADC_11db);

  display.init();
  display.setRotation(3);

  waveSprite.setColorDepth(16);
  waveSprite.createSprite(GRAT_W, GRAT_H);

  topBarSprite.setColorDepth(16);
  topBarSprite.createSprite(312, 12);

  for (int i = 0; i < SAMPLES; i++) {
    samples[i]     = 0.0f;
    sampleTimes[i] = 0;
  }

  recalcInterval();
  drawStaticUI();
  drawTopBar();
  saveTopBarState();
}

void loop() {
  checkScreenPower();
  updateButtons();
  handleButtons();
  Serial.printf("raw=%d  v=%.2f\n", analogRead(ADC_PIN), readVoltage());
  unsigned long now = millis();

  if (!isFrozen && now - lastSampleTime >= sampleIntervalMs) {
    lastSampleTime      = now;
    float v             = constrain(applyMedianFilter(readVoltage()), 0.0f, vMaxPresets[vDivIndex]);
    samples[sampleIndex]     = v;
    sampleTimes[sampleIndex] = now;
    sampleIndex = (sampleIndex + 1) % SAMPLES;
    if (sampleIndex == 0) bufferFull = true;
  }

  waveSprite.fillScreen(C_BG);
  drawGraticule();
  drawWaveform();
  waveSprite.pushSprite(GRAT_X, GRAT_Y);

  if (topBarChanged()) {
    drawTopBar();
    saveTopBarState();
  }
}
