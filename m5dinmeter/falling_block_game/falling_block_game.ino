#include "M5DinMeter.h"
#include "unit_byte.hpp"
#include <Adafruit_NeoPixel.h>

#define PIN_NEOPIXEL    2   // GPIO number which assigned to NeoPixel DIN
#define PIN_PORT_A_SCL  15  // GPIO number which assigned to PortA SCL
#define PIN_PORT_A_SDA  13  // GPIO number which assigned to PortA SDA
#define NUMPIXELS_X     8
#define NUMPIXELS_Y     32
uint8_t switchId = 0x46;

// Game speed for each Level
uint64_t gameTimerInterval[] = {400000, 300000, 300000, 250000, 250000,
                                250000, 200000, 200000, 200000, 200000,
                                150000, 150000, 150000, 150000, 150000,
                                125000, 125000, 125000, 125000, 125000,
                                100000, 100000, 100000, 100000, 100000,
                                100000, 100000, 100000, 100000, 100000,
                                90000};
int levelMax = (sizeof(gameTimerInterval) / sizeof(gameTimerInterval[0])) - 1;

enum GameStatus {
    GAME_INIT = 0,
    GAME_RUNNING,
    GAME_NEXT,
    GAME_OVER,
};

UnitByte device;
hw_timer_t *timer = NULL;
uint16_t conversionMatrix[NUMPIXELS_X][NUMPIXELS_Y];
int gameStatus;
int currentLine = 0;
int currentLevel = 0;
int oldEncPos;
String displayText;
uint8_t block_bits;
uint8_t current_switch_bits;
bool displayTextUpdate = false;
bool beepOn01 = false;
bool beepOn02 = false;
bool beepOn03 = false;

Adafruit_NeoPixel pixels(NUMPIXELS_X * NUMPIXELS_Y, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

void setup() {
    // Setup M5 Din Meter
    auto cfg = M5.config();
    DinMeter.begin(cfg, true);

    // Setup Display and Buzzer
    DinMeter.Display.setRotation(1);
    DinMeter.Display.setTextColor(GREEN);
    DinMeter.Display.setTextDatum(middle_center);
    DinMeter.Display.setFont(&fonts::Orbitron_Light_32);
    DinMeter.Display.setTextSize(2);
    DinMeter.Speaker.setVolume(255);

    // Reset encoder position
    DinMeter.Encoder.readAndReset();

    // Setup NeoPixel
    pixels.begin();
    pixels.clear();
    neoPixelMatrixInit();

    // Setup ByteSwitch Unit
    device.begin(&Wire1, switchId, PIN_PORT_A_SDA, PIN_PORT_A_SCL, 400000);

    Serial.begin(9600);

    // Setup timer
    timer = timerBegin(1*1000*1000); // 1MHz
    timerAttachInterrupt(timer, &timerRoutine);

    // Initialize game
    gameInit();
    gameStatus = GAME_INIT;
}

void loop() {
    DinMeter.update();

    if (gameStatus == GAME_INIT) {
        // Start Game if button is pushed
        if (DinMeter.BtnA.wasPressed()) {
            beep01();
            DinMeter.Encoder.readAndReset();
            currentLevel = 0;
            gameStart();
            oldEncPos = 0;
            gameStatus = GAME_RUNNING;
        }
    }
    else if (gameStatus == GAME_RUNNING) {
    }
    else if (gameStatus == GAME_NEXT) {
        timerStop(timer);
        gameNewBlock();
        gameStart();
        gameStatus = GAME_RUNNING;
    }
    else {
        // Game Over
        if (DinMeter.BtnA.wasPressed()) {
            beep01();
            gameInit();
            gameStatus = GAME_INIT;
        }
    }

    if (gameStatus != GAME_OVER) {
        pixels.clear();
        current_switch_bits = byteSwitchRead();
        if (currentLine == NUMPIXELS_Y - 1) {
            gameBlockJudgeDraw(current_switch_bits);
        }
        else {
            gameBlockDraw(current_switch_bits);
        }
        pixels.show();
    }

    gameDisplayRefresh();

    if (beepOn01 == true) {
        beep01();
        beepOn01 = false;
    }
    if (beepOn02 == true) {
        beep02();
        beepOn02 = false;
    }
    if (beepOn03 == true) {
        beep03();
        beepOn03 = false;
    }
}


//
// Timer routine
//
void timerRoutine() {
    if (currentLine >= NUMPIXELS_Y - 1) {
        timerStop(timer);
        if (gameClear()) {
            currentLevel++;
            gameStatus = GAME_NEXT;
            beepOn02 = true;
        }
        else {
            gameStatus = GAME_OVER;
            beepOn03 = true;
            // Change Display color
            DinMeter.Display.setTextColor(RED);
            displayTextUpdate = true;
        }
    }
    else {
        beepOn01 = true;
        currentLine++;
    }
}


//
// Game functions
//
void gameInit() {
    gameNewBlock();
    DinMeter.Display.setTextColor(GREEN);
    displayText = "Start";
    displayTextUpdate = true;
}

void gameStart() {
    gameLevelDisplay();

    // Start timer
    if (currentLevel <= levelMax) {
        timerAlarm(timer, (uint64_t)gameTimerInterval[currentLevel], true, 0);
    }
    else 
    {
        timerAlarm(timer, (uint64_t)gameTimerInterval[levelMax], true, 0);
    }
    timerStart(timer);
}

void gameNewBlock() {
    // Set block
    currentLine = 0;
    block_bits = (uint8_t)random(1, 256);
}

void gameBlockDraw(uint8_t switch_bits) {
    for (uint8_t i = 0; i < NUMPIXELS_X; i++) {
        if (switch_bits & (1 << i)) {
            neoPixelMatrixSet(i, NUMPIXELS_Y - 1, pixels.Color(0, 10, 0));
            byteSwitchLed(i, 0x00FF00);
        }
        else {
            byteSwitchLed(i, 0x000000);
        }
    }
    for (uint8_t i = 0; i < NUMPIXELS_X; i++) {
        if (block_bits & (1 << i)) {
            neoPixelMatrixSet(i, currentLine, pixels.Color(0, 0, 10));
        }
    }
}

void gameBlockJudgeDraw(uint8_t switch_bits) {
    for (uint8_t i = 0; i < NUMPIXELS_X; i++) {
        uint8_t switch_bit = switch_bits & (1 << i);
        uint8_t block_bit = block_bits & (1 << i);

        // both block and switch are 1
        if (switch_bit & block_bit) {
            neoPixelMatrixSet(i, NUMPIXELS_Y - 1, pixels.Color(0, 10, 10));
            byteSwitchLed(i, 0x00FFFF);
        }
        // both block and switch are 0
        else if (!(switch_bit | block_bit)) {
            byteSwitchLed(i, 0x000000);
        }
        else {
            neoPixelMatrixSet(i, NUMPIXELS_Y - 1, pixels.Color(10, 0, 0));
            byteSwitchLed(i, 0xFF0000);
        }
    }
}

bool gameClear() {
    if (current_switch_bits == block_bits) {
        return true;
    }
    else {
        return false;
    }
}

void gameLevelDisplay() {
    displayText = "Lv." + String(currentLevel + 1);
    displayTextUpdate = true;
}

void gameDisplayRefresh() {
    if(displayTextUpdate) {
        DinMeter.Display.clear();
        DinMeter.Display.drawString(displayText,
                                    DinMeter.Display.width() / 2,
                                    DinMeter.Display.height() / 2);
        displayTextUpdate = false;
    }
}

//
// NeoPixel matrix functions
//
void neoPixelMatrixInit() {
    // Setup coordinate conversion matrix
    uint16_t n = 0;
    for (int y = 0; y < NUMPIXELS_Y; y++) {
        if (y % 2 == 0) {
            for (int x = NUMPIXELS_X - 1; x >= 0; x--) {
                conversionMatrix[x][y] = n++;
            }
        }
        else {
            for (int x = 0; x < NUMPIXELS_X; x++) {
                conversionMatrix[x][y] = n++;
            }
        }
    }
}

void neoPixelMatrixSet(int x, int y, uint32_t c) {
    pixels.setPixelColor(conversionMatrix[x][y], c);
}


//
// Byte Switch functions
//
uint8_t byteSwitchRead() {
    uint8_t value = device.getSwitchStatus();
    uint8_t retval = 0;
    // bit swap
    for (uint8_t i = 0; i < 8; i++) {
        if (value & (0x80 >> i)) {
            retval += (1 << i);
        }
    }
    return retval;
}

void byteSwitchLed(uint8_t bit, uint32_t color) {
    device.setRGB888(7 - bit, color);
}


//
// Others
//
void beep01() {
    DinMeter.Speaker.tone(3300, 20);
}

void beep02() {
    DinMeter.Speaker.tone(4000, 20);
}

void beep03() {
    DinMeter.Speaker.tone(2300, 100);
    delay(200);
    DinMeter.Speaker.tone(2300, 500);
}
