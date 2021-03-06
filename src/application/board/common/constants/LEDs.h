/*

Copyright 2015-2019 Igor Petrovic

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

*/

#pragma once

#include "core/src/HAL/avr/PinManipulation.h"

///
/// \brief Time in milliseconds during which MIDI event indicators on board are on when MIDI event happens.
///
#define MIDI_INDICATOR_TIMEOUT      50

///
/// \brief Time in milliseconds for single startup animation cycle on built-in LED indicators.
///
#define LED_INDICATOR_STARTUP_DELAY 150

///
/// \brief Helper macros used for easier control of internal (on-board) and external LEDs.
/// @{

#ifdef LED_INT_INVERT
#define INT_LED_ON(port, pin)       setLow(port, pin)
#define INT_LED_OFF(port, pin)      setHigh(port, pin)
#else
#define INT_LED_ON(port, pin)       setHigh(port, pin)
#define INT_LED_OFF(port, pin)      setLow(port, pin)
#endif

#ifdef LED_EXT_INVERT
#define EXT_LED_ON(port, pin)       setLow(port, pin)
#define EXT_LED_OFF(port, pin)      setHigh(port, pin)
#else
#define EXT_LED_ON(port, pin)       setHigh(port, pin)
#define EXT_LED_OFF(port, pin)      setLow(port, pin)
#endif

/// @}

#define FADE_TIME_MIN               0
#define FADE_TIME_MAX               10