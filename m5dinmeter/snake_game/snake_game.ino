#include "M5DinMeter.h"
#include <Adafruit_NeoPixel.h>

#define PIN             2   // GPIO number which assigned to NeoPixel DIN
#define NUMPIXELS_X     32
#define NUMPIXELS_Y     8

#define SNAKE_INIT_LEN  3
#define SNAKE_INIT_Y    (NUMPIXELS_Y / 2)
#define SNAKE_LEN_MAX   (NUMPIXELS_X * NUMPIXELS_Y)
#define ITEM_INIT_X     27

uint64_t gameTimerInterval[] = {500000, 250000, 175000, 100000, 50000};    // Game speed for each Level
int levelMax = (sizeof(gameTimerInterval) / sizeof(gameTimerInterval[0])) - 1;

enum SnakeWay {
    RIGHT = 0,
    DOWN,
    LEFT,
    UP,
};

enum GameStatus {
    GAME_INIT = 0,
    GAME_RUNNING,
    GAME_OVER,
};

typedef struct {
    bool enable;
    int x;
    int y;
} snakeBodyInfo;

typedef struct {
    int snakeWay;
    int bodyLength;
    int score;
    int level;
    uint32_t headColor;
    uint32_t bodyColor;
    uint32_t itemColor;
    snakeBodyInfo bodyPixel[SNAKE_LEN_MAX];
    snakeBodyInfo itemPixel;
    bool displayTextUpdate;
    String displayText;
} snakeInfo;

hw_timer_t *timer = NULL;
int gameStatus;
int oldEncPos;
bool encEnable = true;
snakeInfo sInfo;
uint16_t  conversionMatrix[NUMPIXELS_X][NUMPIXELS_Y];

Adafruit_NeoPixel pixels(NUMPIXELS_X * NUMPIXELS_Y, PIN, NEO_GRB + NEO_KHZ800);


void setup() {
    // Setup NeoPixel
    pixels.begin();
    pixels.clear();
    neoPixelMatrixInit();

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

    Serial.begin(9600);

    // Setup timer
    //timer = timerBegin(0, 80, true);
    timer = timerBegin(1*1000*1000); // 1MHz
    timerAttachInterrupt(timer, &timerRoutine);

    // Initialize snake game
    gameStatus = GAME_INIT;
    sInfo.level = 1;
    snakeInit();
}

void loop() {
    DinMeter.update();
    int newEncPos = DinMeter.Encoder.read();

    if (gameStatus == GAME_INIT) {
        // Start Game if button is pushed
        if (DinMeter.BtnA.wasPressed()) {
            beep01();
            DinMeter.Encoder.readAndReset();
            snakeGameStart();
            oldEncPos = 0;
            gameStatus = GAME_RUNNING;
        }
        // Set game level
        else if(newEncPos < oldEncPos - 1) {
            snakeLevelUp();
            oldEncPos = newEncPos;
        }
        else if(newEncPos > oldEncPos + 1) {
            snakeLevelDown();
            oldEncPos = newEncPos;
        }
    }
    else if (gameStatus == GAME_RUNNING) {
        // Change snake way
        if((newEncPos < oldEncPos - 1) && encEnable) {
            snakeTurnRight();
            //Serial.println(newEncPos);
            oldEncPos = newEncPos;
            encEnable = false;
        }
        else if((newEncPos > oldEncPos + 1) && encEnable) {
            snakeTurnLeft();
            //Serial.println(newEncPos);
            oldEncPos = newEncPos;
            encEnable = false;
        }
    }
    else {
        // Wait to push button
        if (DinMeter.BtnA.wasPressed()) {
            beep01();
            DinMeter.Encoder.readAndReset();
            snakeInit();
            oldEncPos = 0;
            gameStatus = GAME_INIT;
        }
    }

    pixels.clear();
    snakeDraw();
    pixels.show();
    snakeDisplayRefresh();
}


//
// Timer routine
//
void timerRoutine() {
    if (gameStatus == GAME_RUNNING) {
        // If head is hit to wall or itself, game over.
        if(snakeHit() == true) {
            snakeDead();
            gameStatus = GAME_OVER;
        }
        else {
            snakeItemCheck();
            snakeMove();
        }
    }
    encEnable = true;
}


//
// Snake functions
//
void snakeInit() {
    sInfo.snakeWay = RIGHT;
    sInfo.bodyLength = SNAKE_INIT_LEN;
    snakeScoreReset();

    // Set snake and item color
    sInfo.headColor = pixels.Color(0, 15, 5);
    sInfo.bodyColor = pixels.Color(0, 5, 0);
    sInfo.itemColor = pixels.Color(15, 10, 0);

    // Set display color
    DinMeter.Display.setTextColor(GREEN);

    for(int i = 0; i < SNAKE_LEN_MAX; i++) {
        sInfo.bodyPixel[i].enable = false;
    }
    // Set initial snake position.
    for(int i = 0; i < SNAKE_INIT_LEN; i++) {
        sInfo.bodyPixel[i].enable = true;
        sInfo.bodyPixel[i].x = SNAKE_INIT_LEN - i;
        sInfo.bodyPixel[i].y = SNAKE_INIT_Y;
    }

    // Set initial item position.
    sInfo.itemPixel.enable = true;
    sInfo.itemPixel.x = ITEM_INIT_X;
    sInfo.itemPixel.y = SNAKE_INIT_Y;

    // Display Game Level
    snakeLevelDisplay();
}

void snakeGameStart() {
    randomSeedReset();

    // Start timer
    timerAlarm(timer, gameTimerInterval[sInfo.level], true, 0);
    timerStart(timer);

    // Start display score
    snakeScoreDisplay();
}


void snakeTurnRight() {
    sInfo.snakeWay++;
    if(sInfo.snakeWay > UP) {
        sInfo.snakeWay = RIGHT;
    }
}

void snakeTurnLeft() {
    sInfo.snakeWay--;
    if(sInfo.snakeWay < RIGHT) {
        sInfo.snakeWay = UP;
    }
}

void snakeMove() {
    for(int i = SNAKE_LEN_MAX - 1; i > 0; i--) {
        sInfo.bodyPixel[i].x = sInfo.bodyPixel[i-1].x;
        sInfo.bodyPixel[i].y = sInfo.bodyPixel[i-1].y;
    }
    snakeHeadMove(&sInfo.bodyPixel[0], sInfo.snakeWay);
}

bool snakeHit() {
    snakeBodyInfo pixel;

    pixel.x = sInfo.bodyPixel[0].x;
    pixel.y = sInfo.bodyPixel[0].y;
    snakeHeadMove(&pixel, sInfo.snakeWay);

    // Check wall hit
    if(pixel.x < 0 || pixel.x >= NUMPIXELS_X) {
        return true;
    }
    else if(pixel.y < 0 || pixel.y >= NUMPIXELS_Y) {
        return true;
    }

    // Check hit myself
    for(int i = 0; i < SNAKE_LEN_MAX; i++) {
        // i+1: Do not check body tail because tail will be moved.
        if(sInfo.bodyPixel[i+1].enable == false) {
            break;
        }
        else if(pixel.x == sInfo.bodyPixel[i].x && pixel.y == sInfo.bodyPixel[i].y) {
            return true;
        }
    }

    return false;
}

void snakeItemCheck() {
    snakeBodyInfo pixel;

    pixel.x = sInfo.bodyPixel[0].x;
    pixel.y = sInfo.bodyPixel[0].y;
    snakeHeadMove(&pixel, sInfo.snakeWay);

    if(pixel.x == sInfo.itemPixel.x && pixel.y == sInfo.itemPixel.y) {
        snakeScoreAdd(1);
        snakeExtend();
        snakeItemPut();
    }
}

void snakeDead() {
    // Stop Timer
    timerStop(timer);

    // Change snake body color as dead
    sInfo.headColor = pixels.Color(15, 0, 5);
    sInfo.bodyColor = pixels.Color(5, 0, 0);

    // Change Display color
    DinMeter.Display.setTextColor(RED);
    sInfo.displayTextUpdate = true;
}

void snakeDraw() {
    // Draw snake
    for(int i = 0; i < SNAKE_LEN_MAX; i++) {
        if(sInfo.bodyPixel[i].enable == true) {
            // Set head and body
            if(i == 0) {
                neoPixelMatrixSet(sInfo.bodyPixel[i].x, sInfo.bodyPixel[i].y, sInfo.headColor);
            }
            else {
                neoPixelMatrixSet(sInfo.bodyPixel[i].x, sInfo.bodyPixel[i].y, sInfo.bodyColor);
            }
        }
    }

    // Draw item
    if(sInfo.itemPixel.enable == true) {
        neoPixelMatrixSet(sInfo.itemPixel.x, sInfo.itemPixel.y, sInfo.itemColor);
    }
}

void snakeDisplayRefresh() {
    if(sInfo.displayTextUpdate) {
        DinMeter.Display.clear();
        DinMeter.Display.drawString(sInfo.displayText,
                                    DinMeter.Display.width() / 2,
                                    DinMeter.Display.height() / 2);
        sInfo.displayTextUpdate = false;
    }
}


void snakeExtend() {
    for(int i = 0; i < SNAKE_LEN_MAX; i++) {
        if(sInfo.bodyPixel[i].enable == false) {
            sInfo.bodyPixel[i].enable = true;
            break;
        }
    }
}

void snakeItemPut() {
    bool next = true;
    int x, y;
    // Set new item
    do {
        x = random(0, NUMPIXELS_X);
        y = random(0, NUMPIXELS_Y);
        // Check new item is not overlap snake body
        for(int i = 0; i < SNAKE_LEN_MAX; i++) {
            if(sInfo.bodyPixel[i].enable == false) {
                next = false;
                break;
            }
            else if(sInfo.bodyPixel[i].x == x && sInfo.bodyPixel[i].y == y) {
                break;
            }
        }
    } while(next);

    sInfo.itemPixel.x = x;
    sInfo.itemPixel.y = y;
}

void snakeHeadMove(snakeBodyInfo *pixel, int way) {
    switch(way) {
        case RIGHT:
            pixel->x += 1;
            break;
        case LEFT:
            pixel->x -= 1;
            break;
        case UP:
            pixel->y -= 1;
            break;
        case DOWN:
            pixel->y += 1;
            break;
        default:
            break;
    }
}

void snakeScoreAdd(int value) {
    sInfo.score += value;
    snakeScoreDisplay();
}

void snakeScoreReset() {
    sInfo.score = 0;
}

void snakeScoreDisplay() {
    sInfo.displayText = String(sInfo.score);
    sInfo.displayTextUpdate = true;
}

void snakeLevelUp() {
    sInfo.level++;
    if(sInfo.level > levelMax) {
        sInfo.level = levelMax;
    }
    else {
        beep01();
        snakeLevelDisplay();
    }
}

void snakeLevelDown() {
    sInfo.level--;
    if(sInfo.level < 0) {
        sInfo.level = 0;
    }
    else {
        beep01();
        snakeLevelDisplay();
    }
}

void snakeLevelDisplay() {
    sInfo.displayText = "Lv." + String(sInfo.level);
    sInfo.displayTextUpdate = true;
}


//
// NeoPixel matrix functions
//
void neoPixelMatrixInit() {
    // Setup coordinate conversion matrix
    // End of NeoPixel is point of origin (0, 0)
    uint16_t n = NUMPIXELS_X * NUMPIXELS_Y - 1;
    for(int x = 0; x < NUMPIXELS_X; x++) {
        if (x % 2 == 0) {
            for(int y = NUMPIXELS_Y - 1; y >= 0; y--) {
                conversionMatrix[x][y] = n--;
            }
        }
        else {
            for(int y = 0; y < NUMPIXELS_Y; y++) {
                conversionMatrix[x][y] = n--;
            }
        }
    }
}

void neoPixelMatrixSet(int x, int y, uint32_t c) {
    pixels.setPixelColor(conversionMatrix[x][y], c);
}


//
// Others
//
void randomSeedReset() {
    unsigned long seed = micros();
    String text = "Seed: " + String(seed);
    Serial.println(text);
    randomSeed(seed);
}

void beep01() {
    DinMeter.Speaker.tone(3300, 20);
}
