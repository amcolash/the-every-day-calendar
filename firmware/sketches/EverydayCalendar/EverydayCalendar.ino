#include <EverydayCalendar_lights.h>
#include <EverydayCalendar_touch.h>
#include <EEPROM.h>

typedef struct {
   int8_t    x;
   int8_t    y;
} Point;

EverydayCalendar_touch cal_touch;
EverydayCalendar_lights cal_lights;
int16_t brightness = 128;

// Save birghtness to eeprom, add some buffer after LED values
# define EEPROM_BRIGHTNESS sizeof(uint32_t) * (12 + 1) + sizeof(int16_t) * 0

// Make sure random is a bit more random
# define EEPROM_RANDOM_SEED sizeof(uint32_t) * (12 + 1) + sizeof(int16_t) * 1

// Random animation timers
uint8_t brightnessTimer = 255;
uint8_t blink = 150;
uint32_t timer = 500;

// Only blink one day for a power-cycle. Thus when turned off by wifi outlet, it will reset this value
// so only the current day will ever blink. Once the current day is touched, no more blinkin'
bool shouldBlink = true;

void setup() {
  Serial.begin(9600);
  Serial.println("Sketch setup started");

  // Make things a bit more random 
  randomSeed(EEPROM.read(EEPROM_RANDOM_SEED));
  EEPROM.write(EEPROM_RANDOM_SEED, random(0, 32767));

  int16_t b = EEPROM.read(EEPROM_BRIGHTNESS);
  Serial.print("Loaded brightness from EEPROM: ");
  Serial.println(b);
  if (b != 0xFF) {
    brightness = b;
  }
  brightness = constrain(brightness, 0, 200);
  
  // Initialize LED functionality
  cal_lights.configure();
  cal_lights.setBrightness(200);
  cal_lights.begin();

  // Perform startup animation
  honeyDrip();

  // Fade out
  for(int b = 200; b >= 0; b--){
    cal_lights.setBrightness(b);
    delay(4);
  }

  // Initialize touch functionality
  cal_touch.configure();
  cal_touch.begin();
  cal_lights.loadLedStatesFromMemory();
  delay(1000);

  // Fade in
  for(int b = 0; b <= brightness; b++){
    cal_lights.setBrightness(b);
    delay(4);
  }
  
  Serial.println("Sketch setup complete");
}

void loop() {
  // Don't interrupt when a button is held down
  if (handleTouch()) return;

  handleBrightness();
  
  randomAnim();
  blinkAnim();
}


Point previouslyHeldButton = {0xFF, 0xFF}; // 0xFF and 0xFF if no button is held
uint16_t touchCount = 1;
const uint8_t debounceCount = 3;
const uint16_t clearCalendarCount = 1300; // ~40 seconds.  This is in units of touch sampling interval ~= 30ms.

bool handleTouch() {
  Point buttonPressed = {0xFF, 0xFF};
  bool touch = cal_touch.scanForTouch();
  
  // Handle a button press
  if(touch)
  {
    // Brightness Buttons
    if(cal_touch.y == 31){
      if(cal_touch.x == 4){
        if (brightness < 5) brightness -= 1;
        else brightness -= 2 * max(1, touchCount / 10);
      }else if(cal_touch.x == 6){
        if (brightness < 5) brightness += 1;
        else brightness += 2 * max(1, touchCount / 10);
      }

      brightness = constrain(brightness, 0, 200);
      Serial.print("Brightness = ");
      Serial.println(brightness);
      cal_lights.setBrightness((uint8_t)brightness);
    }
    // If all buttons aren't touched, reset debounce touch counter
    if(previouslyHeldButton.x == -1){
      touchCount = 0;
    }

    // If this button is been held, or it's just starting to be pressed and is the only button being touched
    if(((previouslyHeldButton.x == cal_touch.x) && (previouslyHeldButton.y == cal_touch.y))
    || (debounceCount == 0))
    {
      // The button has been held for a certain number of consecutive checks 
      // This is called debouncing
      if (touchCount == debounceCount){
        // Button is activated
        cal_lights.toggleLED((uint8_t)cal_touch.x, (uint8_t)cal_touch.y);
        cal_lights.saveLedStatesToMemory();
        Serial.print("x: ");
        Serial.print(cal_touch.x);
        Serial.print("\ty: ");
        Serial.println(cal_touch.y);

        // Turn off blinking when any button is pressed other than brightness
        if(!(cal_touch.y == 31 && (cal_touch.x == 4 || cal_touch.x == 6))) shouldBlink = false;
      }

      // Check if the special "Reset" January 1 button is being held
      if((cal_touch.x == 0) && (cal_touch.y == 0) && (touchCount == clearCalendarCount)){
        Serial.println("Resetting all LED states");
        clearAnimation();
      }
      
      if(touchCount < 65535){
        touchCount++;
//        Serial.print("touch count = ");
//        Serial.println(touchCount);
      }
    }
  }

  previouslyHeldButton.x = cal_touch.x;
  previouslyHeldButton.y = cal_touch.y;

  return touch;
}

void handleBrightness() {
  // Prevent problems with wearing out EEPROM when holding down brightness buttons
  brightnessTimer--;
  if (brightnessTimer <= 0) {
    brightnessTimer = 255;

    int16_t current = EEPROM.read(EEPROM_BRIGHTNESS);
    if (brightness != current) {
      Serial.println("Writing new brightness to EEPROM");
      EEPROM.put(EEPROM_BRIGHTNESS, brightness);
    }
  }
}

void sidewaysAnim() {
  static const uint8_t monthDayOffset[12] = {0, 3, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0};
  // Turn on all LEDs one by one in the span of a few second
  for(int day = 0; day < 31; day+= 2) {
    for(int month = 0; month < 12; month++) {
      int8_t adjustedDay = day - monthDayOffset[month];
      if(adjustedDay >= 0) {
        cal_lights.toggleLED(month, adjustedDay);
        delay(50);
        cal_lights.toggleLED(month, adjustedDay);
      }
    }
  }
}

void honeyDripToggle(){
  uint16_t interval_ms = 25;
  static const uint8_t monthDayOffset[12] = {0, 3, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0};
  // Turn on all LEDs one by one in the span of a few second
  for(int day = 0; day < 31; day++){
    for(int month = 0; month < 12; month++){
      int8_t adjustedDay = day - monthDayOffset[month];
      if(adjustedDay >= 0 ){
        cal_lights.toggleLED(month, adjustedDay);
      }
    }
    delay(interval_ms);
    interval_ms = interval_ms + 2;
  }
}

void honeyDrip(){
  uint16_t interval_ms = 25;
  static const uint8_t monthDayOffset[12] = {0, 3, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0};
  // Turn on all LEDs one by one in the span of a few second
  for(int day = 0; day < 31; day++){
    for(int month = 0; month < 12; month++){
      int8_t adjustedDay = day - monthDayOffset[month];
      if(adjustedDay >= 0 ){
        cal_lights.setLED(month, adjustedDay, true);
      }
    }
    delay(interval_ms);
    interval_ms = interval_ms + 2;
  }
}

void clearAnimation(){
  uint16_t interval_ms = 25;
  static const uint8_t monthMaxDay[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  for(int month = 11; month >= 0; month--){
    for(int day = monthMaxDay[month]-1; day >=0; day--){
       cal_lights.setLED(month, day, false);
       delay(interval_ms);
    }
  }
  cal_lights.saveLedStatesToMemory();
}

void randomAnim() {
  timer--;
  if (timer <= 0) {
    Serial.print("Time for a random animation");
    timer = random(500000, 1000000);

    int anim = random(0, 2);
    Serial.println(anim);
    switch(anim) {
      case 1:
        sidewaysAnim();
        break;
      default:
        honeyDripToggle();
        honeyDripToggle();
        break;
    }
  }
}

void blinkAnim() {
  blink--;
  if (blink <= 0 && shouldBlink) {
    blink = 150;

    bool found = false;
    int foundMonth = -1;
    int foundDay = -1;

    static const uint8_t monthDayOffset[12] = {0, 3, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0};
    for(int month = 0; month < 12; month++) {
      for(int day = 0; day < 31; day++) {
        int8_t adjustedDay = day - monthDayOffset[month];
        if(adjustedDay >= 0) {
          if (found && cal_lights.isLEDOn(month, adjustedDay)) found = false;
          
          if (!found && !cal_lights.isLEDOn(month, adjustedDay)) {
            foundMonth = month;
            foundDay = adjustedDay;
            found = true;
          }
        }
      }
    }

    if (found && foundMonth != -1 && foundDay != -1) {
      cal_lights.toggleLED(foundMonth, foundDay);
      delay(150);
      cal_lights.toggleLED(foundMonth, foundDay);
    }
  }
}
