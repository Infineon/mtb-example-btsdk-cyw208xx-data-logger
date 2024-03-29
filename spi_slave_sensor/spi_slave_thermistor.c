/******************************************************************************
* File Name: spi_slave_thermistor.c
*
* Description: * This example demonstrates how to use SPI driver interface
* to send and receive bytes or a stream of bytes over the SPI hardware as a slave.
*
* Features demonstrated:
* - SPI WICED APIs
* - ADC sampling the analog temperature values from the on-board thermistor
*
* Requirements and Usage:
*
* Program 1 kit with the dual_spi_master app and another kit with
* spi_slave_sensor app. Connect the SPI lines and ground on the 2 kits.
* Power on the kits. You can see the logs through PUART.
*
* Hardware Connections:
*
* This example configures the following SPI functionalities in Evaluation board 
* as follows:
*
* CLK     WICED_P15    J3.8
* MISO    WICED_P14    J3.10
* MOSI    WICED_P13    J12.6
* CS      WICED_P12    J12.5
* GND
*
* Related Document: See Readme.md
*******************************************************************************/
/*******************************************************************************
* Copyright 2020-2022, Cypress Semiconductor Corporation (an Infineon company) or
* an affiliate of Cypress Semiconductor Corporation.  All rights reserved.
*
* This software, including source code, documentation and related
* materials ("Software") is owned by Cypress Semiconductor Corporation
* or one of its affiliates ("Cypress") and is protected by and subject to
* worldwide patent protection (United States and foreign),
* United States copyright laws and international treaty provisions.
* Therefore, you may use this Software only as provided in the license
* agreement accompanying the software package from which you
* obtained this Software ("EULA").
* If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
* non-transferable license to copy, modify, and compile the Software
* source code solely for use in connection with Cypress's
* integrated circuit products.  Any reproduction, modification, translation,
* compilation, or representation of this Software except as specified
* above is prohibited without the express written permission of Cypress.
*
* Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
* reserves the right to make changes to the Software without notice. Cypress
* does not assume any liability arising out of the application or use of the
* Software or any product or circuit described in the Software. Cypress does
* not authorize its products for use in any products where a malfunction or
* failure of the Cypress product may reasonably be expected to result in
* significant property damage, injury or death ("High Risk Product"). By
* including Cypress's product in a High Risk Product, the manufacturer
* of such system or application assumes all risk of such use and in doing
* so agrees to indemnify Cypress against all liability.
*******************************************************************************/

/******************************************************************************
 *                                Includes
 ******************************************************************************/
#include "sparcommon.h"
#include "wiced_bt_dev.h"
#include "wiced_bt_stack.h"
#include "wiced_bt_trace.h"
#include "wiced_hal_gpio.h"
#include "wiced_platform.h"
#include "wiced_hal_pspi.h"
#include "wiced_hal_puart.h"
#include "wiced_hal_adc.h"
#include "wiced.h"
#include "wiced_timer.h"
#include "wiced_rtos.h"
#include "wiced_thermistor.h"

/******************************************************************************
 *                                Macros
 ******************************************************************************/

/* Using SPI instance to replicate SPI Slave as sensor */
#define CLK                                 WICED_P15       /* J3.8 */
#define MISO                                WICED_P14       /* J3.10 */
#define MOSI                                WICED_P13       /* J12.6 */
#define CS                                  WICED_P12       /* J12.5 */

/* Manufacturer ID denoting Cypress Semiconductor */
#define MANUFACTURER_ID                     (0x000A)

/* Unit ID denoting temperature is in Celsius scale */
#define UNIT_ID                             (0x000B)

#define SPI                                 (SPI1)
enum
{
    SEND_MANUFACTURER_ID    =   0x01,
    SEND_UNIT               =   0x02,
    SEND_TEMPERATURE        =   0x03
};

#define PACKET_HEADER                       (0xC819)


#define SLEEP_TIMEOUT                       (1)
#define NORM_FACTOR                         (100)
#define MAX_RETRIES                         (25)
#define RESET_COUNT                         (0)

/*******************************************************************************
 *                                Structures
 ******************************************************************************/

/* pSPI data packet */
typedef struct
{
    uint16_t data ;
    uint16_t header;
}data_packet;

/*******************************************************************************
 *                                Variables Definitions
 ******************************************************************************/

thermistor_cfg_t  thermistor_cfg;
/******************************************************************************
 *                                Function Prototypes
 ******************************************************************************/
wiced_result_t      bt_cback(wiced_bt_management_evt_t event,
                             wiced_bt_management_evt_data_t *p_event_data );

static void         initialize_app(void);

static int16_t      get_ambient_temperature(void);

extern void         thermistor_init(void);

extern int16_t      thermistor_read(thermistor_cfg_t *p_thermistor_cfg);

/*******************************************************************************
 *                           Function Definitions
 ******************************************************************************/
/*******************************************************************************
* Function Name: void application_start
********************************************************************************
* Summary: Initialize transport configuration and register Bluetooth LE
*          management event callback.
*
* Parameters:
*   None
*
* Return:
*   None
*
*******************************************************************************/

APPLICATION_START()
{
    wiced_set_debug_uart( WICED_ROUTE_DEBUG_TO_PUART );

    if(WICED_SUCCESS != wiced_bt_stack_init( bt_cback, NULL, NULL ))
    {
        WICED_BT_TRACE("Bluetooth LE stack initialization failed \r\n");
    }


}

/*******************************************************************************
* Function Name: wiced_result_t bt_cback
********************************************************************************
* Summary:
*   This is a Bluetooth management event handler function to receive events from
*   Bluetooth LE stack and process as per the application.
*
* Parameters:
*   wiced_bt_management_evt_t event             :
*                             Bluetooth LE event code of one byte length
*   wiced_bt_management_evt_data_t *p_event_data:
*                             Pointer to Bluetooth LE management event structures
*
* Return:
*  wiced_result_t: Error code from WICED_RESULT_LIST or BT_RESULT_LIST
*
*******************************************************************************/

wiced_result_t bt_cback( wiced_bt_management_evt_t event,
                         wiced_bt_management_evt_data_t *p_event_data )
{
    wiced_result_t result = WICED_SUCCESS;

    switch(event)
    {
    /* BlueTooth stack enabled */
    case BTM_ENABLED_EVT:
        WICED_BT_TRACE("\r\nSample SPI Slave Application\r\n");
        initialize_app();
        break;

    default:
        break;
    }
    return result;
}

/*******************************************************************************
* Function Name: void initialize_app
********************************************************************************
* Summary:This functions initializes the SPI Slave
*
* Parameters:
*   None
*
* Return:
*   None
*
*******************************************************************************/

void initialize_app( void )
{

    data_packet     send_data;
    data_packet     rec_data;
    uint32_t        rx_fifo_count       = 0;
    uint32_t        tx_fifo_count       = 0;
    uint8_t         retries             = RESET_COUNT;
    int16_t         thermistor_reading  = 0;


    /*Initialize SPI slave*/
    wiced_hal_pspi_init( SPI,
                         SPI_SLAVE,
                         INPUT_PIN_PULL_UP,
                         SPI_PIN_CONFIG(CLK,CS,MOSI,MISO),
                         0,
                         SPI_LSB_FIRST,
                         SPI_SS_ACTIVE_LOW,
                         SPI_MODE_0,
                         CS);

    /*Enable Tx and Rx buffers*/
    wiced_hal_pspi_slave_enable_rx(SPI);
    wiced_hal_pspi_slave_enable_tx(SPI);
    // Initialize thermistor
    thermistor_cfg.high_pin = ADC_INPUT_P8;
    thermistor_init();
    WICED_BT_TRACE("Thermistor initialization done!\n\r");

    while(1)
    {
        /* Checking tx_fifo count to know if the last response was sent to
           master.*/
        tx_fifo_count = wiced_hal_pspi_slave_get_tx_fifo_count(SPI);
        if(tx_fifo_count == 0)// tell why check tx_fifo count
        {
            wiced_hal_pspi_slave_enable_rx(SPI);

            /*Check for number of bytes received*/
            rx_fifo_count = wiced_hal_pspi_slave_get_rx_fifo_count(SPI);

            if(sizeof(rec_data) <= rx_fifo_count)
            {
                if (SPIFFY_SUCCESS
                        != wiced_hal_pspi_slave_rx_data(SPI, sizeof(rec_data),
                                                        (uint8_t*) &rec_data))
                {
                    WICED_BT_TRACE("Receive failed\r\n");
                }
                wiced_hal_pspi_slave_disable_rx(SPI);

                if(rec_data.header == PACKET_HEADER)
                {
                    switch (rec_data.data)
                    {
                    case SEND_MANUFACTURER_ID:
                        WICED_BT_TRACE("Received Command:\t\t\t\t %x\r\n", rec_data.data);

                        /* Configuring send_data data packet to contain response
                           for command SEND_MANUFACTURER_ID*/
                        send_data.data = MANUFACTURER_ID;
                        send_data.header = PACKET_HEADER;
                        wiced_hal_pspi_slave_tx_data(SPI,
                                                     sizeof(send_data),
                                                     (uint8_t*) &send_data);
                        WICED_BT_TRACE("Sent Number:\t\t\t\t\t %x\r\n", send_data.data);
                        break;

                    case SEND_UNIT:
                        WICED_BT_TRACE("Received Command:\t\t\t\t %x\r\n", rec_data.data);

                        /* Configuring send_data data packet to contain response
                           for command SEND_UNIT*/
                        send_data.data = UNIT_ID;
                        send_data.header = PACKET_HEADER;
                        wiced_hal_pspi_slave_tx_data(SPI,
                                                     sizeof(send_data),
                                                     (uint8_t*) &send_data);
                        WICED_BT_TRACE("Sent Number:\t\t\t\t\t %x\r\n", send_data.data);
                        break;

                    case SEND_TEMPERATURE:
                        WICED_BT_TRACE("Received Command:\t\t\t\t %x\r\n", rec_data.data);

                        /* Configuring send_data data packet to contain response
                           for command SEND_TEMPERATURE*/
                        send_data.data = get_ambient_temperature();
                        send_data.header = PACKET_HEADER;
                        wiced_hal_pspi_slave_tx_data(SPI,
                                                     sizeof(send_data),
                                                     (uint8_t*) &send_data);
                        WICED_BT_TRACE("Sent Number:\t\t\t\t\t %x\r\n", send_data.data);
                        break;

                    default:
                        WICED_BT_TRACE("Invalid Command:\t\t\t\t %x\r\n", rec_data.data);

                    }
                }
                else
                {
                    retries++;
                    if(retries > MAX_RETRIES)
                    {

                        /* If the number of retries exceeds the maximum,
                           SPI interface is reset. This reset resolves clock
                           synchronization issues and ensures the data is
                           interpreted correctly.*/
                        retries = RESET_COUNT;
                        wiced_hal_pspi_reset(SPI);
                        wiced_hal_pspi_slave_enable_tx(SPI);
                    }
                }
            }
        }
        wiced_rtos_delay_milliseconds(SLEEP_TIMEOUT, ALLOW_THREAD_TO_SLEEP);
    }
}

/*******************************************************************************
 Function name:  get_ambient_temperature

 Function Description:
 @brief    Obtains ambient temperature from the thermistor.

 @param  void

 @return int16_t         Temperature reading from the thermistor.
 ******************************************************************************/

static int16_t get_ambient_temperature(void)
{
    volatile int16_t  temperature = 0;
    /*
     * Temperature values might vary to +/-2 degree Celsius
     */
    temperature = thermistor_read(&thermistor_cfg);
    WICED_BT_TRACE("Temperature (in degree Celsius) \t\t%d.%02d \n\r",
                  (temperature / NORM_FACTOR),
                  ABS(temperature % NORM_FACTOR));
    return temperature;
}
