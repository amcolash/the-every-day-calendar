#include <EverydayCalendar_lights.h>
#include <EverydayCalendar_touch.h>
#include <EEPROM.h>

typedef struct {
   int8_t    x;
   int8_t    y;
} Point;

typedef struct {
   float    x;
   float    y;
} Pointf;

typedef struct {
  Pointf position;
  Pointf velocity;
  uint8_t life;
} Particle;

EverydayCalendar_touch cal_touch;
EverydayCalendar_lights cal_lights;
int16_t brightness = 128;

// Save brightness to eeprom, add some buffer after LED values
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

// Keep track of our particles
bool particleMode = false;
int pIndex = 0;
# define NUM_PARTICLES 30
Particle particles[NUM_PARTICLES];

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
  delay(500);

  // Fade in
  for(int b = 0; b <= brightness; b++){
    cal_lights.setBrightness(b);
    delay(4);
  }

  Serial.println("Sketch setup complete");
}

float gravity = 0.05;
float friction = 0.98;
float bounce = 0.85; // energy lost on bounce (25%)
float threshold = 0.02;

void spawnParticle(int i, float x, float y) {
  particles[i].position.x = x;
  particles[i].position.y = y;

  particles[i].velocity.x = random(1, 30) / 100.;
  particles[i].velocity.y = random(80, 150) / 100.;

  // How long the particle lives after it lands on the ground so things don't simply "pop" out immediately
  particles[i].life = 20;
}

void handleParticles() {
  cal_lights.clearAllLEDs();

  for (int i = 0; i < NUM_PARTICLES; i++) {
    if (particles[i].life <= 0) continue;

    // Reverse on bounce, lose some energy, add friction on vertical bounce
    if (particles[i].position.x <= 0 || particles[i].position.x > 11) {
      particles[i].velocity.x *= -bounce;
    }
    if (particles[i].position.y <= 0 || particles[i].position.y > 30) {
      particles[i].velocity.y *= -bounce;
      particles[i].velocity.x *= friction;
    }

    // Keep things in the box
    particles[i].position.x = constrain(particles[i].position.x, 0, 11);
    particles[i].position.y = constrain(particles[i].position.y, 0, 30);


    // Stop if too close to 0
    if (fabs(particles[i].velocity.x) <= threshold) particles[i].velocity.x = 0;
    if (fabs(particles[i].velocity.y) <= threshold) particles[i].velocity.y = 0;

    if (fabs(particles[i].velocity.x) <= threshold && fabs(particles[i].velocity.y) <= threshold && particles[i].position.y >= 30 - threshold) {
      particles[i].position.y = 30;
      particles[i].life--;
    }

    particles[i].position.x += particles[i].velocity.x;

    particles[i].velocity.y += gravity;
    particles[i].position.y += particles[i].velocity.y;

    cal_lights.setLED(particles[i].position.x, particles[i].position.y, true);
  }

  cal_lights.setLED(11, 30, true);

  delay(15);
}

void loop() {
  if (particleMode) handleParticles();

  // Don't interrupt when a button is held down
  if (handleTouch()) return;

  handleBrightness();

  if (!particleMode) {
    randomAnim();
    blinkAnim();
  }
}


Point previouslyHeldButton = {0xFF, 0xFF}; // 0xFF and 0xFF if no button is held
uint16_t touchCount = 1;
const uint8_t debounceCount = 3;
const uint16_t clearCalendarCount = 1300; // ~40 seconds.  This is in units of touch sampling interval ~= 30ms.
const uint16_t particleModeCount = 20;

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
        // Spawn a particle when pressed in particle mode
        if (particleMode && cal_touch.x != 11 && cal_touch.y != 30) {
          pIndex = (pIndex + 1) % NUM_PARTICLES;
          spawnParticle(pIndex, cal_touch.x, cal_touch.y);
        } else if (!particleMode && touchCount == debounceCount) {
        // The button has been held for a certain number of consecutive checks
        // This is called debouncing

        // Button is activated
        cal_lights.toggleLED((uint8_t)cal_touch.x, (uint8_t)cal_touch.y);
        cal_lights.saveLedStatesToMemory();
        Serial.print("x: ");
        Serial.print(cal_touch.x);
        Serial.print("\ty: ");
        Serial.println(cal_touch.y);

        // Turn off blinking when any button is pressed other than brightness / dec 31
        if(!((cal_touch.x == 11) && (cal_touch.y == 30)) || !(cal_touch.y == 31 && (cal_touch.x == 4 || cal_touch.x == 6))) shouldBlink = false;
      }

      // Check if the special "Reset" January 1 button is being held
      if((cal_touch.x == 0) && (cal_touch.y == 0) && (touchCount == clearCalendarCount)){
        Serial.println("Resetting all LED states");
        clearAnimation();
      }

      // Check if particle mode button (Dec 31) is being held
      if((cal_touch.x == 11) && (cal_touch.y == 30) && (touchCount == particleModeCount)) {
        particleMode = !particleMode;
        cal_lights.clearAllLEDs();

        Serial.print("Switching particle mode state: ");
        Serial.println(particleMode);

        honeyDrip();

        Serial.println(brightness);

        // Fade out
        for(int b = brightness; b >= 0; b--){
          cal_lights.setBrightness(b);
          delay(4);
        }

        if (particleMode) {
          for (int i = 0; i < NUM_PARTICLES; i++) {
            particles[i].life = 0;
          }

          cal_lights.clearAllLEDs();
        } else {
          cal_lights.loadLedStatesFromMemory();
          blink = 150;
          timer = 500;
        }

        delay(500);

        // Fade in
        for(int b = 0; b <= brightness; b++){
          cal_lights.setBrightness(b);
          delay(4);
        }
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
