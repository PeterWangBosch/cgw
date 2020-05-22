PROG = orchestrator
MODULE_CFLAGS=-DMG_ENABLE_THREADS -DMG_ENABLE_HTTP_STREAMING_MULTIPART -DMG_ENABLE_HTTP_WEBSOCKET=0

SOURCES = $(PROG).c src/bs_core.c src/file_utils.c src/bs_eth_installer_job.c src/bs_dlc_apis.c src/bs_tdr_job.c mongoose/mongoose.c cJSON/cJSON.c
CFLAGS = -g -W -Wall -Werror -I./ -Wno-unused-function $(CFLAGS_EXTRA) $(MODULE_CFLAGS)

all: $(PROG)

ifeq ($(OS), Windows_NT)
# TODO(alashkin): enable SSL in Windows
CFLAGS += -lws2_32
CC = gcc
else
CFLAGS += -pthread
endif

ifeq ($(SSL_LIB),openssl)
CFLAGS += -DMG_ENABLE_SSL -lssl -lcrypto
endif
ifeq ($(SSL_LIB), krypton)
CFLAGS += -DMG_ENABLE_SSL ../../../krypton/krypton.c -I../../../krypton
endif
ifeq ($(SSL_LIB),mbedtls)
CFLAGS += -DMG_ENABLE_SSL -DMG_SSL_IF=MG_SSL_IF_MBEDTLS -DMG_SSL_MBED_DUMMY_RANDOM -lmbedcrypto -lmbedtls -lmbedx509
endif

ifdef ASAN
CC = clang
CFLAGS += -fsanitize=address
endif

$(PROG): $(SOURCES)
	$(CC) $(SOURCES) -o $@ $(CFLAGS)

$(PROG).exe: $(SOURCES)
	cl $(SOURCES) /I../.. /MD /Fe$@

clean:
	rm -rf *.gc* *.dSYM *.exe *.obj *.o a.out $(PROG)
