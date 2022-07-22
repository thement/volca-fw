/************************************************************************
	SYRO for volca sample
 ***********************************************************************/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "volca_syro_func.h"
#include "volca_syro.h"

#define SYRO_MANAGE_HEADER	0x47524F4B
#define	ALL_INFO_SIZE		0x4000

#define CRC_BLOCK_SIZE		0x400
#define BLOCK_SIZE		0x100
#define BLOCK_PER_SECTOR	256
#define BLOCK_PER_SUBSECTOR	16
#define SUBSECTOR_SIZE		(BLOCK_SIZE * BLOCK_PER_SUBSECTOR)

#define	LPF_FEEDBACK_LEVEL	0x2000

#define FSK_BYTES__GAP_HEADER	1250
#define FSK_BYTES__HEADER	(2 + sizeof( SyroTxHeader ) + 1)
#define FSK_BYTES__GAP_3S	500
#define FSK_BYTES__CT_BLK	(2 + 2)
#define FSK_BYTES__BLOCK	(2 + BLOCK_SIZE + 3 + 1)
#define FSK_BYTES__GAP		43
#define FSK_BYTES__GAP_FOOTER	500

#define NUM_GAP_HEADER_CYCLE	10000
#define NUM_GAP_CYCLE		35
#define NUM_GAP_F_CYCLE		1000
#define NUM_GAP_FOOTER_CYCLE	4000
#define NUM_GAP_3S_CYCLE	8000

#define	NUM_FRAME__GAP_HEADER	(NUM_GAP_HEADER_CYCLE * KORGSYRO_QAM_CYCLE)
#define	NUM_FRAME__GAP		(NUM_GAP_CYCLE * KORGSYRO_QAM_CYCLE)
#define	NUM_FRAME__GAP_F	(NUM_GAP_F_CYCLE * KORGSYRO_QAM_CYCLE)
#define	NUM_FRAME__GAP_3S	(NUM_GAP_3S_CYCLE * KORGSYRO_QAM_CYCLE)
#define	NUM_FRAME__GAP_FOOTER	(NUM_GAP_FOOTER_CYCLE * KORGSYRO_QAM_CYCLE)
#define NUM_FRAME__HEADER	(49 * KORGSYRO_QAM_CYCLE)
#define NUM_FRAME__CT_BLK	(6 * KORGSYRO_QAM_CYCLE)
#define NUM_FRAME__BLOCK	(352 * KORGSYRO_QAM_CYCLE)

#define TXHEADER_STR_LEN	16
#define	TXHEADER_STR		"KORG SYSTEM FILE"

typedef enum
{
  TaskStatus_Gap = 0,
  TaskStatus_StartMark,
  TaskStatus_ChannelInfo,
  TaskStatus_Data,
  TaskStatus_Gap_Footer,
  TaskStatus_End = -1
} SyroTaskStatus;

enum modulation
{
  NONE, FSK, QAM,
};

struct device
{
  char         *name;
  uint32_t      id;
  enum modulation mod;
};

struct device Devices[] = {
  {"Sample", 0xff0033b8, QAM},
  {"FM", 0xff0033ba, QAM},
  {"Drum", 0xff004333, QAM},
  {"Kick", 0xff0033b9, QAM},
  {"Beats", 0xff002fa8, FSK},
  {"Bass", 0xff002fb2, FSK},
  {"Nubass", 0xff004332, FSK},
  {"Keys", 0xff002fbc, FSK},
};

#define ALL_DEVICES ( sizeof( Devices ) / sizeof( struct device ) )

typedef struct
{
  uint8_t       Header[TXHEADER_STR_LEN];
  uint32_t      DeviceID;
  uint8_t       BlockCode;
  uint8_t       Num;
  uint8_t       Version[2];
  uint32_t      Size;
  uint16_t      m_Reserved;
  uint16_t      m_Speed;
} SyroTxHeader;

typedef struct
{
  uint32_t      Header;
  SyroTaskStatus TaskStatus;
  struct device *Dev;
  int           TaskCount;

  // ---- Manage source data(all) -----
  int           CurData;

  // ---- Manage source data(this) -----
  const uint8_t *pSrcData;
  int           DataCount;
  int           DataSize;
  uint32_t      EraseLength;
  int           CompBlockPos;
  uint32_t      BlockLen1st;

  // ---- Manage output data -----
  uint8_t       TxBlock[BLOCK_SIZE];
  int           TxBlockSize;
  int           TxBlockPos;

  uint32_t      PoolData;
  int           PoolDataBit;

  bool          UseEcc;
  uint32_t      EccData;
  bool          UseCrc;
  uint32_t      CrcData;

  SyroChannel   Channel[KORGSYRO_NUM_CHANNEL];
  int           CyclePos;
  int           FrameCountInCycle;

  int           LongGapCount;	// Debug Put
} SyroManage;

typedef struct
{
  SyroData     *Data;
} SyroManageSingle;

/*-----------------------------------------------------------------------
	Setup Next Data
 -----------------------------------------------------------------------*/
static void
SyroVolca_SetupNextData( SyroManage * psm )
{
  SyroManageSingle *psms;
  SyroTxHeader *psth;
  char         *p;
  int           i;

  psms = ( SyroManageSingle * ) ( psm + 1 );
  psms += psm->CurData;

  // ----- Setup Tx Header ----
  psth = ( SyroTxHeader * ) psm->TxBlock;

  memset( ( uint8_t * ) psth, 0, sizeof( SyroTxHeader ) );
  memcpy( psth->Header, TXHEADER_STR, TXHEADER_STR_LEN );
  psth->DeviceID = sw.device_id ? sw.device_id : Devices[1].id;	// default FM
  if( sw.device_name )
  {
    for( i = 0; i < ALL_DEVICES; i++ )
      if( !strcasecmp( Devices[i].name, sw.device_name ) )
      {
	psth->DeviceID = Devices[i].id;
	break;
      }
  }
  psm->Dev = NULL;
  for( i = 0; i < ALL_DEVICES; i++ )
    if( psth->DeviceID == Devices[i].id )
    {
      psm->Dev = &Devices[i];
      break;
    }
  psm->TxBlockSize = sizeof( SyroTxHeader );
  psm->pSrcData = psms->Data->pData;
  psm->DataSize = psms->Data->Size;
  psm->DataCount = 0;
  psm->CompBlockPos = 0;
  psm->EraseLength = 0;
  if( ( psm->CurData + 1 ) < 1 )
  {
    sw.block++;			// ----- Set continue
  }
  psth->Size = 0;
  psth->Version[0] = 0;
  psth->Version[1] = 0;
  if( sw.version )
  {
    if( ( p = strchr( sw.version, '.' ) ) == NULL )
    {
      psth->Version[1] = atoi( sw.version );
    }
    else
    {
      *p = 0;
      psth->Version[0] = atoi( sw.version );
      psth->Version[1] = atoi( p + 1 );
    }
  }
  else
  {
    psth->Size = psms->Data->Size;
  }
  psth->Num = 0xff;
  psm->EraseLength = NUM_GAP_3S_CYCLE;
  psth->BlockCode = sw.block;
  psm->TaskStatus = TaskStatus_Gap;
  psm->TaskCount = NUM_GAP_HEADER_CYCLE;
}

/*-----------------------------------------------------------------------
	Setup by TxBlock
 -----------------------------------------------------------------------*/
static void
SyroVolca_SetupBlock( SyroManage * psm )
{
  bool          use_ecc;

  use_ecc = ( psm->TxBlockSize == BLOCK_SIZE ) ? true : false;
  psm->TxBlockPos = 0;
  psm->TaskCount = psm->TxBlockSize;
  psm->UseEcc = use_ecc;
  psm->UseCrc = true;
  psm->CrcData = SyroFunc_CRC16_nrm( psm->TxBlock, psm->TxBlockSize );
  if( use_ecc )
  {
    psm->EccData = SyroFunc_CalculateEcc( psm->TxBlock, psm->TxBlockSize );
  }

  psm->PoolData = 0xa9;		// Block Start Code
  psm->PoolDataBit = 8;
}

static int
Constant_Block( SyroManage * psm )
{
  int           i, ct;

  ct = psm->TxBlock[0];
  for( i = 1; i < BLOCK_SIZE; i++ )
  {
    if( ct != psm->TxBlock[i] )
      return -1;
  }
  return ct;
}

/************************************************************************
	Internal Functions (Output Syro Data)
 ***********************************************************************/
/*-----------------------------------------------------------------------
	Generate Data
	 ret : true if block is end.
 -----------------------------------------------------------------------*/
static bool
SyroVolca_MakeData( SyroManage * psm, int write_page )
{
  int           ch, bit, ct_val;
  uint32_t      dat;
  bool          data_end;

  data_end = false;
  if( psm->TxBlockPos == 0 && psm->TxBlockSize == BLOCK_SIZE )
    if( ( ct_val = Constant_Block( psm ) ) >= 0 )
    {
      psm->PoolData =
	  ( ( ~ct_val & 0xff ) << 16 ) | ( ( ct_val & 0xff ) << 8 ) | 0x4e;
      psm->PoolDataBit = 24 - 1;
      psm->UseEcc = false;
      psm->UseCrc = false;
      psm->TaskCount = 0;
      psm->TxBlockPos = BLOCK_SIZE;
    }
  // ------ Supply Data/Ecc/Crc ------
  if( psm->PoolDataBit < ( 3 * KORGSYRO_NUM_CHANNEL ) )
  {
    if( psm->TaskCount )
    {
      dat = psm->TxBlock[psm->TxBlockPos++];
      bit = 8;
      psm->TaskCount--;
    }
    else if( psm->UseEcc )
    {
      dat = psm->EccData;
      bit = 24;
      // printf( "ECC %x\n", dat );
      psm->UseEcc = false;
    }
    else if( psm->UseCrc )
    {
      dat = psm->CrcData;
      bit = 16;
      // printf( "CRC %x\n", dat );
      psm->UseCrc = false;
    }
    else
    {
      dat = 0;
      bit = ( 3 * KORGSYRO_NUM_CHANNEL ) - psm->PoolDataBit;
      data_end = true;
    }
    psm->PoolData |= ( dat << psm->PoolDataBit );
    psm->PoolDataBit += bit;
  }

  // ------ Make Cycle ------
  for( ch = 0; ch < KORGSYRO_NUM_CHANNEL; ch++ )
  {
    SyroFunc_GenerateSingleCycle( &psm->Channel[ch], write_page,
	( psm->PoolData & 7 ), true );
    psm->PoolData >>= 3;
    psm->PoolDataBit -= 3;
  }
  return data_end;
}

/*-----------------------------------------------------------------------
	Nake Next Cycle
 -----------------------------------------------------------------------*/
static void
SyroVolca_CycleHandler( SyroManage * psm )
{
  int           write_page;

  write_page = ( psm->CyclePos / KORGSYRO_QAM_CYCLE ) ^ 1;
  switch ( psm->TaskStatus )
  {
    case TaskStatus_Gap:
      SyroFunc_MakeGap( psm->Channel, write_page );
      if( !( --psm->TaskCount ) )
      {
	psm->TaskStatus = TaskStatus_StartMark;
	SyroVolca_SetupBlock( psm );
      }
      break;
    case TaskStatus_StartMark:
      SyroFunc_MakeStartMark( psm->Channel, write_page );
      psm->TaskStatus = TaskStatus_ChannelInfo;
      break;
    case TaskStatus_ChannelInfo:
      SyroFunc_MakeChannelInfo( psm->Channel, write_page );
      psm->TaskStatus = TaskStatus_Data;
      break;
    case TaskStatus_Data:
      if( SyroVolca_MakeData( psm, write_page ) )
      {
	if( psm->DataCount < psm->DataSize )
	{
	  int           size;

	  size = ( psm->DataSize - psm->DataCount );
	  if( size >= BLOCK_SIZE )
	  {
	    size = BLOCK_SIZE;
	  }
	  else
	  {
	    memset( psm->TxBlock, 0, BLOCK_SIZE );
	  }
	  memcpy( psm->TxBlock, ( psm->pSrcData + psm->DataCount ), size );
	  psm->TaskStatus = TaskStatus_Gap;
	  psm->TaskCount = NUM_GAP_CYCLE;
	  if( !psm->DataCount )
	  {
	    psm->TaskCount = psm->EraseLength;
	  }
	  psm->TxBlockSize = BLOCK_SIZE;
	  psm->DataCount += size;
	}
	else
	{
	  psm->CurData++;
	  if( psm->CurData < 1 )
	  {
	    SyroVolca_SetupNextData( psm );
	  }
	  else
	  {
	    psm->TaskStatus = TaskStatus_Gap_Footer;
	    psm->TaskCount = NUM_GAP_CYCLE + NUM_GAP_FOOTER_CYCLE;
	  }
	}
      }
      break;
    case TaskStatus_Gap_Footer:
      SyroFunc_MakeGap( psm->Channel, write_page );
      if( !( --psm->TaskCount ) )
      {
	psm->TaskStatus = TaskStatus_End;
      }
      break;
    default:			// case TaskStatus_End:
      return;
  }

  psm->FrameCountInCycle += KORGSYRO_QAM_CYCLE;
}

/*-----------------------------------------------------------------------
	Get Ch Sample
 -----------------------------------------------------------------------*/
static        int16_t
SyroVolca_GetChSample( SyroManage * psm, int ch )
{
  int32_t       dat;

  dat = ( int32_t ) psm->Channel[ch].CycleSample[psm->CyclePos];
  // ----- LPF -----*/
  dat = ( ( dat * ( 0x10000 - LPF_FEEDBACK_LEVEL ) ) +
      ( psm->Channel[ch].Lpf_z * LPF_FEEDBACK_LEVEL ) );
  dat /= 0x10000;
  psm->Channel[ch].Lpf_z = dat;
  return ( int16_t ) dat;
}

static bool
is_constant_block( uint8_t * mem, uint32_t len )
{
  int           i;
  uint8_t       v;

  if( len != BLOCK_SIZE )
    return false;
  v = mem[0];
  for( i = 1; i < BLOCK_SIZE; i++ )
    if( mem[i] != v )
      return false;
  return true;
}

/*-----------------------------------------------------------------------
	Get Frame Size FSK
 -----------------------------------------------------------------------*/
static void
SyroVolca_FSKFrameSize( SyroData * pData, uint32_t * size )
{
  uint8_t      *mem;
  uint32_t      l, len;

  *size = FSK_BYTES__GAP_HEADER * KORGSYRO_FSK1_CYCLE * 8;
  *size += FSK_BYTES__HEADER * KORGSYRO_FSK0_CYCLE * 8;
  *size += FSK_BYTES__GAP_3S * KORGSYRO_FSK1_CYCLE * 8;
  mem = pData->pData;
  len = pData->Size;
  while( len )
  {
    l = len > BLOCK_SIZE ? BLOCK_SIZE : len;
    if( is_constant_block( mem, l ) )
      *size += FSK_BYTES__CT_BLK * KORGSYRO_FSK0_CYCLE * 8;
    else
      *size += FSK_BYTES__BLOCK * KORGSYRO_FSK0_CYCLE * 8;
    *size += FSK_BYTES__GAP * KORGSYRO_FSK1_CYCLE * 8;
    mem += l;
    len -= l;
  }
  len =
      ( pData->Size + CRC_BLOCK_SIZE -
      1 ) / CRC_BLOCK_SIZE * sizeof( uint16_t );
  len += 2 + 1;
  *size += len * KORGSYRO_FSK0_CYCLE * 8;
  *size += FSK_BYTES__GAP * KORGSYRO_FSK1_CYCLE * 8;
  *size += FSK_BYTES__GAP_FOOTER * KORGSYRO_FSK1_CYCLE * 8;
}

/*-----------------------------------------------------------------------
	Get Frame Size QAM
 -----------------------------------------------------------------------*/
static void
SyroVolca_GetFrameSize( SyroData * pData, uint32_t * size )
{
  uint8_t      *mem;
  uint32_t      l, len;

  *size = NUM_FRAME__GAP_HEADER;
  *size += NUM_FRAME__HEADER;
  *size += NUM_FRAME__GAP_3S;
  mem = pData->pData;
  len = pData->Size;
  while( len )
  {
    l = len > BLOCK_SIZE ? BLOCK_SIZE : len;
    if( is_constant_block( mem, l ) )
      *size += NUM_FRAME__CT_BLK;
    else
      *size += NUM_FRAME__BLOCK;
    *size += NUM_FRAME__GAP;
    mem += l;
    len -= l;
  }
  *size += NUM_FRAME__GAP_FOOTER;
}

/************************************************************************
	Exteral Functions
 ***********************************************************************/
/*======================================================================
	Syro Start
 ======================================================================*/
SyroStatus
SyroVolca_Start( SyroHandle * pHandle, SyroData * pData,
    uint32_t * frame_size, uint32_t * channels )
{
  int           i;
  uint32_t      handle_size;
  SyroManage   *psm;
  SyroManageSingle *psms;

  if( pData->Size < ALL_INFO_SIZE )
  {
    return Status_IllegalData;
  }
  // ------- Alloc Memory --------
  handle_size = sizeof( SyroManage ) + sizeof( SyroManageSingle );
  psm = ( SyroManage * ) malloc( handle_size );
  if( !psm )
  {
    return Status_NotEnoughMemory;
  }
  psms = ( SyroManageSingle * ) ( psm + 1 );
  // ------- Setup --------
  memset( ( uint8_t * ) psm, 0, handle_size );
  psm->Header = SYRO_MANAGE_HEADER;
  psms->Data = pData;
  SyroVolca_SetupNextData( psm );
  if( psm->Dev == NULL )
    return Status_IllegalData;
  switch ( psm->Dev->mod )
  {
    case FSK:
      *channels = 1;
      SyroVolca_FSKFrameSize( pData, frame_size );
      printf( "Volca %s [FSK modulation]\n", psm->Dev->name );
      break;
    case QAM:
      *channels = 2;
      SyroVolca_GetFrameSize( pData, frame_size );
      printf( "Volca %s [QAM modulation]\n", psm->Dev->name );
      for( i = 0; i < KORGSYRO_NUM_CYCLE; i++ )
      {
	SyroVolca_CycleHandler( psm );
	psm->CyclePos += KORGSYRO_QAM_CYCLE;
      }
      break;
    default:
      *channels = 0;
      *frame_size = 0;
      break;
  }
  psm->CyclePos = 0;
  *pHandle = ( SyroHandle ) psm;
  return Status_Success;
}

static void
SyroVolca_FSK_Gap( int16_t ** buf, uint32_t * fms, int gap_bytes )
{
  int           i;
  for( i = 0; i < gap_bytes; i++ )
    SyroVolca_FSK_Byte( 0xff, buf, fms );
}

static void
SyroVolca_FSK_Data( int16_t ** buf, uint32_t * fms, uint8_t * data,
    int datalen, bool insert )
{
  int           i;
  uint8_t       sum;

  SyroVolca_FSK_Byte( 0x7f, buf, fms );
  if( is_constant_block( data, datalen ) )
  {
    SyroVolca_FSK_Byte( 0x4e, buf, fms );
    SyroVolca_FSK_Byte( *data, buf, fms );
    SyroVolca_FSK_Byte( ~*data, buf, fms );
  }
  else
  {
    SyroVolca_FSK_Byte( 0xa9, buf, fms );
    sum = 0;
    for( i = 0; i < datalen; i++ )
    {
      SyroVolca_FSK_Byte( data[i], buf, fms );
      sum += data[i];
    }
    if( insert )
    {
      for( i = 0; i < 3; i++ )
	SyroVolca_FSK_Byte( 0x55, buf, fms );
    }
    SyroVolca_FSK_Byte( sum, buf, fms );
  }
}

/*======================================================================
	Syro FSK Sample
 ======================================================================*/
SyroStatus
SyroVolca_FSK( SyroHandle Handle, int16_t * buf, uint32_t * fms )
{
  SyroManage   *psm;
  SyroManageSingle *psms;
  SyroData     *pData;
  uint8_t      *mem;
  uint32_t      i, l, len;
  uint16_t      crc_buf[BLOCK_SIZE];

  psm = ( SyroManage * ) Handle;
  if( psm->Header != SYRO_MANAGE_HEADER )
    return Status_InvalidHandle;
  psms = ( SyroManageSingle * ) ( psm + 1 );
  pData = psms->Data;
  *fms = 0;
  SyroVolca_FSK_Gap( &buf, fms, FSK_BYTES__GAP_HEADER );
  SyroVolca_FSK_Data( &buf, fms, psm->TxBlock, psm->TxBlockSize, false );
  SyroVolca_FSK_Gap( &buf, fms, FSK_BYTES__GAP_3S );
  mem = pData->pData;
  len = pData->Size;
  while( len )
  {
    l = len > BLOCK_SIZE ? BLOCK_SIZE : len;
    SyroVolca_FSK_Data( &buf, fms, mem, l, true );
    mem += l;
    len -= l;
    SyroVolca_FSK_Gap( &buf, fms, FSK_BYTES__GAP );
  }
  mem = pData->pData;
  len = ( pData->Size + CRC_BLOCK_SIZE - 1 ) / CRC_BLOCK_SIZE;
  for( i = 0; i < len; i++ )
  {
    crc_buf[i] = SyroFunc_CRC16_rev( mem, CRC_BLOCK_SIZE );
    mem += CRC_BLOCK_SIZE;
  }
  SyroVolca_FSK_Data( &buf, fms, ( uint8_t * ) crc_buf,
      len * sizeof( uint16_t ), false );
  SyroVolca_FSK_Gap( &buf, fms, FSK_BYTES__GAP_FOOTER );
  return Status_Success;
}

/*======================================================================
	Syro Get Sample
 ======================================================================*/
SyroStatus
SyroVolca_GetSample( SyroHandle Handle, int16_t * pLeft, int16_t * pRight )
{
  SyroManage   *psm;

  psm = ( SyroManage * ) Handle;
  if( psm->Header != SYRO_MANAGE_HEADER )
  {
    return Status_InvalidHandle;
  }

  if( !psm->FrameCountInCycle )
  {
    return Status_NoData;
  }

  *pLeft = SyroVolca_GetChSample( psm, 0 );
  *pRight = SyroVolca_GetChSample( psm, 1 );
  psm->FrameCountInCycle--;
  if( ( ++psm->CyclePos ) == KORGSYRO_NUM_CYCLE_BUF )
  {
    psm->CyclePos = 0;
  }

  if( !( psm->CyclePos % KORGSYRO_QAM_CYCLE ) )
  {
    SyroVolca_CycleHandler( psm );
  }

  return Status_Success;
}

/*======================================================================
	Syro End
 ======================================================================*/
SyroStatus
SyroVolca_End( SyroHandle Handle )
{
  SyroManage   *psm;

  psm = ( SyroManage * ) Handle;
  if( psm->Header != SYRO_MANAGE_HEADER )
  {
    return Status_InvalidHandle;
  }
  free( ( uint8_t * ) psm );
  return Status_Success;
}
