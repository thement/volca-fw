CC=gcc
XCC=i686-w64-mingw32-gcc
XLD=i686-w64-mingw32-ld
XAR=i686-w64-mingw32-ar
XNM=i686-w64-mingw32-nm
XRANLIB=i686-w64-mingw32-ranlib
XDLLTOOL=i686-w64-mingw32-dlltool
XOBJDUMP=i686-w64-mingw32-objdump
XSTRIP=i686-w64-mingw32-strip
XAS=i686-w64-mingw32-as
XDLLTOOL=i686-w64-mingw32-dlltool
XDLLWRAP=i686-w64-mingw32-dllwrap
LC_ALL=C
SED=sed
PROGS=volca_fw_encode volca_fw_decode

CFLAGS=-Wall
XCFLAGS=-Wall

all: $(PROGS)

ENCODE_OBJ = volca_fw_encode.o volca_syro.o volca_syro_func.o
DECODE_OBJ = volca_fw_decode.o volca_syro_func.o
ENCODE_WOBJ = volca_fw_encode.obj volca_syro.obj volca_syro_func.obj
DECODE_WOBJ = volca_fw_decode.obj volca_syro_func.obj

%.o:    %.c
	$(CC) $(CFLAGS) -c $<

%.obj: %.c $(INC)
	$(XCC) $(XCFLAGS) -c $< -o $@

volca_fw_encode: $(ENCODE_OBJ)
	$(CC) $(CFLAGS) -s -o $@ $^

volca_fw_encode.exe: $(ENCODE_WOBJ)
	$(XCC) $(CFLAGS) -o $@ $^
	$(XSTRIP) --strip-all $@

volca_fw_decode: $(DECODE_OBJ)	
	$(CC) $(CFLAGS) -o $@ $^

volca_fw_decode.exe: $(DECODE_WOBJ)	
	$(XCC) $(CFLAGS) -o $@ $^
	$(XSTRIP) --strip-all $@

wav:	volca_fw_encode
	dd if=/dev/urandom of=random.data count=128
	./$< random.data

.PHONY: clean

clean:
	rm -f *.o random.* $(PROGS)

