/*
 * korg_syro_func.h - KORG SYRO SDK
 * Copyright (C) 2014, KORG Inc. All rights reserved.
 */
#ifndef KORG_SYRO_FUNC_H__
#define KORG_SYRO_FUNC_H__

#include <stdint.h>

#ifndef bool
typedef int bool;
#endif
#ifndef true
#define true (1)
#endif
#ifndef false
#define false (0)
#endif

#define KORGSYRO_NUM_CHANNEL	2

#define KORGSYRO_FSK0_CYCLE	20
#define KORGSYRO_FSK1_CYCLE	10
#define KORGSYRO_QAM_CYCLE	8
#define KORGSYRO_NUM_CYCLE	2
#define KORGSYRO_NUM_CYCLE_BUF	(KORGSYRO_QAM_CYCLE * KORGSYRO_NUM_CYCLE)

typedef struct
{
  int16_t       CycleSample[KORGSYRO_NUM_CYCLE_BUF];
  int           LastPhase;
  int32_t       Lpf_z;
} SyroChannel;

uint16_t      SyroFunc_CRC16_nrm( uint8_t * pSrc, int size );
uint16_t      SyroFunc_CRC16_rev( uint8_t * pSrc, int size );
uint32_t      SyroFunc_CalculateEcc( uint8_t * pSrc, int size );
void          SyroFunc_GenerateSingleCycle( SyroChannel * psc,
		int write_page, uint8_t dat, bool block );
void          SyroFunc_MakeGap( SyroChannel * psc, int write_page );
void          SyroFunc_MakeStartMark( SyroChannel * psc, int write_page );
void          SyroFunc_MakeChannelInfo( SyroChannel * psc, int write_page );
void          SyroVolca_FSK_Byte( uint8_t byte, int16_t ** buf, uint32_t * fms );

#endif
