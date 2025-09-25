#include <SPI.h>
#include <Adafruit_GFX.h>
#include <LS020.h>

// Hardware configuration
#define LCD_CS   10
#define LCD_RST  4
#define LCD_DC   5
#define TFT_BL   7
#define RANGE_OUTPUT_PIN 6  // Цифровой пин для выхода при переключении диапазона

// Pins for power and SWR measurement
#define FORWARD_PIN A0  // Forward power
#define REFLECTED_PIN A1  // Reflected power

// Colors for display (standard Adafruit_GFX colors)
#define BLACK 0x0000
#define WHITE 0xFFFF
#define RED 0xF800
#define GREEN 0x07E0
#define YELLOW 0xFFE0
#define BLUE 0x001F

// Калибровочные коэффициенты
const float CALIBRATION_FACTOR_FWD = 0.025;   // Для прямой мощности
const float CALIBRATION_FACTOR_REF = 0.025;   // Для отраженной мощности

LS020 tft = LS020(LCD_RST, LCD_DC, LCD_CS);

// Measurement variables
float forwardPower = 0;
float reflectedPower = 0;
float swr = 1.0;

// Peak power tracking
static float peakPower = 0;
static unsigned long peakTime = 0;
const unsigned long PEAK_HOLD_TIME = 3000; // 3 секунды удержания пика

// Power modes
#define POWER_MODE_LOW 50    // 50 Вт
#define POWER_MODE_HIGH 200  // 200 Вт
#define HYSTERESIS 5         // Гистерезис 5 Вт

int currentPowerMode = POWER_MODE_LOW;  // Текущий режим мощности
bool autoModeEnabled = true;            // Автоматическое переключение включено

// Анимационные переменные
static float displayForwardPower = 0;
static float displaySWR = 1.0;
static int displayPowerBarLength = 0;
static int displaySWRBarLength = 0;

// Флаги для отслеживания изменений
static bool forwardPowerChanged = false;
static bool swrChanged = false;
static bool powerBarChanged = false;
static bool swrBarChanged = false;
static bool powerModeChanged = false;  // Новый флаг для режима мощности
static bool peakPowerChanged = false;  // Флаг для изменения пика

void setup() {
  Serial.begin(9600);
  
  // Initialize pins
  pinMode(TFT_BL, OUTPUT);
  pinMode(RANGE_OUTPUT_PIN, OUTPUT);
  digitalWrite(RANGE_OUTPUT_PIN, LOW);  // Начальное состояние - LOW (50 Вт)
  
  // Initialize display
  tft.begin();
  tft.setRotation(0); // Portrait mode
  digitalWrite(TFT_BL, HIGH);
  
  // Draw static elements
  drawStaticElements();
  
  // Инициализация отображаемых значений
  displayForwardPower = forwardPower;
  displaySWR = swr;
  displayPowerBarLength = map(forwardPower, 0, currentPowerMode, 0, 150);
  displaySWRBarLength = 0; // При SWR=1.0 длина должна быть 0
  peakPower = 0; // Инициализация пика в 0
}

void loop() {
  // Measure power
  measurePower();
  
  // Calculate SWR
  calculateSWR();
  
  // Track peak power
  updatePeakPower();
  
  // Automatic power mode switching
  if (autoModeEnabled) {
    handlePowerModeSwitching();
  }
  
  // Проверяем изменения
  checkForChanges();
  
  // Update display with animation
  updateDisplay();
  
  delay(50); // Быстрее обновление для плавности
}

void measurePower() {
  int forwardRaw = analogRead(FORWARD_PIN);
  int reflectedRaw = analogRead(REFLECTED_PIN);
  
  forwardPower = forwardRaw * CALIBRATION_FACTOR_FWD;
  reflectedPower = reflectedRaw * CALIBRATION_FACTOR_REF;
  
  forwardPower = constrain(forwardPower, 0, currentPowerMode);
  reflectedPower = constrain(reflectedPower, 0, currentPowerMode);
}

void calculateSWR() {
  if (reflectedPower < 0.1) {
    swr = 1.0;
  } else if (forwardPower < 0.1) {
    swr = 99.9;
  } else {
    float gamma = reflectedPower / forwardPower;
    gamma = constrain(gamma, 0.001, 0.999);
    swr = (1 + gamma) / (1 - gamma);
    swr = constrain(swr, 1.0, 99.9);
  }
}

void updatePeakPower() {
  // Обновляем пик только если есть значимая мощность
  if (forwardPower > 1.0) {
    if (forwardPower > peakPower) {
      peakPower = forwardPower;
      peakTime = millis();
      peakPowerChanged = true;
    }
  }
  
  // Сброс пика через заданное время или при отсутствии сигнала
  if ((millis() - peakTime > PEAK_HOLD_TIME) || (forwardPower < 1.0)) {
    if (peakPower != 0) {
      peakPower = 0;
      peakPowerChanged = true;
    }
  }
}

void handlePowerModeSwitching() {
  static unsigned long lastSwitchTime = 0;
  const unsigned long switchDelay = 1000; // Минимальная задержка между переключениями
  
  // Защита от частого переключения
  if (millis() - lastSwitchTime < switchDelay) {
    return;
  }
  
  bool modeChanged = false;
  int oldPowerMode = currentPowerMode;
  
  // Переход в режим 200 Вт если мощность превышает предел 50 Вт
  if (currentPowerMode == POWER_MODE_LOW && forwardPower > POWER_MODE_LOW) {
    currentPowerMode = POWER_MODE_HIGH;
    modeChanged = true;
    digitalWrite(RANGE_OUTPUT_PIN, HIGH);  // Устанавливаем HIGH при переходе на 200 Вт
    Serial.println("Переключение в режим 200 Вт");
  }
  // Возврат в режим 50 Вт с гистерезисом
  else if (currentPowerMode == POWER_MODE_HIGH && forwardPower < (POWER_MODE_LOW - HYSTERESIS)) {
    currentPowerMode = POWER_MODE_LOW;
    modeChanged = true;
    digitalWrite(RANGE_OUTPUT_PIN, LOW);   // Устанавливаем LOW при возврате на 50 Вт
    Serial.println("Переключение в режим 50 Вт");
  }
  
  if (modeChanged) {
    powerModeChanged = true;
    lastSwitchTime = millis();
    // Перерисовываем статические элементы при смене режима
    drawStaticElements();
  }
}

void checkForChanges() {
  // Проверяем, изменились ли значения
  if (abs(forwardPower - displayForwardPower) > 0.1) {
    forwardPowerChanged = true;
  }
  if (abs(swr - displaySWR) > 0.01) {
    swrChanged = true;
  }
  
  // Проверяем изменение баров
  int targetPowerBar = map(forwardPower, 0, currentPowerMode, 0, 150);
  // Исправлено: SWR 1.0 должно давать 0, SWR 3.0 должно давать 150
  int targetSWRBar = 0;
  if (swr > 1.0) {
    float swrLimited = constrain(swr, 1.0, 3.0);
    targetSWRBar = map((swrLimited - 1.0) * 10, 0, 20, 0, 150);
  }
  
  if (targetPowerBar != displayPowerBarLength) {
    powerBarChanged = true;
  }
  if (targetSWRBar != displaySWRBarLength) {
    swrBarChanged = true;
  }
}

void updateDisplay() {
  bool needUpdate = false;
  bool modeDisplayChanged = false;
  
  // Анимация FWD
  if (forwardPowerChanged) {
    float diff = forwardPower - displayForwardPower;
    if (abs(diff) > 0.5) {
      displayForwardPower += diff * 0.3; // Плавный переход
    } else {
      displayForwardPower = forwardPower;
      forwardPowerChanged = false;
    }
    needUpdate = true;
  }

  // Анимация SWR
  if (swrChanged) {
    float diff = swr - displaySWR;
    if (abs(diff) > 0.05) {
      displaySWR += diff * 0.3;
    } else {
      displaySWR = swr;
      swrChanged = false;
    }
    needUpdate = true;
  }

  // Анимация Power Bar
  if (powerBarChanged) {
    int targetLength = map(forwardPower, 0, currentPowerMode, 0, 150);
    int diff = targetLength - displayPowerBarLength;
    if (abs(diff) > 1) {
      displayPowerBarLength += diff * 0.3;
    } else {
      displayPowerBarLength = targetLength;
      powerBarChanged = false;
    }
    needUpdate = true;
  }

  // Анимация SWR Bar
  if (swrBarChanged) {
    float swrLimited = constrain(swr, 1.0, 3.0);
    int targetLength = 0;
    if (swrLimited > 1.0) {
      targetLength = map((swrLimited - 1.0) * 10, 0, 20, 0, 150);
    }
    int diff = targetLength - displaySWRBarLength;
    if (abs(diff) > 1) {
      displaySWRBarLength += diff * 0.3;
    } else {
      displaySWRBarLength = targetLength;
      swrBarChanged = false;
    }
    needUpdate = true;
  }

  // Обновление при смене режима мощности
  if (powerModeChanged) {
    modeDisplayChanged = true;
    powerModeChanged = false;
    needUpdate = true;
  }

  // Обновление при изменении пика
  if (peakPowerChanged) {
    peakPowerChanged = false;
    needUpdate = true;
  }

  // Обновляем дисплей только если есть изменения
  if (needUpdate) {
    drawDisplayValues(modeDisplayChanged);
    drawAnimatedBars();
  }
}

void drawDisplayValues(bool modeChanged) {
  // Очистка и обновление FWD
  tft.fillRect(40, 95, 60, 10, BLACK);
  tft.setTextColor(YELLOW);
  tft.setTextSize(1);
  tft.setCursor(40, 95);
  tft.print((int)displayForwardPower);
  tft.print("W");

  // Очистка и обновление SWR
  tft.fillRect(40, 110, 60, 10, BLACK);
  tft.setTextColor(displaySWR > 2.0 ? RED : (displaySWR > 1.5 ? YELLOW : GREEN));
  tft.setCursor(40, 110);
  tft.print(displaySWR, 2);

  // Обновляем режим мощности только при фактическом изменении
  if (modeChanged) {
    tft.fillRect(5, 125, 150, 10, BLACK);
    tft.setTextColor(WHITE);
    tft.setCursor(5, 125);
    tft.print("Mode: ");
    tft.print(currentPowerMode);
    tft.print("W");
  }
  
  // Отображение пиковой мощности (новые координаты)
  tft.fillRect(100, 110, 55, 10, BLACK);
  if (peakPower > 0) {
    tft.setTextColor(peakPower > displayForwardPower ? RED : WHITE);
    tft.setCursor(100, 110);
    tft.print("Peak:");
    tft.print((int)peakPower);
  }
}

void drawAnimatedBars() {
  // Анимация Power Bar с градиентом
  static int lastPowerBarLength = 0;
  
  if (displayPowerBarLength != lastPowerBarLength || peakPowerChanged) {
    // Очистка изменённой части
    if (displayPowerBarLength > lastPowerBarLength) {
      tft.fillRect(lastPowerBarLength + 5, 25, displayPowerBarLength - lastPowerBarLength, 10, BLACK);
    } else if (displayPowerBarLength < lastPowerBarLength) {
      tft.fillRect(displayPowerBarLength + 5, 25, lastPowerBarLength - displayPowerBarLength, 10, BLACK);
    }

    // Рисование градиентного бара
    if (displayPowerBarLength > 0) {
      drawGradientBar(5, 25, displayPowerBarLength, 10, displayForwardPower / currentPowerMode);
    } else {
      // Полная очистка области бара если длина 0
      tft.fillRect(5, 25, 150, 10, BLACK);
    }
    
    // Рисование маркера пика
    drawPeakMarker();
    
    lastPowerBarLength = displayPowerBarLength;
  }

  // Анимация SWR Bar с градиентом
  static int lastSWRBarLength = 0;
  if (displaySWRBarLength != lastSWRBarLength) {
    // Очистка изменённой части
    if (displaySWRBarLength > lastSWRBarLength) {
      tft.fillRect(lastSWRBarLength + 5, 80, displaySWRBarLength - lastSWRBarLength, 10, BLACK);
    } else if (displaySWRBarLength < lastSWRBarLength) {
      tft.fillRect(displaySWRBarLength + 5, 80, lastSWRBarLength - displaySWRBarLength, 10, BLACK);
    }

    // Рисование градиентного бара - только если есть значение
    if (displaySWRBarLength > 0) {
      float swrNormalized = 0;
      if (displaySWR > 1.0) {
        swrNormalized = constrain((displaySWR - 1.0) / 2.0, 0.0, 1.0);
      }
      drawGradientBar(5, 80, displaySWRBarLength, 10, swrNormalized);
    } else {
      // Полная очистка области бара если длина 0
      tft.fillRect(5, 80, 150, 10, BLACK);
    }

    lastSWRBarLength = displaySWRBarLength;
  }
}

void drawPeakMarker() {
  static int lastPeakMarker = -1;
  
  // Очищаем старый маркер
  if (lastPeakMarker >= 0) {
    tft.drawFastVLine(5 + lastPeakMarker, 23, 14, BLACK);
  }
  
  // Рисуем новый маркер пика только если есть значимый пик
  if (peakPower > 1.0) {
    int peakMarkerPos = map(peakPower, 0, currentPowerMode, 0, 150);
    peakMarkerPos = constrain(peakMarkerPos, 0, 150);
    
    // Рисуем маркер пика (только если пик выше текущей мощности)
    if (peakMarkerPos > 0 && peakPower > displayForwardPower + 1.0) { // +1.0 для избежания мерцания
      tft.drawFastVLine(5 + peakMarkerPos, 23, 14, RED);
    }
    
    lastPeakMarker = peakMarkerPos;
  } else {
    lastPeakMarker = -1;
  }
}

// Функция для рисования градиентного бара
void drawGradientBar(int x, int y, int width, int height, float intensity) {
  // Ограничиваем ширину
  width = constrain(width, 0, 150);
  
  // Рисуем по одному пикселю для плавного градиента
  for (int i = 0; i < width; i++) {
    // Вычисляем интенсивность для текущего пикселя
    float pixelIntensity = (float)i / 150.0;
    
    // Интерполируем цвет между зелёным, жёлтым и красным
    uint16_t color = interpolateColor(pixelIntensity);
    
    // Рисуем вертикальную линию высотой height
    tft.drawFastVLine(x + i, y, height, color);
  }
}

// Функция интерполяции цвета
uint16_t interpolateColor(float ratio) {
  // Ограничиваем ratio от 0.0 до 1.0
  ratio = constrain(ratio, 0.0, 1.0);
  
  // Разбиваем на три сегмента:
  // 0.0-0.5: зелёный -> жёлтый
  // 0.5-1.0: жёлтый -> красный
  
  if (ratio <= 0.5) {
    // Зелёный -> Жёлтый
    float t = ratio * 2.0; // 0.0-1.0
    return blendColors(GREEN, YELLOW, t);
  } else {
    // Жёлтый -> Красный
    float t = (ratio - 0.5) * 2.0; // 0.0-1.0
    return blendColors(YELLOW, RED, t);
  }
}

// Функция смешивания двух цветов
uint16_t blendColors(uint16_t color1, uint16_t color2, float ratio) {
  // Ограничиваем ratio от 0.0 до 1.0
  ratio = constrain(ratio, 0.0, 1.0);
  
  // Извлекаем компоненты RGB
  uint8_t r1 = (color1 >> 11) & 0x1F;
  uint8_t g1 = (color1 >> 5) & 0x3F;
  uint8_t b1 = color1 & 0x1F;
  
  uint8_t r2 = (color2 >> 11) & 0x1F;
  uint8_t g2 = (color2 >> 5) & 0x3F;
  uint8_t b2 = color2 & 0x1F;
  
  // Интерполируем каждый компонент
  uint8_t r = r1 + (r2 - r1) * ratio;
  uint8_t g = g1 + (g2 - g1) * ratio;
  uint8_t b = b1 + (b2 - b1) * ratio;
  
  // Собираем цвет обратно
  return ((r & 0x1F) << 11) | ((g & 0x3F) << 5) | (b & 0x1F);
}

void drawStaticElements() {
  tft.fillScreen(BLACK);
  
  // Title
  tft.setTextColor(WHITE);
  tft.setTextSize(1);
  tft.setCursor(5, 5);
  tft.print("SWR & PWR-Meter US5CAA");
  
  // Power scale - динамически изменяется в зависимости от режима
  tft.drawLine(5, 40, 155, 40, WHITE);
  
  if (currentPowerMode == POWER_MODE_LOW) {
    // Шкала для 50 Вт
    for (int i = 0; i <= 40; i += 10) {
      int x = 5 + map(i, 0, 50, 0, 150);
      tft.drawLine(x, 40, x, 35, WHITE);
      tft.setCursor(x - 5, 45);
      tft.print(i);
    }

    // Отдельно выводим "50" с корректным отступом
    int x50 = 5 + map(50, 0, 50, 0, 150);
    tft.drawLine(x50, 40, x50, 35, WHITE);
    tft.setCursor(x50 - 5, 45);
    tft.print("50");
  } else {
    // Шкала для 200 Вт
    for (int i = 0; i <= 200; i += 50) {
      int x = 5 + map(i, 0, 200, 0, 150);
      tft.drawLine(x, 40, x, 35, WHITE);
      tft.setCursor(x - 8, 45);
      tft.print(i);
    }
  }

  // SWR scale - переработано: теперь метки 1, 1.5, 2, 2.5, 3 выровнены как на шкале мощности
  tft.setCursor(5, 65);
  tft.print("SWR:");
  tft.setCursor(40, 65);
  tft.print("1   ");
  tft.setCursor(70, 65);
  tft.print("1.5 ");
  tft.setCursor(100, 65);
  tft.print("2   ");
  tft.setCursor(130, 65);
  tft.print("2.5 ");
  tft.setCursor(160, 65);
  tft.print("3");

  tft.drawLine(5, 75, 155, 75, WHITE);
  for (int i = 1; i <= 3; i++) {
    int x = 5 + map(i, 1, 3, 0, 150);
    tft.drawLine(x, 75, x, 70, WHITE);
  }

  // Labels - "FWD:" теперь ниже барграфа
  tft.setCursor(5, 95);
  tft.print("FWD:");
  tft.setCursor(5, 110);
  tft.print("SWR:");
  
  // Инициализация отображения режима мощности
  tft.setCursor(5, 125);
  tft.setTextColor(WHITE);
  tft.print("Mode: ");
  tft.print(currentPowerMode);
  tft.print("W");
}
