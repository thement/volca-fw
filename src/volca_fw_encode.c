#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include "volca_syro_func.h"
#include "volca_syro.h"

#define VERSION "1.2"

struct params sw;

static const uint8_t wav_header[] = {
  'R', 'I', 'F', 'F',		// 'RIFF'
  0x00, 0x00, 0x00, 0x00,	// Size (data size + 0x24)
  'W', 'A', 'V', 'E',		// 'WAVE'
  'f', 'm', 't', ' ',		// 'fmt '
  0x10, 0x00, 0x00, 0x00,	// Fmt chunk size
  0x01, 0x00,			// encode(wav)
  0x02, 0x00,			// channel = 2
  0x44, 0xAC, 0x00, 0x00,	// Fs (44.1kHz)
  0x10, 0xB1, 0x02, 0x00,	// Bytes per sec (Fs * 4)
  0x04, 0x00,			// Block Align (2ch,16Bit -> 4)
  0x10, 0x00,			// 16Bit
  'd', 'a', 't', 'a',		// 'data'
  0x00, 0x00, 0x00, 0x00	// data size(bytes)
};

#define WAV_POS_RIFF_SIZE	0x04
#define WAV_POS_CHANNELS	0x16
#define WAV_POS_FSAMPLE		0x18
#define WAV_POS_BYTES_SEC	0x1c
#define WAV_POS_ALIGN		0x20
#define WAV_POS_DATA_SIZE	0x28

/*----------------------------------------------------------------------------
	Read File
 ----------------------------------------------------------------------------*/
static uint8_t *
read_file( char *filename, uint32_t * psize )
{
  FILE	       *fp;
  uint8_t      *buf;
  uint32_t	size;

  fp = fopen( ( const char * ) filename, "rb" );
  if( !fp )
  {
    printf( " File open error, %s \n", filename );
    return NULL;
  }

  fseek( fp, 0, SEEK_END );
  size = ftell( fp );
  fseek( fp, 0, SEEK_SET );

  buf = malloc( size );
  if( !buf )
  {
    printf( " Not enough memory for read file.\n" );
    fclose( fp );
    return NULL;
  }

  if( fread( buf, 1, size, fp ) < size )
  {
    printf( " File read error, %s \n", filename );
    fclose( fp );
    free( buf );
    return NULL;
  }

  fclose( fp );

  *psize = size;
  return buf;
}

/*----------------------------------------------------------------------------
	Write File
 ----------------------------------------------------------------------------*/
static bool
write_file( char *filename, uint8_t * buf, uint32_t size )
{
  FILE	       *fp;

  fp = fopen( filename, "wb" );
  if( !fp )
  {
    printf( " File open error, %s \n", filename );
    return false;
  }

  if( fwrite( buf, 1, size, fp ) < size )
  {
    printf( " File write error(perhaps disk space is not enough), %s \n",
	filename );
    fclose( fp );
    return false;
  }

  fclose( fp );

  return true;
}

/*----------------------------------------------------------------------------
	setup & load file (all)
 ----------------------------------------------------------------------------*/
static bool
setup_file_all( char *filename, SyroData * syro_data )
{
  uint32_t	size;

  syro_data->pData = read_file( filename, &size );
  if( !syro_data->pData )
  {
    return false;
  }

  syro_data->Size = size;

  printf( "ok.\n" );

  return true;
}

/*----------------------------------------------------------------------------
	free data memory
 ----------------------------------------------------------------------------*/
static void
free_syrodata( SyroData * syro_data )
{
  if( syro_data->pData )
  {
    free( syro_data->pData );
    syro_data->pData = NULL;
  }
}

void
usage( char *progname )
{
  fprintf( stderr, "\n%s [-v#n#d#b#h] bin_file(s)\n"
      "\t-v\tfirmware version\n"
      "\t-n\tvolca name (instead of id; ex: drum)\n"
      "\t-d\tdevice id\n" "\t-b\tstart block (default 1)\n" "\n", progname );
  exit( 0 );
}

/*****************************************************************************
	Main
 *****************************************************************************/
int
main( int argc, char *argv[] )
{
  SyroData	syro_data;
  SyroStatus	status;
  SyroHandle	handle;
  uint8_t      *buf_dest;
  uint32_t	size_dest, data_size;
  uint32_t	frame,ch, write_pos;
  int16_t	left, right;
  char	       *fname, *p;
  int		option, i;

  printf( "Volca Firmware Encoder version %s\n", VERSION );
  do
  {
    option = getopt( argc, argv, "v:n:d:b:h" );
    switch ( option )
    {
      case 'v':
	sw.version = strdup( optarg );
	break;
      case 'n':
	sw.device_name = strdup( optarg );
	break;
      case 'd':
	sw.device_id = strtol( optarg, NULL, 16 );
	break;
      case 'b':
	sw.block = atoi( optarg );
	break;
      case EOF:		// no more options
	break;
      case 'h':
	usage( argv[0] );
      default:
	fprintf( stderr, "getopt returned impossible value: %d ('%c')",
	    option, option );
	usage( argv[0] );
    }
  }
  while( option != EOF );

  if( optind == argc )
    usage( argv[0] );
  while( optind < argc )
  {
    if( setup_file_all( argv[optind], &syro_data ) == false )
    {
      printf( "No such file %s\n", argv[optind] );
      exit( 0 );
    }
    fname = malloc( strlen( argv[optind] + 5 ) );
    strcpy( fname, argv[optind] );
    if( ( p = strrchr( fname, '.' ) ) == NULL )
      p = fname + strlen( fname );
    strcpy( p, ".wav" );
    optind++;
    // ----- Start ------
    status = SyroVolca_Start( &handle, &syro_data, &frame, &ch );
    if( status != Status_Success )
    {
      printf( " Start error, %d \n", status );
      free_syrodata( &syro_data );
      return 1;
    }

    data_size = frame * ch * sizeof( int16_t );
    size_dest = data_size + sizeof( wav_header );

    buf_dest = malloc( size_dest );
    if( !buf_dest )
    {
      printf( " Not enough memory for write file.\n" );
      SyroVolca_End( handle );
      free_syrodata( &syro_data );
      return 1;
    }

    // ----- convert loop ------
    write_pos = sizeof( wav_header );
    if( ch == 1 )
    {
       SyroVolca_FSK( handle, (int16_t *)(buf_dest + write_pos), &frame );
    }
    else if( ch == 2 )
    {
      for( i = 0; i < frame; i++ )
      {
	SyroVolca_GetSample( handle, &left, &right );
	*( int16_t * ) ( buf_dest + write_pos ) = left;
	write_pos += sizeof( int16_t );
	*( int16_t * ) ( buf_dest + write_pos ) = right;
	write_pos += sizeof( int16_t );
      }
    }
    SyroVolca_End( handle );
    free_syrodata( &syro_data );

    data_size = frame * ch * sizeof( int16_t );
    size_dest = data_size + sizeof( wav_header );
    memcpy( buf_dest, wav_header, sizeof( wav_header ) );
    *( uint32_t * ) ( buf_dest + WAV_POS_RIFF_SIZE ) = data_size + 0x24;
    *( uint32_t * ) ( buf_dest + WAV_POS_DATA_SIZE ) = data_size;
    *( uint16_t * ) ( buf_dest + WAV_POS_CHANNELS ) = ch;
    *( uint16_t * ) ( buf_dest + WAV_POS_ALIGN ) = ch * sizeof( short );
    *( uint32_t * ) ( buf_dest + WAV_POS_BYTES_SEC ) =
	*( uint32_t * ) ( buf_dest + WAV_POS_FSAMPLE ) * ch * sizeof( short );

    // ----- write ------

    if( write_file( fname, buf_dest, size_dest ) )
    {
      printf( "File %s (block %d) written.\n", fname, sw.block );
      sw.block++;
    }
    if( buf_dest )
      free( buf_dest );
  }
  return 0;
}
