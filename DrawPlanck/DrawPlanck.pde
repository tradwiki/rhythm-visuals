import themidibus.*;
import javax.sound.midi.MidiMessage; 
import java.util.Arrays; 
import java.util.List;
import java.util.Iterator; 
import java.util.Properties;
import java.io.InputStream; 
import java.io.IOException;
import java.lang.IllegalArgumentException;



////////CONSTANT GLOBALS (don't change after setup) ////////

//list of Mode implementing instances to switch between
final Mode[] MODES = new Mode[1];

//list of named config parameters that can have a note assigned
final String[] NAMED_PADS = {"BOTTOM_RIGHT_NOTE", "BOTTOM_LEFT_NOTE", "TOP_LEFT_NOTE", "TOP_RIGHT_NOTE"};

//list of other pad notes read in from config
final ArrayList<Integer> AUX_PAD_NOTES = new ArrayList<Integer>(); 

//static (non-changing) pad data and helper methods
final ArrayList<Pad> pads = new ArrayList<Pad>();

//sum of named and auxiliary pads
int NUM_PADS;

//should fill this with default config using setProperty(string_key, string_value) before calling loadConfig()
final Properties defaultConfig = new Properties();

//filled by loadConfig()
Properties globalLoadedConfig;

MidiBus myBus;

//background image
PGraphics pg;

////////DYNAMIC GLOBALS (change after setup) /////////

Mode currentMode;
int currentModeIndex = 0;

//flags indicating a pad was pressed, set by midi callback and unset after each draw()
ArrayList<Boolean> padWasPressed;

//number of consecutive presses for each pad
//pressing any pad resets the count on all the others
//switching mode resets the count on all pads
ArrayList<Integer> pressCounter; 

void setup() {
  size(800, 600, P2D);
  //fullScreen(P2D);
  frameRate(30);

  defaultConfig.setProperty("LOGO_SCALING", "0.05");
  defaultConfig.setProperty("MIDI_DEVICE", "0");
  defaultConfig.setProperty("PRESSES_FOR_MODE_SWITCH", "3");
  defaultConfig.setProperty("BOTTOM_RIGHT_NOTE", "85");
  defaultConfig.setProperty("BOTTOM_LEFT_NOTE", "84");
  defaultConfig.setProperty("TOP_LEFT_NOTE", "80");
  defaultConfig.setProperty("TOP_RIGHT_NOTE", "82");
  defaultConfig.setProperty("AUX_PAD_NOTES", "");

  //read config file
  loadConfigFrom("config.properties");

  //parse auxiliary notes list from config
  List<String> string_aux_pad_notes = Arrays.asList(globalLoadedConfig.getProperty("AUX_PAD_NOTES").split("\\s*,\\s*"));
  Iterator<String> iter = string_aux_pad_notes.iterator();
  while (iter.hasNext()) {
    String next = iter.next();
    try {      
      AUX_PAD_NOTES.add(Integer.parseInt(next));
    } 
    catch (NumberFormatException e) {
      println("Warning: Config var AUX_PAD_NOTES is either empty or not of expected type (int). Ignoring its value: " + next);
    }
  }
  println("Global config: ");  
  println(globalLoadedConfig);
  println();

  //create pad list
  for (int i = 0; i < NAMED_PADS.length; i++) {
    int note = getIntProp(NAMED_PADS[i]);
    println(NAMED_PADS[i] + " : " + note);
    pads.add(new Pad(NAMED_PADS[i], note, false));
  }
  for (int i = 0; i < AUX_PAD_NOTES.size(); i++) {
    int note = AUX_PAD_NOTES.get(i);
    println("AUX_" + i + " : " + note);
    pads.add(new Pad(NAMED_PADS[i], note, true));
  }

  NUM_PADS = pads.size(); 

  //setup midi
  MidiBus.list(); 
  myBus = new MidiBus(this, getIntProp("MIDI_DEVICE"), 1); 

  //Create background static image (PGraphic)
  noFill();
  stroke(255, 0, 0);
  pg = createGraphics(width, height);
  PImage logo;
  logo = loadImage("bitmap.png");
  println();
  pg.beginDraw();
  pg.background(25);
  int newWidth = (int)(logo.width * getFloatProp("LOGO_SCALING"));
  int newHeight = (int) (logo.height * getFloatProp("LOGO_SCALING"));
  pg.image(logo, width/2-(newWidth/2), height/2-(newHeight/2), newWidth, newHeight);
  pg.endDraw();

  //global state init
  padWasPressed = new ArrayList<Boolean>();
  pressCounter = new ArrayList<Integer>();  
  for ( int padIndex = 0; padIndex < NUM_PADS; padIndex++) {
    padWasPressed.add(false);
    pressCounter.add(0);
  }

  //Create modes and initialize currentMode
  MODES[0] = new CircleMode();
  //MODES[1] = new SquareMode();

  currentModeIndex = 0;
  currentMode = MODES[currentModeIndex];
  currentMode.setup();

  //easier to scale
  colorMode(HSB, 255);
}

void draw() {
  //Redraw bg to erase previous frame
  background(pg);

  //Increment presses and check for mode switch
  for (int padIndex = 0; padIndex < NUM_PADS; padIndex++) {
    Pad pad = pads.get(padIndex);
    if (padWasPressed.get(padIndex)) {
      //reset pressCounter on other pads
      for (int otherpad = 0; otherpad<NUM_PADS; otherpad++) {
        if (otherpad != padIndex) {
          pressCounter.set(otherpad, 0);
        }
      }

      //increment own presscounter
      pressCounter.set(padIndex, pressCounter.get(padIndex) + 1);

      //switch modes
      if (pad.name == "TOP_LEFT_NOTE" && pressCounter.get(padIndex) >= getIntProp("PRESSES_FOR_MODE_SWITCH")) {
        currentModeIndex++;
        if (currentModeIndex >= MODES.length) {          
          currentModeIndex = 0;
        }        
        currentMode = MODES[currentModeIndex];
        pressCounter.set(padIndex, 0);
        currentMode.setup();
        //reset pressed flag before drawing new mode
        padWasPressed.set(padIndex, false);
      }
    }
  }

  currentMode.draw();

  //reset pressed flag
  for (int padIndex = 0; padIndex < NUM_PADS; padIndex++) {
    padWasPressed.set(padIndex, false);
  }
}

void loadConfigFrom(String configFileName) {
  globalLoadedConfig = new Properties(defaultConfig);
  InputStream is = null;
  try {
    is = createInput(configFileName);
    globalLoadedConfig.load(is);
  } 
  catch (IOException ex) {
    println("Error reading config file.");
  }
}

int getIntProp(String propName) {
  int toReturn;
  if (globalLoadedConfig.containsKey(propName)) {
    try {
      toReturn = Integer.parseInt(globalLoadedConfig.getProperty(propName));
    } 
    catch (NumberFormatException e) {
      println("WARNING: Config var" + propName + " is not of expected type (integer). Falling back to default config for this parameter.");
      toReturn = Integer.parseInt(defaultConfig.getProperty(propName));
    }
  } else {
    println("Error: Couldn't find requested config var : " + propName);
    throw(new IllegalArgumentException());
  }
  return toReturn;
}

float getFloatProp(String propName) {
  float toReturn;
  if (globalLoadedConfig.containsKey(propName)) {
    try {
      toReturn = Float.parseFloat(globalLoadedConfig.getProperty(propName));
    } 
    catch (NumberFormatException e) {
      println("WARNING: Config var" + propName + " is not of expected type (float). Falling back to default config for this parameter.");
      toReturn = Float.parseFloat(defaultConfig.getProperty(propName));
    }
  } else {
    println("Error: Couldn't find requested config var : " + propName);
    throw(new IllegalArgumentException());
  }
  return toReturn;
}


//Called by MidiBus library whenever a new midi message is received
void midiMessage(MidiMessage message) {
  int note = (int)(message.getMessage()[1] & 0xFF) ;
  int vel = (int)(message.getMessage()[2] & 0xFF);
  println("note: " + note + " vel: "+ vel);

  int padIndex = Pad.noteToPad(note);
  if (padIndex >= 0 && (vel > 0)) {
    padWasPressed.set(padIndex, true);
    currentMode.handleMidi(pads.get(padIndex), note, vel);
  }
}
