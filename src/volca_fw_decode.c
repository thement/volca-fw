#include "stdlib.h"
#include "stdio.h"
#include "string.h"
#include "stdint.h"
#include "sys/stat.h"
#include "volca_syro_func.h"

#define VERSION "1.2"

// #define GEN_IQ_WAV 1

const short   sine_tbl[8] = {
  0x0, 0x5a82, 0x7fff, 0x5a82,
  0x0, 0xa57e, 0x8001, 0xa57e,
};

#define BUFSIZE 0x400
uint8_t       crcbuf[BUFSIZE], fw[BUFSIZE + 6], fill[BUFSIZE];

struct wav_header
{
  char          riff[4];
  uint32_t      size;
  char          wave[4];
  char          fmt[4];
  uint32_t      chunk_size;
  uint16_t      encode;
  uint16_t      channels;
  uint32_t      fs;
  uint32_t      Bps;
  uint16_t      block;
  uint16_t      bps;
  char          data[4];
  uint32_t      data_size;
};

struct fw_block
{
  short         block;
  short         last;
  short         bytes;
};

#define TXHEADER_STR	 "KORG SYSTEM FILE"
#define TXHEADER_STR_LEN 16

struct tx_header
{
  uint8_t       Header[TXHEADER_STR_LEN];
  uint32_t      DeviceID;
  uint8_t       BlockCode;
  uint8_t       Num;
  uint8_t       Version[2];
  uint32_t      Size;
  uint16_t      m_Reserved;
  uint16_t      m_Speed;
};

void
firmware_to_binary( FILE * ffw, FILE * fbin, int ch )
{
  int           i, addr, offset, packet_size;
  uint8_t       sum, csum, *tail;
  uint16_t      crc, ccrc, *scrc;
  struct tx_header *hd;
  struct fw_block bh;

  offset = 0;
  packet_size = 0;
  addr = 0;
  fseek( ffw, 0L, SEEK_SET );
  memset( fill, 0xff, 0x100 );
  while( fread( &bh, sizeof( struct fw_block ), 1, ffw ) )
  {
    if( bh.bytes > BUFSIZE )
      break;
    if( fread( fw, 1, bh.bytes, ffw ) != bh.bytes )
      break;
    if( bh.bytes )
    {
      switch ( fw[0] )
      {
	case 0xa9:
	  if( !strncmp( ( const char * ) &fw[1], TXHEADER_STR,
	      TXHEADER_STR_LEN ) )
	  {
	    hd = ( struct tx_header * ) &fw[1];
	    printf( "Device 0x%x Version %d.%02d Block %d Size 0x%x\n",
		hd->DeviceID, hd->Version[0], hd->Version[1],
		hd->BlockCode, hd->Size );
	    if( ch == 1 )
	    {
	      sum = fw[1 + sizeof( struct tx_header )];
	      for( csum = 0, i = 1; i <= sizeof( struct tx_header ); i++ )
		csum += fw[i];
	      if( sum != csum )
		printf( "Bad sum %02x != %02x in header\n", sum, csum );
	    }
	    break;
	  }
	  if( bh.bytes - ( ch == 2 ? 6 : 5 ) > packet_size )
	    packet_size = bh.bytes - ( ch == 2 ? 6 : 5 );
	  if( ch == 2 )
	  {
	    tail = fw + bh.bytes - sizeof( uint16_t );
	    crc = *( uint16_t * ) tail;
	    ccrc = SyroFunc_CRC16_nrm( &fw[1], 0x100 );
	  }
	  else			// ch == 1
	  {
	    sum = csum = 0;
	    tail = fw + bh.bytes - 4;
	    if( tail[0] == 0x55 && tail[1] == 0x55 && tail[2] == 0x55 )
	    {
	      sum = tail[3];
	      for( csum = 0, i = 1; i <= packet_size; i++ )
		csum += fw[i];
	      bh.bytes -= 4;
	    }
	  }
	  if( ch == 1 && sum != csum )
	    printf( "Bad sum %02x != %02x in block 0x%x\n", sum, csum,
		bh.block );
	  else if( ch == 2 && crc != ccrc )
	    printf( "Bad CRC %04x != %04x in block 0x%x\n", crc, ccrc,
		bh.block );
	  else
	  {
	    if( bh.last )
	    {
	      memcpy( fill, &fw[1], bh.bytes - 1 );
	      scrc = ( uint16_t * ) fill;
	      fseek( fbin, 0L, SEEK_SET );
	      addr = 0;
	      while( ( bh.bytes =
		  fread( crcbuf, 1, BUFSIZE, fbin ) ) == BUFSIZE )
	      {
		crc = *scrc++;
		ccrc = SyroFunc_CRC16_rev( crcbuf, BUFSIZE );
		if( crc != ccrc )
		  printf( "Bad CRC %04x != %04x at address %04x\n",
		      crc, ccrc, addr );
		addr += BUFSIZE;
	      }
	    }
	    else
	    {
	      fwrite( &fw[1], 1, packet_size, fbin );
	    }
	  }
	  break;
	case 0x4e:
	  if( bh.bytes != 3 && bh.bytes != 2 )
	    break;
	  if( bh.bytes == 2 && ch == 1 )
	    fw[2] = 0xff;
	  if( fw[1] != ( ~fw[2] & 0xff ) )
	    break;
	  memset( fill, fw[1], packet_size );
	  fwrite( fill, 1, packet_size, fbin );
	  break;
	default:
	  printf( "Unknown block marker %02x at offset %x\n", fw[0], offset );
	  exit( 0 );
	  break;
      }
    }
  }
}

int
decode_fsk( short *samples, int frames, uint8_t * rec )
{
  int           i, cnt, bits, len, pre;
  uint8_t       b;
  uint32_t      word;

  len = 0;
  pre = -1;
  cnt = 1;
  b = 0;
  word = 0;
  bits = 0;
  for( i = 0; i < frames; i++ )
  {
    if( samples[i] == 0 )
      continue;
    if( ( pre < 0 && samples[i] > 0 ) || ( pre > 0 && samples[i] < 0 ) )
    {
      b <<= 4;
      b |= cnt;
      if( b == 0x55 || b == 0xaa )
      {
	word >>= 1;
	if( b == 0x55 )
	  word |= 1 << 31;
	bits++;
	b = 0;
	if( word == 0xa97fffff || word == 0x4e7fffff )	// sync words
	{
	  len--;
	  rec[len++] = 0x7f;
	  rec[len++] = word >> 24;
	  bits = 24;
	}
	if( bits >= 32 )
	{
	  rec[len++] = ( word >> 24 ) & 0xff;
	  bits -= 8;
	}
      }
      cnt = 1;
      pre = samples[i];
    }
    else
      cnt++;
  }
  return len;
}

int
decode_qam( short *samples, int frames, uint8_t * rec )
{
  int           i, j, len, smp;
  char          bits;
  unsigned      word;
  short        *smp_i, *smp_q;

  if( ( smp_i = malloc( frames * 2 * sizeof( short ) ) ) == NULL ||
      ( smp_q = malloc( frames * 2 * sizeof( short ) ) ) == NULL )
  {
    printf( "Malloc\n" );
    exit( 0 );
  }
#ifdef GEN_IQ_WAV
  FILE         *I_ofh, *Q_ofh;
  struct wav_header *wh;

  wh = ( struct wav_header * ) fill;
  memcpy( wh->riff, "RIFF", 4 );
  wh->size = frames * ch * sizeof( short ) + 0x24;
  memcpy( wh->wave, "WAVE", 4 );
  memcpy( wh->fmt, "fmt ", 4 );
  wh->chunk_size = 16;
  wh->encode = 1;
  wh->channels = 2;
  wh->fs = 44100;
  wh->Bps = 44100 * ch * sizeof( short );
  wh->block = ch * sizeof( short );
  wh->bps = 8 * sizeof( short );
  memcpy( wh->data, "data", 4 );
  wh->data_size = frames * ch * sizeof( short );
  if( ( I_ofh = fopen( "volcafm_I.wav", "wb" ) ) == NULL ||
      ( Q_ofh = fopen( "volcafm_Q.wav", "wb" ) ) == NULL )
  {
    printf( "Cannot open IQ wav files for writing\n" );
    exit( 0 );
  }
  fwrite( fill, 1, sizeof( struct wav_header ), I_ofh );
  fwrite( fill, 1, sizeof( struct wav_header ), Q_ofh );
#endif
  len = 0;
  for( i = 0; i < frames; i++ )
  {
    smp = samples[i * 2] * sine_tbl[( i + 0 ) % 8];
    smp_i[i * 2] = ( short ) ( smp >> 15 );	// left
    smp = samples[i * 2 + 1] * sine_tbl[( i + 0 ) % 8];
    smp_i[i * 2 + 1] = ( short ) ( smp >> 15 );	// right

    smp = samples[i * 2] * sine_tbl[( i + 2 ) % 8];	// 90deg offset
    smp_q[i * 2] = ( short ) ( smp >> 15 );	// left
    smp = samples[i * 2 + 1] * sine_tbl[( i + 2 ) % 8];
    smp_q[i * 2 + 1] = ( short ) ( smp >> 15 );	// right
  }
#ifdef GEN_IQ_WAV
  fwrite( smp_i, ch * sizeof( short ), frames, I_ofh );
  fwrite( smp_q, ch * sizeof( short ), frames, Q_ofh );
#endif
  for( i = 0; i < 2 * frames; i += 2 * 8 )
  {
    int           lr, mag;

    bits = 0;
    for( lr = 0; lr < 2; lr++ )
    {
      bits <<= 4;
      word = 0;
      for( j = 0; j < 2 * 8; j += 4 )
      {
	word <<= 4;
	if( smp_i[i + j + lr] == 0 )
	{
	  word |= 1;
	  if( smp_q[i + j + lr] < 0 )
	    word |= 2;
	  mag = smp_q[i + j + lr] * smp_q[i + j + lr];
	  if( mag > 250000000 )
	    word |= 4;
	}
	else if( smp_q[i + j + lr] == 0 )
	{
	  word |= 0;
	  if( smp_i[i + j + lr] < 0 )
	    word |= 2;
	  mag = smp_i[i + j + lr] * smp_i[i + j + lr];
	  if( mag > 250000000 )
	    word |= 4;
	}
	else
	{
	  printf( "failed at sample %d\n", i + j + lr );
	}
	word &= 0x1fff;
	switch ( word )
	{
	  case 0x1030:
	    bits |= 0;
	    break;
	  case 0x1434:
	    bits |= 1;
	    break;
	  case 0x1010:
	    bits |= 2;
	    break;
	  case 0x1050:
	    bits |= 3;
	    break;
	  case 0x1212:
	    bits |= 4;
	    break;
	  case 0x1616:
	    bits |= 5;
	    break;
	  case 0x1232:
	    bits |= 6;
	    break;
	  case 0x1272:
	    bits |= 7;
	    break;
	}
      }
    }
    rec[len++] = bits;
  }
#ifdef GEN_IQ_WAV
  fclose( I_ofh );
  fclose( Q_ofh );
#endif
  if( smp_i )
    free( smp_i );
  if( smp_q )
    free( smp_q );
  return len;
}

int
main( int argc, char *argv[] )
{
  FILE         *ftxt, *ffw, *fbin, *ifh;
  int           i, j, fa, col, len, bcnt, bytes, ch;
  int           width, frames, pre, end_packet;
  short        *samples;
  unsigned      word;
  char         *p;
  uint8_t      *rec, pattern, packet_type;
  double        rate;
  struct wav_header *wh;
  struct fw_block bh;
  char          fname[40];
  struct stat   fstat;

  printf( "Volca Firmware Decoder version %s\n", VERSION );
  if( argc < 2 )
  {
    printf( "Usage: %s wav_file(s)\n", argv[0] );
    exit( -1 );
  }
  for( fa = 1; fa < argc; fa++ )
  {
    strcpy( fname, argv[fa] );
    if( stat( fname, &fstat ) < 0 )
    {
      printf( "File %s not found\n", fname );
      exit( -2 );
    }
    if( ( ifh = fopen( fname, "rb" ) ) == NULL )
    {
      printf( "Cannot open file %s for reading\n", fname );
      exit( -3 );
    }
    fread( fill, 1, sizeof( struct wav_header ), ifh );
    wh = ( struct wav_header * ) fill;
    if( strncmp( wh->riff, "RIFF", 4 ) ||
	strncmp( wh->wave, "WAVE", 4 ) ||
	strncmp( wh->fmt, "fmt ", 4 ) ||
	strncmp( wh->data, "data", 4 ) || wh->encode != 1 )
    {
      printf( "Invalid format %d\n", wh->encode );
      exit( 0 );
    }
    ch = wh->channels;
    rate = wh->fs;
    width = wh->bps;
    if( width != 16 )
    {
      printf( "Invalid sample format\n" );
      exit( 0 );
    }
    if( wh->block )
      frames = wh->data_size / wh->block;
    else
      frames = 0;
    printf( "Channels %d, width %d frames %d, rate %.0f\n", ch, width, frames,
	rate );
    if( ( rec = malloc( frames / 8 ) ) == NULL ||
	( samples = malloc( frames * ch * sizeof( short ) ) ) == NULL )
    {
      printf( "Malloc\n" );
      exit( 0 );
    }
    len = 0;
    if( fread( samples, ch * sizeof( short ), frames, ifh ) == frames )
    {
      if( ch == 1 )
	len = decode_fsk( samples, frames, rec );
      else if( ch == 2 )
	len = decode_qam( samples, frames, rec );
      FILE *frec = fopen("rec.bin", "wb");
      if (frec == NULL) {
	printf("cannot open rec.bin\n");
	exit(1);
      }
      fwrite(rec, len, 1, frec);
      fclose(frec);
    }
    fclose( ifh );
    if( ( p = strrchr( fname, '.' ) ) == NULL )
      p = fname + strlen( fname );
    strcpy( p, ".txt" );
    if( ( ftxt = fopen( fname, "wt" ) ) == NULL )
    {
      printf( "Cannot write to file %s\n", fname );
      exit( 0 );
    }
    strcpy( p, ".fw" );
    if( ( ffw = fopen( fname, "w+b" ) ) == NULL )
    {
      printf( "Cannot write to file %s\n", fname );
      exit( 0 );
    }
    strcpy( p, ".bin" );
    if( ( fbin = fopen( fname, "w+b" ) ) == NULL )
    {
      printf( "Cannot write to file %s\n", fname );
      exit( 0 );
    }
    word = 0;
    bcnt = 0;
    bytes = 0;
    col = 0;
    pre = 0;
    pattern = ( ch == 2 ) ? 0x11 : 0xff;
    bh.block = 0;
    packet_type = 0;
    for( j = 0; j < len; j++ )
    {
      if( pre == 0 && rec[j] == pattern )
      {
	for( i = 0; j + i < len && rec[j + i] == pattern; )
	  i++;
	if( i >= 20 )
	{
	  end_packet = 0;
	  switch ( packet_type )
	  {
	    case 0xa9:
	      if( bytes > 16
		  && !strncmp( ( const char * ) &fw[1], TXHEADER_STR, 16 ) )
		end_packet = 1;
	      if( ch == 2 && bytes == 0x106 )
		end_packet = 1;
	      if( ch == 1 && ( bytes == 0x105 || bytes == 0x205 ) )
		end_packet = 1;
	      break;
	    case 0x4e:
	      if( bytes == 3 )
		end_packet = 1;
	      break;
	    default:
	      end_packet = 1;
	      break;
	  }
	  if( end_packet )
	  {
	    pre = 1;
	    fprintf( ftxt, "\n  %dx%02x\n", i, pattern );
	    j += i - 1;
	    word = 0;
	    bcnt = 0;
	    col = 0;
	    continue;
	  }
	}
      }
      if( pre )
      {
	if( ( rec[j] == 0x7f &&
	    ( rec[j + 1] == 0xa9 || rec[j + 1] == 0x4e ) &&
	    ch == 1 ) || ( rec[j] == 0x55 && rec[j + 1] == 0x01 && ch == 2 ) )
	{
	  if( bytes )
	  {
	    bh.last = 0;
	    bh.bytes = bytes;
	    fwrite( &bh, sizeof( struct fw_block ), 1, ffw );
	    bh.block++;
	    fwrite( fw, 1, bytes, ffw );
	  }
	  bytes = 0;
	  fprintf( ftxt, "    %02x %02x [0x%x]\n", rec[j], rec[j + 1],
	      bh.block );
	  word = 0;
	  bcnt = 0;
	  col = 0;
	  j++;
	  if( ch == 1 )
	  {
	    packet_type = rec[j];
	    fw[bytes++] = packet_type;
	  }
	  continue;
	}
	pre = 0;
      }
      if( ch == 2 )
      {
	word |= ( ( rec[j] >> 4 ) & 7 ) << bcnt;
	bcnt += 3;
	word |= ( ( rec[j] >> 0 ) & 7 ) << bcnt;
	bcnt += 3;
	if( bcnt >= 8 )
	{
	  if( bytes == 0 )
	    packet_type = word;
	  fw[bytes++] = word;
	  bcnt -= 8;
	  word >>= 8;
	}
      }
      else			// ch == 1
      {
	fw[bytes++] = rec[j];
      }
      fprintf( ftxt, "%02x ", rec[j] );
      col++;
      if( col > 15 )
      {
	fprintf( ftxt, "\n" );
	col = 0;
      }
    }
    fprintf( ftxt, "\n" );
    fclose( ftxt );
    if( bytes )
    {
      bh.last = 1;
      bh.bytes = bytes;
      fwrite( &bh, sizeof( struct fw_block ), 1, ffw );
      fwrite( fw, 1, bytes, ffw );
    }
    if( rec )
      free( rec );
    if( samples )
      free( samples );
    firmware_to_binary( ffw, fbin, ch );
    fclose( ffw );
    fclose( fbin );
  }
  exit( 0 );
}
