#ifndef KORG_SYRO_VOLCASAMPLE_H__
#define KORG_SYRO_VOLCASAMPLE_H__

typedef enum
{
  Status_Success,

  // ------ Start -------
  Status_IllegalData,
  Status_NotEnoughMemory,

  // ------ GetSample/End -------
  Status_InvalidHandle,
  Status_NoData
} SyroStatus;

typedef struct
{
  uint8_t      *pData;
  uint32_t	Size;		// Byte Size
} SyroData;

typedef void *SyroHandle;

struct params
{
  char	       *device_name;
  unsigned	device_id;
  char	       *version;
  int		block;
};

extern struct params sw;

/*-------------------------*/
/*------ Functions --------*/
/*-------------------------*/
#ifdef __cplusplus
extern	      "C"
{
#endif

  SyroStatus	SyroVolca_Start( SyroHandle * pHandle, SyroData * pData,
      uint32_t * frame_size, uint32_t * channels );
  SyroStatus	SyroVolca_FSK( SyroHandle Handle, int16_t * buf,
      uint32_t * fms );
  SyroStatus	SyroVolca_GetSample( SyroHandle Handle, int16_t * pLeft,
      int16_t * pRight );
  SyroStatus	SyroVolca_End( SyroHandle Handle );

#ifdef __cplusplus
}
#endif

#endif				// KORG_SYRO_H__
