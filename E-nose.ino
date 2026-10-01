#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>

// using standard 16x2 i2c
LiquidCrystal_I2C lcd(0x27, 16, 2);

// pins
#define MQ135 A0
#define MQ2 A1
#define BTN 7
#define BUZZ 8

int mq135Val = 0;
int mq2Val = 0;
int base135 = 120; // default clean values
int base2 = 120;

int gasScore = 0;
int freshness = 100;
int peak = 0;
int lowest = 1023;

int page = 0; // 0 to 4
int lastBtn = HIGH;
unsigned long btnTimer = 0;
bool held = false;

unsigned long sensorTimer = 0;
unsigned long screenTimer = 0;
unsigned long buzzTimer = 0;
bool beepState = false;

void setup() {
  Serial.begin(9600);
  pinMode(BTN, INPUT_PULLUP);
  pinMode(BUZZ, OUTPUT);
  digitalWrite(BUZZ, LOW);

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Sensor Warmup...");

  // check eeprom for saved baselines
  if (EEPROM.read(0) == 123) {
    EEPROM.get(1, base135);
    EEPROM.get(3, base2);
    Serial.println("EEPROM loaded ok");
  } else {
    Serial.println("No EEPROM data found");
  }

  // quick countdown (30s is too long for quick reboots)
  for (int i = 10; i > 0; i--) {
    lcd.setCursor(0, 1);
    lcd.print("Wait ");
    lcd.print(i);
    lcd.print("s   ");
    delay(1000);
  }
  lcd.clear();
}

void loop() {
  unsigned long ms = millis();

  // --- READ SENSORS EVERY 500ms ---
  if (ms - sensorTimer >= 500) {
    sensorTimer = ms;

    // take a few samples to smooth out jitter
    long sum1 = 0;
    long sum2 = 0;
    for (int i = 0; i < 5; i++) {
      sum1 += analogRead(MQ135);
      sum2 += analogRead(MQ2);
      delay(2);
    }
    mq135Val = sum1 / 5;
    mq2Val = sum2 / 5;

    // wiring check
    bool err = false;
    if (mq135Val < 5 || mq135Val > 1015 || mq2Val < 5 || mq2Val > 1015) {
      err = true;
    }

    if (!err) {
      // simple percentage above baseline
      float diff1 = (float)(mq135Val - base135) / (base135 * 1.5);
      float diff2 = (float)(mq2Val - base2) / (base2 * 1.5);
      if (diff1 < 0) diff1 = 0;
      if (diff2 < 0) diff2 = 0;

      int dev1 = diff1 * 100;
      int dev2 = diff2 * 100;

      // weight the higher one slightly more
      int maxD = max(dev1, dev2);
      int avgD = (dev1 + dev2) / 2;
      gasScore = (avgD + maxD) / 2;
      if (gasScore > 100) gasScore = 100;

      freshness = 100 - gasScore;
      if (freshness < 0) freshness = 0;

      // tracker
      int curMax = max(mq135Val, mq2Val);
      if (curMax > peak) peak = curMax;
      if (curMax < lowest) lowest = curMax;
    } else {
      gasScore = 0;
      freshness = 0;
    }

    // serial debug
    Serial.print("MQ135: ");
    Serial.print(mq135Val);
    Serial.print(" | MQ2: ");
    Serial.print(mq2Val);
    Serial.print(" | Gas: ");
    Serial.println(gasScore);
  }

  // --- BUTTON LOGIC ---
  int b = digitalRead(BTN);
  if (b == LOW && lastBtn == HIGH) {
    btnTimer = ms;
    held = false;
  }
  if (b == LOW && !held && (ms - btnTimer > 2000)) {
    // long press: run calibration
    held = true;
    lcd.clear();
    lcd.print("Calibrating...");
    lcd.setCursor(0, 1);
    lcd.print("Stay in air!");

    long cal1 = 0;
    long cal2 = 0;
    for (int i = 0; i < 20; i++) {
      cal1 += analogRead(MQ135);
      cal2 += analogRead(MQ2);
      delay(100);
    }
    base135 = cal1 / 20;
    base2 = cal2 / 20;

    // save to eeprom
    EEPROM.write(0, 123); // flag byte
    EEPROM.put(1, base135);
    EEPROM.put(3, base2);

    lcd.clear();
    lcd.print("Saved!");
    delay(1000);
    lcd.clear();
  }
  if (b == HIGH && lastBtn == LOW) {
    // short click: next page
    if (!held && (ms - btnTimer > 50)) {
      page++;
      if (page > 4) page = 0;
      lcd.clear();
    }
  }
  lastBtn = b;

  // --- BUZZER ---
  if (gasScore >= 65) { // danger
    if (ms - buzzTimer > 200) {
      buzzTimer = ms;
      beepState = !beepState;
      if (beepState) tone(BUZZ, 1500);
      else noTone(BUZZ);
    }
  } else if (gasScore >= 30) { // warn
    if (ms - buzzTimer > 600) {
      buzzTimer = ms;
      beepState = !beepState;
      if (beepState) tone(BUZZ, 900);
      else noTone(BUZZ);
    }
  } else {
    noTone(BUZZ);
  }

  // --- LCD UPDATE ---
  if (ms - screenTimer >= 400) {
    screenTimer = ms;

    if (page == 0) {
      lcd.setCursor(0, 0);
      lcd.print("Gas: ");
      lcd.print(gasScore);
      lcd.print("%   ");

      lcd.setCursor(0, 1);
      if (gasScore >= 65) lcd.print("Status: DANGER ");
      else if (gasScore >= 30) lcd.print("Status: WARN   ");
      else {
        lcd.print("Fresh: ");
        lcd.print(freshness);
        lcd.print("%   ");
      }
    } else if (page == 1) {
      lcd.setCursor(0, 0);
      lcd.print("MQ135: ");
      lcd.print(mq135Val);
      lcd.print("    ");

      lcd.setCursor(0, 1);
      lcd.print("MQ2:   ");
      lcd.print(mq2Val);
      lcd.print("    ");
    } else if (page == 2) {
      lcd.setCursor(0, 0);
      lcd.print("Peak: ");
      lcd.print(peak);
      lcd.print("      ");

      lcd.setCursor(0, 1);
      lcd.print("Low:  ");
      lcd.print(lowest);
      lcd.print("      ");
    } else if (page == 3) {
      lcd.setCursor(0, 0);
      lcd.print("Cal135: ");
      lcd.print(base135);
      lcd.print("   ");

      lcd.setCursor(0, 1);
      lcd.print("CalMQ2: ");
      lcd.print(base2);
      lcd.print("   ");
    } else {
      lcd.setCursor(0, 0);
      lcd.print("Raw Values");
      lcd.setCursor(0, 1);
      lcd.print(analogRead(MQ135));
      lcd.print(" / ");
      lcd.print(analogRead(MQ2));
      lcd.print("    ");
    }
  }
}