#include "MIDI.h"
#include "..\sysex/SysEx.h"
#include "..\eeprom/EEPROMsettings.h"
#include "..\sysex/ProtocolDefinitions.h"
#include "..\BitManipulation.h"
#include "..\midi\usb_midi\USBmidi.h"

typedef enum {

    noteChannel,
    programChangeChannel,
    CCchannel,
    HwMIDIpitchBendChannel,
    inputChannel,
    NUMBER_OF_CHANNELS

} channels;

typedef enum {

    midiFeatureConf,
    midiChannelConf,
    MIDI_SUBTYPES

} sysExMessageSubtypeMIDI;

typedef enum {

    midiFeatureStandardNoteOff,
    midiFeatureRunningStatus,
    midiFeatureUSBconvert,
    MIDI_FEATURES

} midiFeaturesParameters;

subtype midiFeatureSubtype  = { MIDI_FEATURES, 0, 1 };
subtype midiChannelSubtype  = { NUMBER_OF_CHANNELS, 1, 16 };

const uint8_t midiSubtypeArray[] = {

    midiFeatureConf,
    midiChannelConf

};

const uint8_t midiParametersArray[] = {

    midiFeatureSubtype.parameters,
    midiChannelSubtype.parameters

};

const uint8_t midiNewParameterLowArray[] = {

    midiFeatureSubtype.lowValue,
    midiChannelSubtype.lowValue

};

const uint8_t midiNewParameterHighArray[] = {

    midiFeatureSubtype.highValue,
    midiChannelSubtype.highValue

};

MIDI::MIDI()    {

    sendSysExCallback   = NULL;
    sendNoteCallback    = NULL;

}

void MIDI::init() {

    sysEx.addMessageType(SYS_EX_MT_MIDI, MIDI_SUBTYPES);

    for (int i=0; i<MIDI_SUBTYPES; i++)   {

        //define subtype messages
        sysEx.addMessageSubType(SYS_EX_MT_MIDI, midiSubtypeArray[i], midiParametersArray[i], midiNewParameterLowArray[i], midiNewParameterHighArray[i]);

    }

    uint8_t inChannel = getMIDIchannel(inputChannel);
    //read incoming MIDI messages on specified channel
    hwMIDI.begin(inChannel);
    usbMIDI.begin(inChannel);

}

uint8_t MIDI::getParameter(uint8_t messageType, uint8_t parameterID)  {

    switch(messageType) {

        case midiFeatureConf:
        return getFeature(parameterID);
        break;

        case midiChannelConf:
        return getMIDIchannel(parameterID);
        break;

    }   return INVALID_VALUE;
}

bool MIDI::setParameter(uint8_t messageType, uint8_t parameterID, uint8_t newValue) {

    int16_t address;
    uint8_t featuresArray;

    switch(messageType) {

        case midiFeatureConf:
        address = EEPROM_FEATURES_MIDI;
        featuresArray = eeprom_read_byte((uint8_t*)address);
        if (newValue == RESET_VALUE)    bitWrite(featuresArray, parameterID, bitRead(pgm_read_byte(&(defConf[address])), parameterID));
        else                            bitWrite(featuresArray, parameterID, newValue);

        eeprom_update_byte((uint8_t*)address, featuresArray);
        return (featuresArray == eeprom_read_byte((uint8_t*)EEPROM_FEATURES_MIDI));
        break;

        case midiChannelConf:
        address = eepromMIDIchannelArray[parameterID];
        if (newValue == RESET_VALUE)
            eeprom_update_byte((uint8_t*)address, pgm_read_byte(&(defConf[address])));
        else eeprom_update_byte((uint8_t*)address, newValue);
        return (newValue == getMIDIchannel(parameterID));
        break;

    }   return false;

}

void MIDI::checkInput()   {

    hwMIDI.sendNoteOn(10,127,1);

    if (usbMIDI.read())   {   //new message on usb

        uint8_t messageType = usbMIDI.getType();
        source = usbSource;

        switch(messageType) {

            case midiMessageSystemExclusive:
            sendSysExCallback(usbMIDI.getSysExArray(), usbMIDI.getData1());
            lastSysExMessageTime = rTimeMillis();
            break;

            case midiMessageNoteOff:
            case midiMessageNoteOn:
            sendNoteCallback(usbMIDI.getData1(), usbMIDI.getData2());
            break;

        }

    }

    //check for incoming MIDI messages on USART
    if (hwMIDI.read())    {

        uint8_t messageType = hwMIDI.getType();
        uint8_t data1 = hwMIDI.getData1();
        uint8_t data2 = hwMIDI.getData2();
        uint8_t sysExArraySize;
        uint8_t sysExArray[MIDI_SYSEX_ARRAY_SIZE];

        source = midiSource;

        if (!getFeature(midiFeatureUSBconvert))  {

            switch(messageType) {

                case midiMessageNoteOff:
                case midiMessageNoteOn:
                sendNoteCallback(hwMIDI.getData1(), hwMIDI.getData2());
                break;

                case midiMessageSystemExclusive:
                sendSysExCallback(hwMIDI.getSysExArray(), hwMIDI.getSysExArrayLength());
                break;

            }

        }   else {

                //dump everything from MIDI in to USB MIDI out
                switch(messageType) {

                    case midiMessageNoteOff:
                    usbMIDI.sendNoteOn(data1, data2, getMIDIchannel(inputChannel));
                    break;

                    case midiMessageNoteOn:
                    usbMIDI.sendNoteOn(data1, data2, getMIDIchannel(inputChannel));
                    break;

                    case midiMessageControlChange:
                    usbMIDI.sendControlChange(data1, data2, getMIDIchannel(inputChannel));
                    break;

                    case midiMessageProgramChange:
                    usbMIDI.sendProgramChange(data1, getMIDIchannel(inputChannel));
                    break;

                    case midiMessageSystemExclusive:
                    //to-do
                    break;

                    case midiMessageAfterTouchChannel:
                    usbMIDI.sendAfterTouch(data1, getMIDIchannel(inputChannel));
                    break;

                    case midiMessagePitchBend:
                    //to-do
                    break;

                }

        }

    }

    //disable sysex config after inactivity
    if (rTimeMillis() - lastSysExMessageTime > CONFIG_TIMEOUT)
        sysEx.disableConf();

}

bool MIDI::getFeature(uint8_t featureID)  {

    uint8_t features = eeprom_read_byte((uint8_t*)EEPROM_FEATURES_MIDI);
    return bitRead(features, featureID);

}

void MIDI::sendMIDInote(uint8_t buttonNote, bool buttonState, uint8_t _velocity)  {

    uint8_t channel = getMIDIchannel(noteChannel);

    switch (buttonState) {

        case false:
        //button released
        if (getFeature(midiFeatureStandardNoteOff))   {

            usbMIDI.sendNoteOff(buttonNote, _velocity, channel);
            hwMIDI.sendNoteOff(buttonNote, _velocity, channel);

        } else {

            usbMIDI.sendNoteOn(buttonNote, _velocity, channel);
            hwMIDI.sendNoteOn(buttonNote, _velocity, channel);

        }
        break;

        case true:
        //button pressed
        usbMIDI.sendNoteOn(buttonNote, _velocity, channel);
        hwMIDI.sendNoteOn(buttonNote, _velocity, channel);
        break;

    }

}

void MIDI::sendProgramChange(uint8_t program)    {

    uint8_t channel = getMIDIchannel(programChangeChannel);
    usbMIDI.sendProgramChange(program, channel);
    hwMIDI.sendProgramChange(program, channel);

}

void MIDI::sendControlChange(uint8_t ccNumber, uint8_t ccValue) {

    uint8_t channel = getMIDIchannel(CCchannel);
    usbMIDI.sendControlChange(ccNumber, ccValue, channel);
    hwMIDI.sendControlChange(ccNumber, ccValue, channel);

}

void MIDI::sendSysEx(uint8_t *sysExArray, uint8_t arraySize)   {

    switch (source) {

        case midiSource:
        hwMIDI.sendSysEx(arraySize, sysExArray, false);
        break;

        case usbSource:
        usbMIDI.sendSysEx(arraySize, sysExArray, false);
        break;

    }

}

bool MIDI::setMIDIchannel(uint8_t channel, uint8_t channelNumber)  {

    eeprom_update_byte((uint8_t*)((int16_t)eepromMIDIchannelArray[channel]), channelNumber);
    return (channelNumber == eeprom_read_byte((uint8_t*)((int16_t)eepromMIDIchannelArray[channel])));

}

uint8_t MIDI::getMIDIchannel(uint8_t channel)  {

    return eeprom_read_byte((uint8_t*)((int16_t)eepromMIDIchannelArray[channel]));

}

void MIDI::setHandleSysEx(void(*fptr)(uint8_t sysExArray[], uint8_t arraySize))    {

    sendSysExCallback = fptr;

}

void MIDI::setHandleNote(void(*fptr)(uint8_t note, uint8_t noteVelocity))    {

    sendNoteCallback = fptr;

}

MIDI midi;