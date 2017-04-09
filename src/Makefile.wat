#
# THIS FILE IS DEPRECATED AND WILL BE REMOVED IN A FUTURE RELEASE
#
# Wmake File for seisan2mseed - For Watcom's wmake
# Use 'wmake -f Makefile.wat'

.BEFORE
	@set INCLUDE=.;$(%watcom)\H;$(%watcom)\H\NT
	@set LIB=.;$(%watcom)\LIB386

cc     = wcc386
cflags = -zq
lflags = OPT quiet OPT map LIBRARY ..\libmseed\libmseed.lib
cvars  = $+$(cvars)$- -DWIN32

BIN = ..\seisan2mseed.exe

INCS = -I..\libmseed

all: $(BIN)

$(BIN):	seisan2mseed.obj
	wlink $(lflags) name $(BIN) file {seisan2mseed.obj}

# Source dependencies:
seisan2mseed.obj:	seisan2mseed.c

# How to compile sources:
.c.obj:
	$(cc) $(cflags) $(cvars) $(INCS) $[@ -fo=$@

# Clean-up directives:
clean:	.SYMBOLIC
	del *.obj *.map $(BIN)
