/*
    Copyright 2016-2017 StapleButter

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#include <stdio.h>
#include "NDS.h"
#include "SPI.h"


namespace SPI_Firmware
{

u8* Firmware;
u32 FirmwareLength;

u32 Hold;
u8 CurCmd;
u32 DataPos;
u8 Data;

u8 StatusReg;
u32 Addr;


u16 CRC16(u8* data, u32 len, u32 start)
{
    u16 blarg[8] = {0xC0C1, 0xC181, 0xC301, 0xC601, 0xCC01, 0xD801, 0xF001, 0xA001};

    for (u32 i = 0; i < len; i++)
    {
        start ^= data[i];

        for (int j = 0; j < 8; j++)
        {
            if (start & 0x1)
            {
                start >>= 1;
                start ^= (blarg[j] << (7-j));
            }
            else
                start >>= 1;
        }
    }

    return start & 0xFFFF;
}

bool VerifyCRC16(u32 start, u32 offset, u32 len, u32 crcoffset)
{
    u16 crc_stored = *(u16*)&Firmware[crcoffset];
    u16 crc_calced = CRC16(&Firmware[offset], len, start);
    //printf("%04X vs %04X\n", crc_stored, crc_calced);
    return (crc_stored == crc_calced);
}


void Init()
{
    Firmware = NULL;
}

void Reset()
{
    if (Firmware) delete[] Firmware;

    FILE* f = fopen("firmware.bin", "rb");
    fseek(f, 0, SEEK_END);
    FirmwareLength = (u32)ftell(f);
    Firmware = new u8[FirmwareLength];

    fseek(f, 0, SEEK_SET);
    fread(Firmware, FirmwareLength, 1, f);

    fclose(f);

    // verify shit
    printf("FW: WIFI CRC16 = %s\n", VerifyCRC16(0x0000, 0x2C, *(u16*)&Firmware[0x2C], 0x2A)?"GOOD":"BAD");
    printf("FW: AP1 CRC16 = %s\n", VerifyCRC16(0x0000, 0x3FA00, 0xFE, 0x3FAFE)?"GOOD":"BAD");
    printf("FW: AP2 CRC16 = %s\n", VerifyCRC16(0x0000, 0x3FB00, 0xFE, 0x3FBFE)?"GOOD":"BAD");
    printf("FW: AP3 CRC16 = %s\n", VerifyCRC16(0x0000, 0x3FC00, 0xFE, 0x3FCFE)?"GOOD":"BAD");
    printf("FW: USER0 CRC16 = %s\n", VerifyCRC16(0xFFFF, 0x3FE00, 0x70, 0x3FE72)?"GOOD":"BAD");
    printf("FW: USER1 CRC16 = %s\n", VerifyCRC16(0xFFFF, 0x3FF00, 0x70, 0x3FF72)?"GOOD":"BAD");

    Hold = 0;
    CurCmd = 0;
    Data = 0;
    StatusReg = 0x00;
}

u8 Read()
{
    return Data;
}

void Write(u8 val, u32 hold)
{
    if (!hold)
    {
        Hold = 0;
    }

    if (hold && (!Hold))
    {
        CurCmd = val;
        Hold = 1;
        Data = 0;
        DataPos = 1;
        Addr = 0;
        //printf("firmware SPI command %02X\n", CurCmd);
        return;
    }

    switch (CurCmd)
    {
    case 0x03: // read
        {
            if (DataPos < 4)
            {
                Addr <<= 8;
                Addr |= val;
                Data = 0;

                //if (DataPos == 3) printf("firmware SPI read %08X\n", Addr);
            }
            else
            {
                if (Addr >= FirmwareLength)
                    Data = 0;
                else
                    Data = Firmware[Addr];

                Addr++;
            }

            DataPos++;
        }
        break;

    case 0x04: // write disable
        StatusReg &= ~(1<<1);
        Data = 0;
        break;

    case 0x05: // read status reg
        Data = StatusReg;
        break;

    case 0x06: // write enable
        StatusReg |= (1<<1);
        Data = 0;
        break;

    case 0x9F: // read JEDEC ID
        {
            switch (DataPos)
            {
            case 1: Data = 0x20; break;
            case 2: Data = 0x40; break;
            case 3: Data = 0x12; break;
            default: Data = 0; break;
            }
            DataPos++;
        }
        break;

    default:
        printf("unknown firmware SPI command %02X\n", CurCmd);
        break;
    }
}

}


namespace SPI
{

u16 CNT;

u32 CurDevice;


void Init()
{
    SPI_Firmware::Init();
}

void Reset()
{
    CNT = 0;

    SPI_Firmware::Reset();
}


u16 ReadCnt()
{
    return CNT;
}

void WriteCnt(u16 val)
{
    CNT = val & 0xCF03;
    if (val & 0x0400) printf("!! CRAPOED 16BIT SPI MODE\n");
}

u8 ReadData()
{
    if (!(CNT & (1<<15))) return 0;

    switch (CNT & 0x0300)
    {
    case 0x0100: return SPI_Firmware::Read();
    default: return 0;
    }
}

void WriteData(u8 val)
{
    if (!(CNT & (1<<15))) return;

    // TODO: take delays into account

    switch (CNT & 0x0300)
    {
    case 0x0100: SPI_Firmware::Write(val, CNT&(1<<11)); break;
    default: printf("SPI to unknown device %04X %02X\n", CNT, val); break;
    }

    if (CNT & (1<<14))
        NDS::TriggerIRQ(1, NDS::IRQ_SPI);
}

}
