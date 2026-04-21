/*
 * Copyright (c) 2026 Paul Barnard (Toxic Celery)
 */

#include "TCA9554PWR.h"

/*****************************************************  Operation register REG   ****************************************************/
uint8_t Read_REG(uint8_t REG)
{
    uint8_t bitsStatus = 0;
    I2C_Read(TCA9554_ADDRESS, REG, &bitsStatus, 1);
    return bitsStatus;
}

void Write_REG(uint8_t REG, uint8_t Data)
{
    I2C_Write(TCA9554_ADDRESS, REG, &Data, 1);
}

/********************************************************** Set EXIO mode **********************************************************/
void Mode_EXIO(uint8_t Pin, uint8_t State)
{
    uint8_t bitsStatus = Read_REG(TCA9554_CONFIG_REG);
    uint8_t Data = (0x01 << (Pin-1)) | bitsStatus;
    Write_REG(TCA9554_CONFIG_REG, Data);
}

void Mode_EXIOS(uint8_t PinState)
{
    Write_REG(TCA9554_CONFIG_REG, PinState);
}

/********************************************************** Read EXIO status **********************************************************/
uint8_t Read_EXIO(uint8_t Pin)
{
    uint8_t inputBits = Read_REG(TCA9554_INPUT_REG);
    uint8_t bitStatus = (inputBits >> (Pin-1)) & 0x01;
    return bitStatus;
}

uint8_t Read_EXIOS(void)
{
    uint8_t inputBits = Read_REG(TCA9554_INPUT_REG);
    return inputBits;
}

/********************************************************** Set the EXIO output status **********************************************************/
void Set_EXIO(uint8_t Pin, bool State)
{
    uint8_t Data = 0;
    uint8_t bitsStatus = Read_REG(TCA9554_OUTPUT_REG);
    if (Pin < 9 && Pin > 0) {
        if (State)
            Data = (0x01 << (Pin-1)) | bitsStatus;
        else
            Data = (~(0x01 << (Pin-1)) & bitsStatus);
        Write_REG(TCA9554_OUTPUT_REG, Data);
    } else {
        printf("Parameter error, please enter the correct parameter!\r\n");
    }
}

void Set_EXIOS(uint8_t PinState)
{
    Write_REG(TCA9554_OUTPUT_REG, PinState);
}

/********************************************************** Flip EXIO state **********************************************************/
void Set_Toggle(uint8_t Pin)
{
    uint8_t bitsStatus = Read_EXIO(Pin);
    Set_EXIO(Pin, (bool)!bitsStatus);
}

/********************************************************* TCA9554PWR Initializes the device ***********************************************************/
void TCA9554PWR_Init(uint8_t PinState)
{
    Mode_EXIOS(PinState);
}

esp_err_t EXIO_Init(void)
{
    TCA9554PWR_Init(0x00);
    return ESP_OK;
}
