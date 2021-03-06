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

#include "Display.h"
#include "database/Database.h"
#include "core/src/general/Timing.h"
#include "core/src/general/BitManipulation.h"

///
/// \brief Initialize display driver and variables.
///
bool Display::init(displayController_t controller, displayResolution_t resolution, bool setHome)
{
    if (initDone)
        U8X8::clearDisplay();

    if (U8X8::initDisplay(controller, resolution))
    {
        U8X8::setPowerSave(0);
        U8X8::setFlipMode(0);

        this->resolution = resolution;

        U8X8::setFont(u8x8_font_pxplustandynewtv_r);
        U8X8::clearDisplay();

        //init char arrays
        for (int i=0; i<LCD_HEIGHT_MAX; i++)
        {
            for (int j=0; j<STRING_BUFFER_SIZE-2; j++)
            {
                lcdRowStillText[i][j] = ' ';
                lcdRowTempText[i][j] = ' ';
            }

            lcdRowStillText[i][STRING_BUFFER_SIZE-1] = '\0';
            lcdRowTempText[i][STRING_BUFFER_SIZE-1] = '\0';

            scrollEvent[i].size = 0;
            scrollEvent[i].startIndex = 0;
            scrollEvent[i].currentIndex = 0;
            scrollEvent[i].direction = scroll_ltr;
        }

        initDone = true;

        if (setHome)
            displayHome();

        return true;
    }

    return false;
}

///
/// \brief Checks if LCD requires updating continuously.
///
bool Display::update()
{
    if (!initDone)
        return false;

    if ((rTimeMs() - lastLCDupdateTime) < LCD_REFRESH_TIME)
        return false; //we don't need to update lcd in real time

    //use char pointer to point to line we're going to print
    char *charPointer;

    updateTempTextStatus();

    for (int i=0; i<LCD_HEIGHT_MAX; i++)
    {
        if (activeTextType == lcdtext_still)
        {
            //scrolling is possible only with still text
            updateScrollStatus(i);
            charPointer = lcdRowStillText[i];
        }
        else
        {
            charPointer = lcdRowTempText[i];
        }

        if (!charChange[i])
            continue;

        int8_t string_len = strlen(charPointer) > LCD_WIDTH_MAX ? LCD_WIDTH_MAX : strlen(charPointer);

        for (int j=0; j<string_len; j++)
        {
            if (BIT_READ(charChange[i], j))
                U8X8::drawGlyph(j, rowMap[resolution][i], charPointer[j+scrollEvent[i].currentIndex]);
        }

        //now fill remaining columns with spaces
        for (int j=string_len; j<LCD_WIDTH_MAX; j++)
            U8X8::drawGlyph(j, rowMap[resolution][i], ' ');

        charChange[i] = 0;
    }

    lastLCDupdateTime = rTimeMs();

    //check if midi in/out messages need to be cleared
    if (!retentionState)
    {
        for (int i=0; i<2; i++)
        {
            //0 = in, 1 = out
            if ((rTimeMs() - lastMIDIMessageDisplayTime[i] > MIDImessageRetentionTime) && midiMessageDisplayed[i])
                clearMIDIevent(static_cast<displayEventType_t>(i));
        }
    }

    return true;
}

///
/// \brief Updates text to be shown on display.
/// This function only updates internal buffers with received text, actual updating is done in update() function.
/// Text isn't passed directly, instead, value in stringBuffer is used.
/// @param [in] row             Row which is being updated.
/// @param [in] textType        Type of text to be shown on display (enumerated type). See lcdTextType_t enumeration.
/// @param [in] startIndex      Index on which received text should on specified row.
///
void Display::updateText(uint8_t row, lcdTextType_t textType, uint8_t startIndex)
{
    uint8_t size = stringBuffer.getSize();
    uint8_t scrollSize = 0;
    char* string = stringBuffer.getString();

    if (size+startIndex >= STRING_BUFFER_SIZE-2)
        size = STRING_BUFFER_SIZE-2-startIndex; //trim string

    if (directWriteState)
    {
        for (int j=0; j<size; j++)
            U8X8::drawGlyph(j+startIndex, rowMap[resolution][row], string[j]);
    }
    else
    {
        bool scrollingEnabled = false;

        switch(textType)
        {
            case lcdtext_still:
            for (int i=0; i<size; i++)
            {
                lcdRowStillText[row][startIndex+i] = string[i];
                BIT_WRITE(charChange[row], startIndex+i, 1);
            }

            //scrolling is enabled only if some characters are found after LCD_WIDTH_MAX-1 index
            for (int i=LCD_WIDTH_MAX; i<STRING_BUFFER_SIZE-1; i++)
            {
                if ((lcdRowStillText[row][i] != ' ') && (lcdRowStillText[row][i] != '\0'))
                {
                    scrollingEnabled = true;
                    scrollSize++;
                }
            }

            if (scrollingEnabled && !scrollEvent[row].size)
            {
                //enable scrolling
                scrollEvent[row].size = scrollSize;
                scrollEvent[row].startIndex = startIndex;
                scrollEvent[row].currentIndex = 0;
                scrollEvent[row].direction = scroll_ltr;

                lastScrollTime = rTimeMs();
            }
            else if (!scrollingEnabled && scrollEvent[row].size)
            {
                scrollEvent[row].size = 0;
                scrollEvent[row].startIndex = 0;
                scrollEvent[row].currentIndex = 0;
                scrollEvent[row].direction = scroll_ltr;
            }
            break;

            case lcdText_temp:
            //clear entire message first
            for (int j=0; j<LCD_WIDTH_MAX-2; j++)
                lcdRowTempText[row][j] = ' ';

            lcdRowTempText[row][LCD_WIDTH_MAX-1] = '\0';

            for (int i=0; i<size; i++)
                lcdRowTempText[row][startIndex+i] = string[i];

            //make sure message is properly EOL'ed
            lcdRowTempText[row][startIndex+size] = '\0';

            activeTextType = lcdText_temp;
            messageDisplayTime = rTimeMs();

            //update all characters on display
            for (int i=0; i<LCD_HEIGHT_MAX; i++)
                charChange[i] = static_cast<uint32_t>(0xFFFFFFFF);
            break;

            default:
            return;
        }
    }
}

///
/// \brief Enables or disables direct writing to LCD.
/// When enabled, low-level APIs are used to write text to LCD directly.
/// Otherwise, update() function takes care of updating LCD.
/// @param [in] state   New direct write state.
///
void Display::setDirectWriteState(bool state)
{
    directWriteState = state;
}

///
/// \brief Calculates position on which text needs to be set on display to be in center of display row.
/// @param [in] textSize    Size of text for which center position on display is being calculated.
/// \returns Center position of text on display.
///
uint8_t Display::getTextCenter(uint8_t textSize)
{
    return U8X8::getColumns()/2 - (textSize/2);
}

///
/// \brief Updates status of temp text on display.
///
void Display::updateTempTextStatus()
{
    if (activeTextType == lcdText_temp)
    {
        //temp text - check if temp text should be removed
        if ((rTimeMs() - messageDisplayTime) > LCD_MESSAGE_DURATION)
        {
            activeTextType = lcdtext_still;

            //make sure all characters are updated once temp text is removed
            for (int j=0; j<LCD_HEIGHT_MAX; j++)
                charChange[j] = static_cast<uint32_t>(0xFFFFFFFF);
        }
    }
}

///
/// \brief Updates status of scrolling text on display.
/// @param [in] row     Row which is being checked.
///
void Display::updateScrollStatus(uint8_t row)
{
    if (!scrollEvent[row].size)
        return;

    if ((rTimeMs() - lastScrollTime) < LCD_SCROLL_TIME)
        return;

    switch(scrollEvent[row].direction)
    {
        case scroll_ltr:
        //left to right
        scrollEvent[row].currentIndex++;

        if (scrollEvent[row].currentIndex == scrollEvent[row].size)
        {
            //switch direction
            scrollEvent[row].direction = scroll_rtl;
        }
        break;

        case scroll_rtl:
        //right to left
        scrollEvent[row].currentIndex--;

        if (scrollEvent[row].currentIndex == 0)
        {
            //switch direction
            scrollEvent[row].direction = scroll_ltr;
        }
        break;
    }

    for (int i=scrollEvent[row].startIndex; i<LCD_WIDTH_MAX; i++)
        BIT_WRITE(charChange[row], i, 1);

    lastScrollTime = rTimeMs();
}

///
/// \brief Checks for currently active text type on display.
/// \returns Active text type (enumerated type). See lcdTextType_t enumeration.
///
lcdTextType_t Display::getActiveTextType()
{
    return activeTextType;
}

///
/// \brief Updates message retention state.
/// @param [in] state   New retention state.
///
void Display::setRetentionState(bool state)
{
    retentionState = state;

    if (!state)
    {
        for (int i=0; i<2; i++)
        {
            //0 = in, 1 = out
            //make sure events are cleared immediately in next call of update()
            lastMIDIMessageDisplayTime[i] = 0;
        }
    }
}

///
/// \brief Sets new message retention time.
/// @param [in] retentionTime New retention time.
///
void Display::setRetentionTime(uint32_t retentionTime)
{
    if ((retentionTime < MIN_MESSAGE_RETENTION_TIME) || (retentionTime > MAX_MESSAGE_RETENTION_TIME))
        return;

    MIDImessageRetentionTime = retentionTime;
    //reset last update time
    lastMIDIMessageDisplayTime[displayEventIn] = rTimeMs();
    lastMIDIMessageDisplayTime[displayEventOut] = rTimeMs();
}