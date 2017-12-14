#include <limits.h>

//*CONFIGURATION CONSTANTS*

//Sensor pins
//At least one plz...
unsigned const short NUM_SENSORS = 1;
unsigned const short PINS[NUM_SENSORS] = {0};

//the note corresponding to each sensor
unsigned const short NOTES[NUM_SENSORS] = {61};

unsigned const short MIDI_CHANNEL = 0;

//MAX_THRESHOLD is used when the baseline is very unstable
unsigned const short MAX_THRESHOLD = 120;

//MIN_THRESHOLD is used when the baseline is very stable
unsigned const short MIN_THRESHOLD = 75;

//Delays in *microseconds* after sending corresponding midi messages
//Filters out the noise when going across threshold and limits number of
//aftertouch messages sent
unsigned const long NOTE_ON_DELAY = 80000;
unsigned const long NOTE_OFF_DELAY = 20000;
unsigned const long AFTERTOUCH_DELAY = 20000;

//Delay in *microseconds* for printing
//Prevents overloading serial communications
unsigned const short PRINT_DELAY = 50;

//used in cycle dependent settings so that performance
//remains similar across different clocks
unsigned const short CLOCK_RATE = 180;

//Used to scale parameters based on configuration
//Will grow with clock speed and shrink with number of sensors to sample
unsigned const short SCALE_FACTOR = CLOCK_RATE / NUM_SENSORS;

//to avoid sending signals for noise spikes
unsigned const short MIN_JUMPS_FOR_SIGNAL = (unsigned const short) max((0.01 * SCALE_FACTOR), 1);

//amount of sensorReadings that we average baseline over
unsigned const short BASELINE_BUFFER_SIZE = (unsigned const short) (20 * SCALE_FACTOR);

//Amount of sensor readings used to average press velocity.
//Avoid making too large as then you might miss short jumps and add too much latency
//Setting it lower would reduce latency and help detecting short jumps, but also allow for more noise
unsigned const short JUMP_BUFFER_SIZE = (unsigned const short) max((0.01 * SCALE_FACTOR), 1);

//After this amount of stagnant (consecutive and non-varying) jumps is reached,
//the baseline is reset to that jump sequences avg velocity
unsigned const long MAX_STAGNANT_JUMPS = (unsigned const long) (2 * SCALE_FACTOR);

//Used to ignore first part of stagnant jump when resetting baseline
unsigned const long STAGNATION_BUFFER_DELAY = MAX_STAGNANT_JUMPS / 3;

//stagnant jumps we average over when resetting baseline after getting stuck in jump
unsigned const long SJUMP_BUFFER_SIZE = (unsigned const long) min(max((0.25 * SCALE_FACTOR), 1), MAX_STAGNANT_JUMPS - STAGNATION_BUFFER_DELAY);

//number of microseconds() after jump during which baseline update is paused
//this delay occurs after the NOTE_OFF delay, but only avoids baseline buffering, not jumping
unsigned const long BASELINE_BLOWBACK_DURATION = 20000;

//number of cycles removed from baseline when jump is over
//this is used to prevent jump beginning from weighing in on baseline
//making too large would prevent baseline update while fast-tapping
unsigned const short RETRO_JUMP_BLOWBACK_CYCLES = (unsigned const short) (0.0625 * SCALE_FACTOR);

//*SYSTEM CONSTANTS*
//these shouldn't have to be modified

//Serial communication Hz
unsigned const long BAUD_RATE = 115200;

//Maximum value returned by AnalogRead()
//Always 1024 for arduino
unsigned const short MAX_READING = 1024;

//How often should we store stagnant jump values to the sJumpBuffer
//*DO NOT MODIFY*
unsigned const long CYCLES_PER_SJUMP = max(((MAX_STAGNANT_JUMPS - STAGNATION_BUFFER_DELAY) / SJUMP_BUFFER_SIZE), (unsigned const long) 1);


//*GLOBAL VARIABLES*
//would love to make them static and local to loop(), but they need to be initialized to non-zero values
//std::vector allows non-zero initialization, but I'm not sure I should include it just for this purpose...

//current baseline for each pin
unsigned short baseline[NUM_SENSORS];

//last read value for each pin
unsigned short lastSensorReading[NUM_SENSORS];

//current threshold
unsigned short jumpThreshold[NUM_SENSORS];

//number of microseconds left to ignore input from each sensor
unsigned long toWait[NUM_SENSORS];


void setup() {
  Serial.begin(BAUD_RATE);

  memset(baseline, MAX_READING, sizeof(baseline));
  memset(lastSensorReading, MAX_READING * 2, sizeof(lastSensorReading));
  memset(jumpThreshold, (MIN_THRESHOLD + MAX_THRESHOLD / 2), sizeof(jumpThreshold));
}

void loop() {
  //*STATIC VARIABLES*

  //used to initiate blowback waiting
  static bool justJumped[NUM_SENSORS];

  //used to average the baseline
  //gets the average value computed at each loop()
  static unsigned short baselineBuffer[NUM_SENSORS][BASELINE_BUFFER_SIZE];

  //counts the number of loop() executions without jumps
  //used to add values in the baselineBuffer
  static unsigned short baselineCount[NUM_SENSORS];

  //used to reset baseline after getting stuck in jump
  static unsigned short sJumpBuffer[NUM_SENSORS][SJUMP_BUFFER_SIZE];

  //number of stored stagnant jumps
  static unsigned short sJumpIndex[NUM_SENSORS];

  //number of stagnant jumps (not all are stored)
  static unsigned long sJumpCount[NUM_SENSORS];


  //used to delay baseline calculation after coming out of jump
  static unsigned long toWaitForBaseline[NUM_SENSORS];

  static unsigned long lastTime[NUM_SENSORS];

  //*STACK VARIABLES*

  //small buffer used to smooth signal
  unsigned short jumpBuffer[NUM_SENSORS][JUMP_BUFFER_SIZE];
  memset(jumpBuffer, 0, sizeof(jumpBuffer));

  unsigned short toPrint[NUM_SENSORS];
  memset(toPrint, 0, sizeof(toPrint));

  //*PROCESS SIGNAL*

  fillJumpBuffer(jumpBuffer);

  //process buffer content for each sensor
  for (unsigned short currentSensor = 0; currentSensor < NUM_SENSORS; currentSensor++) {

    unsigned short sensorReadingAvg = bufferAverage(jumpBuffer[currentSensor], JUMP_BUFFER_SIZE);
    unsigned short distanceAboveBaseline = max(0, sensorReadingAvg - baseline[currentSensor]);

//    toPrint[currentSensor] = sensorReadingAvg;

    //JUMPING
    if (distanceAboveBaseline >= jumpThreshold[currentSensor]) {

      //WAIT
      if (toWait[currentSensor] > 0) {
        unsigned long thisTime = micros();

        unsigned long deltaTime = thisTime - lastTime[currentSensor];
        if (deltaTime < toWait[currentSensor]){
          toWait[currentSensor] -= deltaTime;
        } else {
          toWait[currentSensor] = 0;
        }

        lastTime[currentSensor] = thisTime;
      }

      //STAGNATION CHECK
      else if (sJumpIndex[currentSensor] == SJUMP_BUFFER_SIZE) {

        unsigned short sJumpAvg = bufferAverage(sJumpBuffer[currentSensor], SJUMP_BUFFER_SIZE);

        //raise average a little to ensure we get out of jump
        baseline[currentSensor] = min(MAX_READING, sJumpAvg * 1.1);

        //reset counters
        baselineCount[currentSensor] = 0;
        sJumpCount[currentSensor] = 0;
        sJumpIndex[currentSensor] = 0;
        lastSensorReading[currentSensor] = MAX_READING * 2;
      }
      //SIGNAL
      else {
        //INCREMENT
        if (sJumpCount[currentSensor] > STAGNATION_BUFFER_DELAY &&
            (sJumpCount[currentSensor] - STAGNATION_BUFFER_DELAY) % CYCLES_PER_SJUMP == 0) {
          sJumpBuffer[currentSensor][sJumpIndex[currentSensor]] = sensorReadingAvg;
          sJumpIndex[currentSensor]++;
        }
        sJumpCount[currentSensor]++;
        lastSensorReading[currentSensor] = sensorReadingAvg;

        //NOTE_ON
        if (sJumpCount[currentSensor] == MIN_JUMPS_FOR_SIGNAL) {
          justJumped[currentSensor] = true;
          usbMIDI.sendNoteOn(NOTES[currentSensor], map(constrain(distanceAboveBaseline, MIN_THRESHOLD, 512), MIN_THRESHOLD, 512, 32, 127), 1);
          usbMIDI.send_now();
          // MIDI Controllers should discard incoming MIDI messages.
          while (usbMIDI.read()) {}
          lastTime[currentSensor] = micros();
          toWait[currentSensor] = NOTE_ON_DELAY;
        }
        //AFTERTOUCH
        else if (sJumpCount[currentSensor] > MIN_JUMPS_FOR_SIGNAL) {
          usbMIDI.sendPolyPressure(NOTES[currentSensor], map(constrain(distanceAboveBaseline, 0, 512), MIN_THRESHOLD, 512, 32, 127), 1);
          usbMIDI.send_now();
          // MIDI Controllers should discard incoming MIDI messages.
          while (usbMIDI.read()) {}
          lastTime[currentSensor] = micros();
          toWait[currentSensor] = AFTERTOUCH_DELAY;
        }
      }
    }
    //BASELINING
    else {
      //WAIT FOR SIGNALING
      if (toWait[currentSensor] > 0) {
        unsigned long thisTime = micros();
        unsigned long deltaTime = thisTime - lastTime[currentSensor];
        
        if (deltaTime < toWait[currentSensor]){
          toWait[currentSensor] -= deltaTime;
        } else {
          toWait[currentSensor] = 0;
        }
        
        lastTime[currentSensor] = thisTime;
      }
      //NOTE_OFF
      else if (justJumped[currentSensor]) {
        usbMIDI.sendNoteOff(NOTES[currentSensor], 0, 1);
        usbMIDI.send_now();
        // MIDI Controllers should discard incoming MIDI messages.
        while (usbMIDI.read()) {}

        justJumped[currentSensor] = false;
        baselineCount[currentSensor] = max((long) 0, (long) baselineCount[currentSensor] - RETRO_JUMP_BLOWBACK_CYCLES);
        lastTime[currentSensor] = micros();
        toWait[currentSensor] = NOTE_OFF_DELAY;
        toWaitForBaseline[currentSensor] = BASELINE_BLOWBACK_DURATION;

        //reset counters
        lastSensorReading[currentSensor] = MAX_READING * 2;
        sJumpCount[currentSensor] = 0;
        sJumpIndex[currentSensor] = 0;
      }
      //WAIT FOR BASELINING
      else if (toWaitForBaseline[currentSensor] > 0) {
        unsigned long thisTime = micros();
        unsigned long deltaTime = thisTime - lastTime[currentSensor];
        if (deltaTime < toWaitForBaseline[currentSensor]){
          toWaitForBaseline[currentSensor] -= deltaTime;
        } else {
          toWaitForBaseline[currentSensor] = 0;
        }
        
        lastTime[currentSensor] = thisTime;
      }
      //RESET BASELINE AND THRESHOLD
      else if (baselineCount[currentSensor] > (BASELINE_BUFFER_SIZE - 1)) {
        jumpThreshold[currentSensor] = updateThreshold(baselineBuffer[currentSensor], baseline[currentSensor], jumpThreshold[currentSensor]);

        baseline[currentSensor] = bufferAverage(baselineBuffer[currentSensor], BASELINE_BUFFER_SIZE);
        baselineCount[currentSensor] = 0;

        //reset counters
        lastSensorReading[currentSensor] = MAX_READING * 2;
        sJumpCount[currentSensor] = 0;
        sJumpIndex[currentSensor] = 0;
      }
      //SAVE
      else {
        baselineBuffer[currentSensor][baselineCount[currentSensor]] = sensorReadingAvg;
        baselineCount[currentSensor]++;

        //reset counters
        lastSensorReading[currentSensor] = MAX_READING * 2;
        sJumpCount[currentSensor] = 0;
        sJumpIndex[currentSensor] = 0;
      }
    }
//    printResults(toPrint, sizeof(toPrint) / sizeof(short));
  }
}

//*HELPERS*

unsigned short bufferAverage(unsigned short * a, unsigned long aSize) {
  unsigned long sum = 0;
  for (unsigned long i = 0; i < aSize; i++) {
    //makes sure we dont bust when filling up sum
    if (sum < (ULONG_MAX - a[i])) {
      sum = sum + a[i];
    }
    else {
      Serial.println("WARNING: Exceeded ULONG_MAX while running bufferAverage(). Check your parameters to ensure buffers aren't too large.");
      delay(3000);
      return (unsigned short) constrain(ULONG_MAX / aSize, (unsigned long) 0, (unsigned long) USHRT_MAX) ;
    }
  }
  return (unsigned short) (sum / aSize);
}

unsigned short varianceFromTarget(unsigned short * a, unsigned long aSize, unsigned short target) {
  unsigned long sum = 0;
  for (unsigned long i = 0; i < aSize; i++) {
    //makes sure we dont bust when filling up sum
    if (sum < ULONG_MAX - a[i]) {
      sum += pow((int) (a[i] - target), 2);
    }
    else {
      Serial.println("WARNING: Exceeded ULONG_MAX while running varianceFromTarget(). Check your parameters to ensure buffers aren't too large.");
      delay(3000);
      return (unsigned short) constrain(ULONG_MAX / aSize, (unsigned long) 0, (unsigned long) USHRT_MAX);
    }
  }
  return (unsigned short) pow((sum / aSize), 1);
}

//Single use function to improve readability
void fillJumpBuffer(unsigned short (&jBuff)[NUM_SENSORS][JUMP_BUFFER_SIZE]) {
  for (unsigned short sample = 0; sample < JUMP_BUFFER_SIZE; sample++) {
    for (unsigned short sensor = 0; sensor < NUM_SENSORS; sensor++) {
      jBuff[sensor][sample] = (unsigned short) analogRead(PINS[sensor]);
    }
  }
}

//Single use function to improve readability
unsigned short updateThreshold(unsigned short (&baselineBuff)[BASELINE_BUFFER_SIZE], unsigned short oldBaseline, unsigned short oldThreshold) {

  unsigned short varianceFromBaseline = varianceFromTarget(baselineBuff, BASELINE_BUFFER_SIZE, oldBaseline);
  unsigned short newThreshold = constrain(varianceFromBaseline, MIN_THRESHOLD, MAX_THRESHOLD);

  int deltaThreshold = newThreshold - oldThreshold;
  if (deltaThreshold < 0) {
    //split the difference to slow down threshold becoming more sensitive
    newThreshold = constrain(oldThreshold + ((deltaThreshold) / 4), MIN_THRESHOLD, MAX_THRESHOLD);
  }

  return newThreshold;
}

//print results for all sensors in Arduino Plotter format
//Note that running the debug slows down the rest of the script (requires delay to avoid overloading serial)
//so you'll have to compensate for the slowdown when setting parameters
void printResults(unsigned short toPrint[], unsigned short printSize) {
  for (unsigned short i = 0; i < printSize; i++) {
    Serial.print("0");
    Serial.print(" ");
    Serial.print(toPrint[i]);
    Serial.print(" ");
    Serial.print(baseline[i]);
    Serial.print(" ");
    Serial.print(baseline[i] + jumpThreshold[i]);
    Serial.print(" ");
    Serial.print(baseline[i] + jumpThreshold[i]);
//    Serial.print(" ");
//    Serial.print(toWait[i]);
    Serial.print(" ");
    Serial.print(1000);
  }
  Serial.println();
  delayMicroseconds(PRINT_DELAY);
}
