#include "daisy_versio.h"
#include "daisysp.h"
#include <string>
#include <vector>
#include <random>
#include "Note.hpp"
#include "Scales.hpp"
#include "utils.hpp"
#include <time.h> 

using namespace daisy;
using namespace daisysp;

DaisyVersio hw;

enum sequenceMutationOptions {
    DONT_MUTATE,
    CHANGE_NOTES,
    OCTAVE_UP,
    OCTAVE_DOWN,
    MUTATION_OPTIONS_LEN
};
enum newMelodyNoteOptions {
    NM_REPEAT,
    NM_UP,
    NM_DOWN,
    NM_NEW
};
enum oscillatorType {
    OSC_RINGS,
    OSC_FORMANT
};


//Clock trigger
bool triggerState = false;
bool prevTriggerState = false;
float triggerValue;

//New melody CV trigger
bool triggerStateNewMelody = false;
bool prevTriggerStateNewMelody = false;
float triggerValueNewMelody;

//boolean options
bool regen = true;
bool mutate = true;
bool accumulate = true;

//The sequence
static const int maxSteps = 32;
Note sequence[maxSteps]; 

//Melody creation
int noteOptionWeights[4] = {5,5,5,10};
std::string baseKey = "C";
std::string scale = "Major";
uint8_t scaleNum = 1;
uint8_t baseOctave = 4;
Note rootNote = Note(baseKey,baseOctave);
std::vector<int> validTones = scaleTones.at(scale);
std::vector<int> validToneWeights = scaleToneWeights.at(scale);


bool gateState = false;
uint8_t repetitionCount = 0;
uint8_t restingCount = 0;
int octaveOffset = 0;
uint8_t mutationOption = DONT_MUTATE;
uint8_t repeatsInARow = 0;
uint8_t maxOctaveOffsetUp = 2;
uint8_t maxOctaveOffsetDown = 2;
int sequenceLength = 16;
uint8_t sequenceGap = 0;
double restProbability = 20; //out of 100
float octaveChangeProbability = 20;
float noteChangeProbability = 20;
uint8_t currentNote = 0;
float gateDuration = .05f;
uint8_t activeLED = 0;
float LEDbrightness = 0;
bool downOnly, upOnly, upDown;
double trigToTrigTime = 0;
double prevTrigTime = 0;
double prevFrame = 0;
double currentFrame = 0;
int numRecentTriggers = 0;
double triggerGapAccumulator = 0;
double triggerGapAverage = 0;
double triggerGapSeconds = 0;
static const int numTriggersToAverage = 6;
float gateLengthKnobPosition;
float octaveCV;
float scaleCV;
float gateLengthCV;
float noteChangeCV;
float lambdaCV;
float densityCV;
float modulateCV;
float lengthCV;
uint8_t poisson_lambda = 12;
Note prevNote; 
int noteKind;
int noteOn;
int numChannels;
float currentNoteVoltage = 0;
float sampleRate;
bool oscControls;
float formant_freq_factor;
int selectedOsc;

//Set up voices
Pluck string_osc;
FormantOscillator formant_osc;

float string_trig = 0.f;
float string_buffer[1000];
int string_npt = 1000;


Note getRandomNote() {

    //Choose a random note from the current scale

    int ourSemitone;
    int newNoteMIDI;
    int ourChoice;

    int num_choices = validToneWeights.size();
    ourChoice = weightedRandom(&validToneWeights[0],num_choices);

    ourSemitone = validTones[ourChoice];
    newNoteMIDI = rootNote.noteNumMIDI + ourSemitone - 1;
    Note newNote = Note(newNoteMIDI);
    return newNote;

};

void changeOctave(int octaveChange) {
    
    //transpose by octave

    //Change lights to yellow
    hw.SetLed(0,1,1,0);
    hw.SetLed(1,1,1,0);
    hw.SetLed(2,1,1,0);
    hw.SetLed(3,1,1,0);
    hw.UpdateLeds();
    
    hw.seed.PrintLine("---------Octave change---------");
    if (octaveOffset+octaveChange > maxOctaveOffsetUp) {

    } else if (octaveOffset+octaveChange <  -1*maxOctaveOffsetDown) {

    } else {

        for (int t=0; t < maxSteps; ++t) {
            sequence[t].octave += octaveChange;
            sequence[t].setVoltage();
            sequence[t].setMIDInum();

        }
        octaveOffset += octaveChange;

    }

}

void changeNotes(int amount) {
    
    hw.seed.PrintLine("---------Note change---------");
    //substitute notes in the melody with new notes

    //Change lights to purple
    hw.SetLed(0,1,0,1);
    hw.SetLed(1,1,0,1);
    hw.SetLed(2,1,0,1);
    hw.SetLed(3,1,0,1);
    hw.UpdateLeds();

    int noteToChange = std::rand() % sequenceLength;
    Note newNote = getRandomNote();
    sequence[noteToChange] = newNote;

}


void newMelody() {

    //Generate a new melody sequence

    hw.seed.PrintLine("Will make new melody");

    //Flash LEDs white
    hw.SetLed(0,1,1,1);
    hw.SetLed(1,1,1,1);
    hw.SetLed(2,1,1,1);
    hw.SetLed(3,1,1,1);
    hw.UpdateLeds();

    scale = scaleNames.at(scaleNum);
    validTones = scaleTones.at(scale);
    validToneWeights = scaleToneWeights.at(scale);
    octaveOffset = 0;
    repetitionCount = 0;

    for (int x = 0; x < maxSteps; x++ ){
        
        if (x>0) {

            //Use rest probability (density?) to decide if we have a rest
            int noteOnChoice = std::rand() % 100;
            if (noteOnChoice < restProbability) {
                noteOn = 0;
            } else {
                noteOn = 1;
            }

        } else {
            //first note is never a rest
            noteOn = 1;
        }
        if (noteOn) {

            //We have an actual note, not a rest
            //Decide what kind of note
            //First note is always a new random note
            
            if (x>0){
                noteKind = weightedRandom(noteOptionWeights,4);
        
            } else {
                noteKind = NM_NEW;
            }
            
            if (noteKind == NM_REPEAT) {
                sequence[x] = prevNote;

            } else if (noteKind == NM_DOWN) {

                //find tone of previous note in the scale, find index of toneNum in validTones
                std::vector<int>::iterator it = std::find(validTones.begin(),validTones.end(),prevNote.toneNum);
                int toneIndex = std::distance(validTones.begin(), it);

                toneIndex--;
                int newOctave = prevNote.octave;
                if (toneIndex < 0) {
                    toneIndex = validTones.size()-1;
                    newOctave--;
                }
                int newTone = validTones[toneIndex];
            
                std::string newNoteName = prevNote.getNoteNameFromNum(newTone);

                Note aNewNote = Note(newNoteName,newOctave);
                sequence[x] = aNewNote;

            } else if (noteKind == NM_UP) {

                //find tone of previous note in the scale, find index of toneNum in validTones
                std::vector<int>::iterator it = std::find(validTones.begin(),validTones.end(),prevNote.toneNum);
                int toneIndex = std::distance(validTones.begin(), it);

                toneIndex++;
                int newOctave = prevNote.octave;
                if (toneIndex >= int(validTones.size())) {
                    toneIndex = 0;
                    newOctave++;
                }
                int newTone = validTones[toneIndex];
            
                std::string newNoteName = prevNote.getNoteNameFromNum(newTone);

                Note aNewNote = Note(newNoteName,newOctave);
                sequence[x] = aNewNote;


            } else if (noteKind == NM_NEW) {
                Note newNote = getRandomNote();
                sequence[x] = newNote;
                prevNote = newNote;
            }
            prevNote = sequence[x];

        } else {
            //We have a rest beat
            Note newNote = Note("rest");
            sequence[x] = newNote;
        }

    }

};


void doStep() {


    //Trigger has been received, we execute the next step of the sequence

	bool melodyChanged = false;

    //turn all lights off
    for (int i = 0; i <= 3; ++i) {
        hw.SetLed(i,0,0,0);
        hw.leds[i].Update();
    }
    //but turn the next one on
    LEDbrightness = float(repetitionCount)/float(poisson_lambda);
    LEDbrightness = LEDbrightness/0.5;
    if (LEDbrightness < .2) {LEDbrightness = 0.2f;};
    if (currentNote == 0){
        hw.SetLed(0,0,1,0);
    } else {
        if (accumulate && mutate) {
            hw.SetLed(activeLED,0,0,LEDbrightness);
            hw.leds[activeLED].Update();
        } else if (mutate && !accumulate) {
            hw.SetLed(activeLED,0,0.33,LEDbrightness);
            hw.leds[activeLED].Update();
        } else {
            hw.SetLed(activeLED,0.16,0.17,LEDbrightness);
            hw.leds[activeLED].Update();
        }
    }

    activeLED+=1;
	if (activeLED > 3) {activeLED=0;}

    Note noteToPlay = sequence[currentNote];
    
    if (noteToPlay.noteName != "rest") {
        string_osc.SetFreq(mtof(noteToPlay.noteNumMIDI));
        string_trig = 1.0f;
        string_osc.Process(string_trig);
        formant_osc.SetCarrierFreq(mtof(noteToPlay.noteNumMIDI));
        formant_osc.SetFormantFreq(mtof(noteToPlay.noteNumMIDI) * formant_freq_factor * 4);
        currentNoteVoltage = noteToPlay.voltage;
    }

    ++currentNote;

    if (currentNote >= sequenceLength) {
			
        //We are at end of loop, so we need to check for various changes
        //accumulate means we are accruing repetitions
        if (accumulate) {
            repetitionCount++;

            //decide if we get a new melody
            int choice = std::rand() % 100;
            float criterion = float(repetitionCount)/float(poisson_lambda) * 50;
            
            if (choice < criterion) {
                newMelody();
                melodyChanged = true;
            } else {
                melodyChanged = false;
            }
        }

        if (!melodyChanged && mutate) {

            int octaveChoice = std::rand() % 100;
            if (octaveChoice < octaveChangeProbability) {
                
                if (downOnly) {
                    changeOctave(-1);
                } else if (upOnly) {
                    changeOctave(1);
                } else {
                    int coinFlip = std::rand() %100;
                    if (coinFlip < 50) {
                        if (octaveOffset <=  -1*maxOctaveOffsetDown) {
                            //We're already at the min octave so we bounce up
                            changeOctave(1);
                        } else {
                            changeOctave(-1);
                        }
                    } else {
                        if (octaveOffset >= maxOctaveOffsetUp) {
                            //We're already at the max octave so we bounce down
                            changeOctave(-1);
                        } else {
                            changeOctave(1);
                        }
                    }
                }


            } else {
            
                int noteChoice = std::rand() % 100;
                if (noteChoice < noteChangeProbability) {
                    changeNotes(1);
                }
    
            };

        
        }
        currentNote = 0;
        activeLED = 0;
    }

};


int processKnobValue(float value, int maxValue) {

    float voltsPerNum = 1.0/maxValue;
    float rawVal = value/voltsPerNum;
    return std::ceil(rawVal);

}

static void AudioCallback(AudioHandle::InputBuffer  in,
                          AudioHandle::OutputBuffer out,
                          size_t                    size)
{
    
    //We are looking for a clock trigger on the left audio channel
    bool highValue = false;

    float threshold = 0.5;
    for (size_t i =0; i < size; ++i) {
        if (in[0][i] > threshold) {
            highValue = true;
        }
    }
    triggerState = highValue;

    if (triggerState && !prevTriggerState) {

        //Calculate time since last trigger to adjust gate times
        //(Output gate is a fraction of clock time)
        currentFrame = daisy::System::GetUs();
        trigToTrigTime = currentFrame - prevFrame;
        prevFrame = currentFrame;
        triggerGapAccumulator += trigToTrigTime;
        numRecentTriggers += 1;
        triggerGapAverage = triggerGapAccumulator/numRecentTriggers;
        triggerGapSeconds = triggerGapAverage / 1000000 ;

        //Adjust gate
        gateDuration = gateLengthKnobPosition * triggerGapSeconds;

        if (numRecentTriggers > numTriggersToAverage) {
            numRecentTriggers = 0;
            triggerGapAccumulator = 0;
        }

       doStep();
    }

    prevTriggerState = triggerState;

    //Process outgoing audio
    for (size_t i = 0; i < size; ++i) {

        float sig; 

        if (sequence[currentNote].noteName != "rest") {
            if (selectedOsc == OSC_RINGS) {
                string_trig = 0.f;
                sig = string_osc.Process(string_trig);
            } else {
                sig = formant_osc.Process();
            }
            OUT_L[i] = sig;
            OUT_R[i] = sig;
        } else {
            
            OUT_L[i] = 0;
            OUT_R[i] = 0;
        }
      
    }
  
}


int main(void)
{

    //Seed random number generator
    srand (time(NULL));

    // Initialize Versio hardware and start audio, ADC
    hw.Init();
    hw.seed.StartLog(false);
    hw.StartAdc();

    hw.seed.PrintLine("Logging enabled.");

    hw.SetLed(0,0,1,1);
    hw.SetLed(1,0,1,1);
    hw.SetLed(2,0,1,1);
    hw.SetLed(3,0,1,1);
    hw.UpdateLeds();

    sampleRate = hw.seed.AudioSampleRate();
    
    newMelody();

    //Initialize oscillators
    string_osc.Init(sampleRate,string_buffer,string_npt,daisysp::PLUCK_MODE_RECURSIVE);
    string_osc.SetAmp(0.5f);
    string_osc.SetDecay(0.33f);
    string_osc.SetDamp(0.5f);
    formant_osc.Init(sampleRate);
    formant_osc.SetFormantFreq(0.5f);

    hw.StartAudio(AudioCallback);

    while(1)
    {
        hw.ProcessAllControls(); // Normalize CV inputs
        hw.tap.Debounce();
        hw.UpdateLeds();

        //Handle button press
        if (hw.tap.RisingEdge()) {
                hw.seed.PrintLine("Button pressed.");
                newMelody();
        }

        //Process threeway switches

        //Top stwitch controls melody lock
        if (hw.sw[0].Read() == hw.sw->POS_LEFT) {
            mutate = true;
            accumulate = true;
        } else if (hw.sw[0].Read() == hw.sw->POS_CENTER) {
            mutate = true;
            accumulate = false;
        } else if (hw.sw[0].Read() == hw.sw->POS_RIGHT) {
            mutate = false;
            accumulate = false;
        }

        //Bottom switch controls active voice and determines which
        //params the knobs are controlling
        if (hw.sw[1].Read() == hw.sw->POS_LEFT) {

            oscControls = true;
            selectedOsc = OSC_FORMANT;
            formant_freq_factor = hw.GetKnobValue(DaisyVersio::KNOB_5);
            formant_osc.SetPhaseShift(hw.GetKnobValue(DaisyVersio::KNOB_6));
            gateLengthKnobPosition = hw.GetKnobValue(DaisyVersio::KNOB_3);

        } else if (hw.sw[1].Read() == hw.sw->POS_CENTER) {

            oscControls = false;
            gateLengthKnobPosition = hw.GetKnobValue(DaisyVersio::KNOB_3);
            octaveChangeProbability = hw.GetKnobValue(DaisyVersio::KNOB_5) * 100;
            noteChangeProbability = hw.GetKnobValue(DaisyVersio::KNOB_6) * 100;

        } else if (hw.sw[1].Read() == hw.sw->POS_RIGHT) {
        
            oscControls = true;
            selectedOsc = OSC_RINGS;
            string_osc.SetAmp(hw.GetKnobValue(DaisyVersio::KNOB_5));
            string_osc.SetDecay(hw.GetKnobValue(DaisyVersio::KNOB_3));
            string_osc.SetDamp(hw.GetKnobValue(DaisyVersio::KNOB_6));

        }

		//Process knobs
        sequenceLength = processKnobValue(hw.GetKnobValue(DaisyVersio::KNOB_0),maxSteps);
        scaleNum = processKnobValue(hw.GetKnobValue(DaisyVersio::KNOB_1),scaleNames.size());
        poisson_lambda = processKnobValue(hw.GetKnobValue(DaisyVersio::KNOB_2),50);
        restProbability = 100 - hw.GetKnobValue(DaisyVersio::KNOB_4) * 100;

        //Gate on the FSU input triggers new melody
        triggerStateNewMelody = hw.Gate();
        if (triggerStateNewMelody && !prevTriggerStateNewMelody) {
            hw.seed.PrintLine("FSU Gate received");
            newMelody();
            
        }
        prevTriggerStateNewMelody = triggerStateNewMelody;
   
    }
}