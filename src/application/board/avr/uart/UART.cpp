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

#include "board/common/indicators/Variables.h"
#include "board/common/uart/Variables.h"
#include "Constants.h"

namespace
{
    ///
    /// \brief Flag determining whether or not UART loopback functionality is enabled.
    /// When enabled, all incoming UART traffic is immediately passed on to UART TX.
    ///
    bool  loopbackEnabled[UART_INTERFACES];

    ///
    /// \brief Flag signaling that the transmission is done.
    ///
    bool  txDone[UART_INTERFACES];

    ///
    /// \brief Starts the process of transmitting the data from UART TX buffer to UART interface.
    /// @param [in] channel     UART channel on MCU.
    ///
    void uartTransmitStart(uint8_t channel)
    {
        if (channel >= UART_INTERFACES)
            return;

        txDone[channel] = false;

        switch(channel)
        {
            case 0:
            UCSRB_0 |= (1<<UDRIE_0);
            break;

            #if UART_INTERFACES > 1
            case 1:
            UCSRB_1 |= (1<<UDRIE_1);
            break;
            #endif

            default:
            break;
        }
    }
}

namespace Board
{
    namespace detail
    {
        RingBuff_t   txBuffer[UART_INTERFACES];
        RingBuff_t   rxBuffer[UART_INTERFACES];
    }

    void resetUART(uint8_t channel)
    {
        using namespace Board::detail;

        if (channel >= UART_INTERFACES)
            return;

        ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
        {
            switch(channel)
            {
                case 0:
                UCSRA_0 = 0;
                UCSRB_0 = 0;
                UCSRC_0 = 0;
                UBRR_0 = 0;
                break;

                #if UART_INTERFACES > 1
                case 1:
                UCSRA_1 = 0;
                UCSRB_1 = 0;
                UCSRC_1 = 0;
                UBRR_1 = 0;
                break;
                #endif

                default:
                break;
            }
        }

        RingBuffer_InitBuffer(&rxBuffer[channel]);
        RingBuffer_InitBuffer(&txBuffer[channel]);
    }

    void initUART(uint32_t baudRate, uint8_t channel)
    {
        if (channel >= UART_INTERFACES)
            return;

        resetUART(channel);

        int32_t baud_count = ((F_CPU / 8) + (baudRate / 2)) / baudRate;

        if ((baud_count & 1) && baud_count <= 4096)
        {
            switch(channel)
            {
                case 0:
                UCSRA_0 = (1<<U2X_0); //double speed uart
                UBRR_0 = baud_count - 1;
                break;

                #if UART_INTERFACES > 1
                case 1:
                UCSRA_1 = (1<<U2X_1); //double speed uart
                UBRR_1 = baud_count - 1;
                break;
                #endif

                default:
                break;
            }
        }
        else
        {
            switch(channel)
            {
                case 0:
                UCSRA_0 = 0;
                UBRR_0 = (baud_count >> 1) - 1;
                break;

                #if UART_INTERFACES > 1
                case 1:
                UCSRA_1 = 0;
                UBRR_1 = (baud_count >> 1) - 1;
                break;
                #endif

                default:
                break;
            }
        }

        //8 bit, no parity, 1 stop bit
        //enable receiver, transmitter and receive interrupt
        switch(channel)
        {
            case 0:
            UCSRC_0 = (1<<UCSZ1_0) | (1<<UCSZ0_0);
            UCSRB_0 = (1<<RXEN_0) | (1<<TXEN_0) | (1<<RXCIE_0) | (1<<TXCIE_0);
            break;

            #if UART_INTERFACES > 1
            case 1:
            UCSRC_1 = (1<<UCSZ1_1) | (1<<UCSZ0_1);
            UCSRB_1 = (1<<RXEN_1) | (1<<TXEN_1) | (1<<RXCIE_1) | (1<<TXCIE_1);
            break;
            #endif

            default:
            break;
        }
    }

    bool uartRead(uint8_t channel, uint8_t &data)
    {
        using namespace Board::detail;

        data = 0;

        if (channel >= UART_INTERFACES)
            return false;

        if (RingBuffer_IsEmpty(&rxBuffer[channel]))
            return false;

        data = RingBuffer_Remove(&rxBuffer[channel]);

        return true;
    }

    bool uartWrite(uint8_t channel, uint8_t data)
    {
        using namespace Board::detail;

        if (channel >= UART_INTERFACES)
            return false;

        //if both the outgoing buffer and the UART data register are empty
        //write the byte to the data register directly
        if (RingBuffer_IsEmpty(&txBuffer[channel]))
        {
            switch(channel)
            {
                case 0:
                if (UCSRA_0 & (1<<UDRE_0))
                {
                    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
                    {
                        UDR_0 = data;
                        txDone[0] = false;
                    }

                    return true;
                }
                break;

                #if UART_INTERFACES > 1
                case 1:
                if (UCSRA_1 & (1<<UDRE_1))
                {
                    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
                    {
                        UDR_1 = data;
                        txDone[1] = false;
                    }

                    return true;
                }
                break;
                #endif

                default:
                return false;
            }
        }

        while (RingBuffer_IsFull(&txBuffer[channel]));
        RingBuffer_Insert(&txBuffer[channel], data);
        uartTransmitStart(channel);

        return true;
    }

    void setUARTloopbackState(uint8_t channel, bool state)
    {
        if (channel >= UART_INTERFACES)
            return;

        loopbackEnabled[channel] = state;
    }

    bool getUARTloopbackState(uint8_t channel)
    {
        if (channel >= UART_INTERFACES)
            return false;

        return loopbackEnabled[channel];
    }

    bool isUARTtxEmpty(uint8_t channel)
    {
        if (channel >= UART_INTERFACES)
            return false;

        return txDone[channel];
    }
}

///
/// \brief ISR used to store incoming data from UART to buffer.
/// @{

ISR(USART_RX_vect_0)
{
    using namespace Board::detail;

    uint8_t data = UDR_0;

    if (!loopbackEnabled[0])
    {
        if (!RingBuffer_IsFull(&rxBuffer[0]))
        {
            RingBuffer_Insert(&rxBuffer[0], data);
            UARTreceived = true;
        }
    }
    else
    {
        if (!RingBuffer_IsFull(&txBuffer[0]))
        {
            RingBuffer_Insert(&txBuffer[0], data);
            UCSRB_0 |= (1<<UDRIE_0);
            UARTsent = true;
            UARTreceived = true;
        }
    }
}

#if UART_INTERFACES > 1
ISR(USART_RX_vect_1)
{
    using namespace Board::detail;

    uint8_t data = UDR_1;

    if (!loopbackEnabled[1])
    {
        if (!RingBuffer_IsFull(&rxBuffer[1]))
        {
            RingBuffer_Insert(&rxBuffer[1], data);
            UARTreceived = true;
        }
    }
    else
    {
        if (!RingBuffer_IsFull(&txBuffer[1]))
        {
            RingBuffer_Insert(&txBuffer[1], data);
            UCSRB_1 |= (1<<UDRIE_1);
            UARTsent = true;
            UARTreceived = true;
        }
    }
}
#endif

/// @}

///
/// \brief ISR used to write outgoing data in buffer to UART.
/// @{

ISR(USART_UDRE_vect_0)
{
    using namespace Board::detail;

    if (RingBuffer_IsEmpty(&txBuffer[0]))
    {
        //buffer is empty, disable transmit interrupt
        UCSRB_0 &= ~(1<<UDRIE_0);
    }
    else
    {
        uint8_t data = RingBuffer_Remove(&txBuffer[0]);
        UDR_0 = data;
        UARTsent = true;
    }
}

#if UART_INTERFACES > 1
ISR(USART_UDRE_vect_1)
{
    using namespace Board::detail;

    if (RingBuffer_IsEmpty(&txBuffer[1]))
    {
        //buffer is empty, disable transmit interrupt
        UCSRB_1 &= ~(1<<UDRIE_1);
    }
    else
    {
        uint8_t data = RingBuffer_Remove(&txBuffer[1]);
        UDR_1 = data;
        UARTsent = true;
    }
}
#endif

ISR(USART_TX_vect_0)
{
    txDone[0] = true;
}

#if UART_INTERFACES > 1
ISR(USART_TX_vect_1)
{
    txDone[1] = true;
}
#endif

/// @}