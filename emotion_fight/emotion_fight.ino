#include <Arduino.h>
#include <FastLED.h>
#include <EEPROM.h>
#include <avr/pgmspace.h>  // 使用 PROGMEM

// ======================= 配置部分 =======================

// LED配置
#define MAIN_NUM_LEDS     24      // 前24个LED：血条
#define MID_NUM_LEDS      22      // 中间22个LED：恒亮白色
#define BG_NUM_LEDS       72      // 后72个LED：背景（火焰）
#define TOTAL_NUM_LEDS    (MAIN_NUM_LEDS + MID_NUM_LEDS + BG_NUM_LEDS) // 118

#define DATA_PIN          9       // LED数据引脚
#define COLOR_ORDER       GRB
#define CHIPSET           WS2812
#define LED_BRIGHTNESS    150     // LED亮度

CRGB leds[TOTAL_NUM_LEDS];

// 背景LED索引范围
#define BG_START_INDEX    (MAIN_NUM_LEDS + MID_NUM_LEDS)  // 46
#define BG_END_INDEX      (TOTAL_NUM_LEDS)                // 118

// PROGMEM示例：将一些字符串/调色板定义存入闪存
PROGMEM const char debugModeOnMsg[]  = "调试模式开启：输出实时传感器压力数据。";
PROGMEM const char debugModeOffMsg[] = "调试模式关闭：仅在掉血时输出传感器数据。";
PROGMEM const char invalidCmdMsg[]   = "无效指令。输入 '1' 开启调试模式，或 '0' 关闭。";

// 火焰调色板(示例) - 如果需要更多复杂自定义，可再细化
const TProgmemPalette16 FireColors_p FL_PROGMEM =
{  
    CRGB::Black,
    0x330000,
    0x660000,
    0x990000,
    0xCC3300,
    0xFF6600,
    0xFF9900,
    0xFFCC00,
    0xFFFF33,
    0xFFFF66,
    0xFFFF99,
    0xFFFFCC,
    CRGB::White,
    CRGB::White,
    CRGB::Gray,
    CRGB::Black
};

// =============== 传感器 & 角色 配置 ===============
#define HIT1_PIN  A0  // 人物1的受击传感器
#define HIT2_PIN  A1  // 人物2的受击传感器
#define CRIT1_PIN A2  // 人物1暴击传感器 (20g~10000g)
#define CRIT2_PIN A3  // 人物2暴击传感器 (20g~10000g)

// 血量与索引
const int MAX_HEALTH = 4;  // 4格血

// 受击冷却
const unsigned long HIT_COOLDOWN = 1000;

// 稳定期
const unsigned long STABLE_PERIOD = 2000;
bool isStable = false;
unsigned long gameStartTime = 0;

// 游戏结束
bool gameOver = false;
unsigned long gameOverResetTime = 0;
const unsigned long GAME_OVER_DELAY = 1000; 

// LED闪烁
bool isFlashing = false;
unsigned long ledFlashStartTime = 0;
const unsigned long FLASH_DURATION = 100;

// 调试模式
bool debugMode = false; // '1' => 开启, '0' => 关闭

// 暴击阈值
const long HIT_THRESHOLDS_NORMAL = 6000;  // <6000 => 普通
const long HIT_THRESHOLDS_CRIT   = 10000; // 6000~9999 => 暴击, ==10000 => 大暴击

// 角色结构
struct Character {
  int hitPin;       // 受击传感器
  int critPin;      // 暴击传感器
  int health;       // 健康(4格)
  bool hitDetected; 
  unsigned long lastHitTime;
};

Character characters[2] = {
  {HIT1_PIN, CRIT1_PIN, MAX_HEALTH, false, 0},
  {HIT2_PIN, CRIT2_PIN, MAX_HEALTH, false, 0}
};

// ================ 函数声明 ================
void initializeSystem();
bool isHit(int pin);
long getCritPressure(int pin);
void updateLEDDisplay();
void handleHit(int characterIndex, unsigned long currentTime);
void applyDamage(int targetIndex, int damage);
void checkGameOver();
void resetGame();
void triggerLEDFlash();
void handleLEDFlash(unsigned long currentTime);
void handleSerialCommands();
void initializeBackgroundLEDs();
void flashBackgroundLEDs();
void updateBackgroundLEDs();

// ================ setup & loop ================
void setup() {
  initializeSystem();
}

void loop() {
  unsigned long currentTime = millis();

  // 串口命令
  handleSerialCommands();

  // 稳定期检查
  if (!isStable && (currentTime - gameStartTime) >= STABLE_PERIOD) {
    isStable = true;
    Serial.println("稳定期结束，游戏开始！");

    // 初始化角色状态
    for (int i = 0; i < 2; i++) {
      characters[i].hitDetected = isHit(characters[i].hitPin);
      characters[i].lastHitTime = currentTime;
    }
    // 初始化背景LED
    initializeBackgroundLEDs();
  }

  // 游戏结束
  if (gameOver) {
    if ((currentTime - gameOverResetTime) >= GAME_OVER_DELAY) {
      resetGame();
    }
  }
  // 游戏进行
  else if (isStable) {
    for (int i = 0; i < 2; i++) {
      handleHit(i, currentTime);
    }
  }
  // 稳定期内，仅输出传感器信息
  else {
    for (int i = 0; i < 2; i++) {
      bool hitSt = isHit(characters[i].hitPin);
      long critVal = getCritPressure(characters[i].critPin);
      if (debugMode) {
        Serial.print("[稳定期] 人物");
        Serial.print(i + 1);
        Serial.print(": 受击=");
        Serial.print(hitSt ? "true" : "false");
        Serial.print(", 暴击值=");
        Serial.println(critVal);
      }
    }
  }

  // LED闪烁
  handleLEDFlash(currentTime);

  // 更新LED显示
  FastLED.show();

  // 更新背景LED动画
  updateBackgroundLEDs();

  // 非阻塞延时
  delay(50);
}

// ================ 函数实现 ================

// 系统初始化
void initializeSystem() {
  Serial.begin(9600);
  Serial.println(F("游戏初始化完成，等待稳定..."));

  // 初始化LED
  FastLED.addLeds<CHIPSET, DATA_PIN, COLOR_ORDER>(leds, TOTAL_NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(LED_BRIGHTNESS);

  // 中间22个LED => 白色
  for (int i = MAIN_NUM_LEDS; i < MAIN_NUM_LEDS + MID_NUM_LEDS; i++) {
    leds[i] = CRGB::White;
  }

  // 其余背景LED => 黑
  for (int i = MAIN_NUM_LEDS + MID_NUM_LEDS; i < TOTAL_NUM_LEDS; i++) {
    leds[i] = CRGB::Black;
  }
  FastLED.show();

  // 血条初始
  updateLEDDisplay();

  gameStartTime = millis();
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
}

// 受击传感器 => 布尔值
bool isHit(int pin) {
  // 简易平均
  const int SAMPLES = 5;
  long total = 0;
  for (int i = 0; i < SAMPLES; i++) {
    total += analogRead(pin);
    delay(10);
  }
  long avg = total / SAMPLES;

  // >某阈值即认为受击(5g~600g)
  // 这里可根据实际测试进行映射 / 调整
  bool hit = (avg > 500); 

  if (debugMode) {
    Serial.print("[受击传感器] 引脚: ");
    Serial.print(pin);
    Serial.print(", ADC平均: ");
    Serial.print(avg);
    Serial.print(", 受击: ");
    Serial.println(hit ? "true" : "false");
  }
  return hit;
}

// 暴击传感器 => [20g ~ 10000g], <6000 => 1格, 6000~9999 =>2格, ==10000 =>3格
long getCritPressure(int pin) {
  // 简易平均
  const int SAMPLES = 5;
  long total = 0;
  for (int i = 0; i < SAMPLES; i++) {
    total += analogRead(pin);
    delay(10);
  }
  long avg = total / SAMPLES;

  // 映射到[0..10000]
  long pressVal = map(avg, 0, 1023, 0, 10000);
  if (pressVal < 0) pressVal = 0;
  if (pressVal > 10000) pressVal = 10000;

  if (debugMode) {
    Serial.print("[暴击传感器] 引脚: ");
    Serial.print(pin);
    Serial.print(", ADC平均: ");
    Serial.print(avg);
    Serial.print(", 暴击值: ");
    Serial.println(pressVal);
  }
  return pressVal;
}

// 更新血条显示
// 前24个LED => 血条，每格血3个LED
// 人物1 => 索引0~11(从11->0扣)
// 人物2 => 索引12~23(从12->23扣)
void updateLEDDisplay() {
  // 先全部置绿
  for (int i = 0; i < MAIN_NUM_LEDS; i++) {
    leds[i] = CRGB::Green;
  }
  // 人物1
  int lost1 = MAX_HEALTH - characters[0].health; 
  for (int i = 0; i < lost1; i++) {
    for (int j = 0; j < 3; j++) {
      int idx = (MAX_HEALTH - 1 - i) * 3 + j; // 11->0
      if (idx >= 0 && idx < 12) {
        leds[idx] = CRGB::Red;
      }
    }
  }
  // 人物2
  int lost2 = MAX_HEALTH - characters[1].health;
  for (int i = 0; i < lost2; i++) {
    for (int j = 0; j < 3; j++) {
      int idx = 12 + i * 3 + j; // 12->23
      if (idx >= 12 && idx < 24) {
        leds[idx] = CRGB::Red;
      }
    }
  }

  // 中间22个LED => 白色
  for (int i = MAIN_NUM_LEDS; i < MAIN_NUM_LEDS + MID_NUM_LEDS; i++) {
    leds[i] = CRGB::White;
  }
  // 背景LED保持或更新至噪声效果
}

// 处理受击
void handleHit(int characterIndex, unsigned long currentTime) {
  Character &ch = characters[characterIndex];

  bool hitStatus = isHit(ch.hitPin);
  long critVal   = getCritPressure(ch.critPin);

  if ((currentTime - ch.lastHitTime) < HIT_COOLDOWN) {
    return;
  }

  if (hitStatus && !ch.hitDetected) {
    int damage = 0;
    String desc;

    if (critVal < HIT_THRESHOLDS_NORMAL) {
      damage = 1;
      desc = "普通攻击";
    } else if (critVal < HIT_THRESHOLDS_CRIT) {
      damage = 2;
      desc = "暴击";
    } else {
      damage = 3;
      desc = "大暴击";
    }

    if (debugMode) {
      Serial.print("人物");
      Serial.print(characterIndex + 1);
      Serial.print(" => 压力: ");
      Serial.print(critVal);
      Serial.print(", 伤害: ");
      Serial.println(damage);
    }

    if (damage > 0) {
      // 每次扣血时，先闪烁两次（白色快速闪），再变红
      // 执行“闪烁动画”后再applyDamage
      flickerDeductLEDs(characterIndex, damage);

      applyDamage(characterIndex, damage);
      // 闪烁主LED
      triggerLEDFlash();
      // 闪烁背景LED
      flashBackgroundLEDs();

      if (!debugMode) {
        Serial.print("人物");
        Serial.print(characterIndex + 1);
        Serial.print(" 受到了 ");
        Serial.print(desc);
        Serial.print("，扣除 ");
        Serial.print(damage);
        Serial.println(" 格血");
      }

      Serial.print("人物1剩余血量: ");
      Serial.println(characters[0].health);
      Serial.print("人物2剩余血量: ");
      Serial.println(characters[1].health);

      // 更新血条
      updateLEDDisplay();
      checkGameOver();
    }

    ch.hitDetected = true;
    ch.lastHitTime = currentTime;
  }

  if (!hitStatus && ch.hitDetected) {
    ch.hitDetected = false;
  }
}

// 扣血时闪烁指定角色、指定格数（白色快速闪2次），再变红
void flickerDeductLEDs(int charIndex, int damage) {
  // 人物1 => 索引0~11(从右往左)
  // 人物2 => 索引12~23(从左往右)
  // damage格 => each格3LED
  // 快速闪2次
  for (int flick = 0; flick < 2; flick++) {
    // 闪烁（白色）
    if (charIndex == 0) {
      int lost = MAX_HEALTH - characters[0].health;
      // 需要再扣 "damage" 格 => lastX
      // 右→左 => from (lost) up to (lost + damage -1)
      for (int i = 0; i < damage; i++) {
        int offset = lost + i; 
        for (int j = 0; j < 3; j++) {
          int idx = (MAX_HEALTH - 1 - offset) * 3 + j; 
          if (idx >= 0 && idx < 12) {
            leds[idx] = CRGB::White;
          }
        }
      }
    } else {
      int lost = MAX_HEALTH - characters[1].health;
      // 左→右 => from (lost) up to (lost + damage -1)
      for (int i = 0; i < damage; i++) {
        int offset = lost + i;
        for (int j = 0; j < 3; j++) {
          int idx = 12 + offset * 3 + j; 
          if (idx >= 12 && idx < 24) {
            leds[idx] = CRGB::White;
          }
        }
      }
    }
    FastLED.show();
    delay(80);

    // 恢复原血条
    updateLEDDisplay();
    FastLED.show();
    delay(80);
  }
}

// 扣血
void applyDamage(int targetIndex, int damage) {
  if (targetIndex < 0 || targetIndex >= 2) return;
  characters[targetIndex].health -= damage;
  if (characters[targetIndex].health < 0) {
    characters[targetIndex].health = 0;
  }
  if (debugMode) {
    Serial.print("人物");
    Serial.print(targetIndex + 1);
    Serial.print(" 扣血: ");
    Serial.print(damage);
    Serial.print(" => 剩余血量: ");
    Serial.println(characters[targetIndex].health);
  }
}

// 检查游戏是否结束
void checkGameOver() {
  if (characters[0].health == 0 || characters[1].health == 0) {
    Serial.println("游戏结束！重新开始...");
    gameOver = true;
    gameOverResetTime = millis();
  }
}

// 重置游戏
void resetGame() {
  for (int i = 0; i < 2; i++) {
    characters[i].health = MAX_HEALTH;
    characters[i].hitDetected = false;
    characters[i].lastHitTime = 0;
  }

  gameOver = false;
  isStable = false;
  gameStartTime = millis();

  updateLEDDisplay();
  initializeBackgroundLEDs();

  Serial.println("游戏重新开始，等待稳定...");
}

// 触发主LED闪烁
void triggerLEDFlash() {
  isFlashing = true;
  ledFlashStartTime = millis();
  digitalWrite(LED_BUILTIN, HIGH);
}

// 主LED闪烁
void handleLEDFlash(unsigned long currentTime) {
  if (isFlashing) {
    if ((currentTime - ledFlashStartTime) >= FLASH_DURATION) {
      digitalWrite(LED_BUILTIN, LOW);
      isFlashing = false;
    }
  }
}

// 串口命令
void handleSerialCommands() {
  if (Serial.available() > 0) {
    char cmd = Serial.read();
    if (cmd == '1') {
      debugMode = true;
      Serial.println((__FlashStringHelper*)debugModeOnMsg);
    }
    else if (cmd == '0') {
      debugMode = false;
      Serial.println((__FlashStringHelper*)debugModeOffMsg);
    }
    else {
      Serial.println((__FlashStringHelper*)invalidCmdMsg);
    }
  }
}

// 初始化背景LED (简单 Noise+火焰调色板)
void initializeBackgroundLEDs() {
  static uint8_t noiseSeed = 0;
  for (int i = BG_START_INDEX; i < BG_END_INDEX; i++) {
    uint8_t noise = inoise8((i - BG_START_INDEX) * 10, noiseSeed);
    // 采用火焰调色板
    CRGB color = ColorFromPalette(FireColors_p, noise, 255, LINEARBLEND);
    leds[i] = color;
  }
  noiseSeed++;
  FastLED.show();
}

// 背景LED闪烁（阻塞，闪烁两次）
void flashBackgroundLEDs() {
  // 记录当前颜色
  CRGB backup[TOTAL_NUM_LEDS];
  for (int i = BG_START_INDEX; i < BG_END_INDEX; i++) {
    backup[i] = leds[i];
  }

  for(int blink = 0; blink < 2; blink++) {
    // 闪烁橙红
    for (int i = BG_START_INDEX; i < BG_END_INDEX; i++) {
      leds[i] = CRGB::Red;
    }
    FastLED.show();
    delay(80);

    // 恢复
    for (int i = BG_START_INDEX; i < BG_END_INDEX; i++) {
      leds[i] = backup[i];
    }
    FastLED.show();
    delay(80);
  }
}

// 背景LED 动态更新 (Noise + 火焰调色板)
void updateBackgroundLEDs() {
  static uint8_t noiseSeed = 0;
  for (int i = BG_START_INDEX; i < BG_END_INDEX; i++) {
    uint8_t noise = inoise8((i - BG_START_INDEX) * 10, noiseSeed);
    CRGB color = ColorFromPalette(FireColors_p, noise, 255, LINEARBLEND);
    leds[i] = color;
  }
  noiseSeed++;
}
