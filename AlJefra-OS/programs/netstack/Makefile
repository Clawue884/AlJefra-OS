# =============================================================================
# AlJefra OS — Network Stack + TLS + HTTP + AI Agent Build
# =============================================================================

# Toolchain
CC = gcc
LD = ld
NASM = nasm

# Paths
DEMO_SRC = ../../src/aljefra-demo/src
API_SRC = ../../aljefra/api
BEARSSL_DIR = bearssl

# Compiler flags (AlJefra freestanding)
CFLAGS = -c -m64 -nostdlib -nostartfiles -nodefaultlibs -ffreestanding \
         -fno-pic -fno-pie -mcmodel=large \
         -falign-functions=16 -fomit-frame-pointer -mno-red-zone \
         -fno-builtin -fno-stack-protector -fcf-protection=none -Os \
         -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 \
         -DBR_AES_X86NI=0 -DBR_SSE2=0 \
         -DBR_USE_URANDOM=0 -DBR_USE_GETENTROPY=0 -DBR_USE_UNIX_TIME=0 \
         -DBR_USE_WIN32_RAND=0 -DBR_USE_WIN32_TIME=0 -DBR_RDRAND=0 \
         -I$(API_SRC) -I. -I$(BEARSSL_DIR)/inc
LDFLAGS = -T netstack.ld

# Source files
NETSTACK_SRCS = netstack.c arp.c ipv4.c icmp.c udp.c tcp.c dns.c checksum.c util.c
TLS_SRCS = tls.c certs.c
HTTP_SRCS = http.c json.c agent.c

MAIN_SRCS = main.c
SHIM_SRCS = libc_shim.c

ALL_SRCS = $(NETSTACK_SRCS) $(TLS_SRCS) $(HTTP_SRCS) $(MAIN_SRCS) $(SHIM_SRCS)
ALL_OBJS = $(ALL_SRCS:.c=.o)

# BearSSL objects (compiled from vendored source)
BEARSSL_SRCS = $(wildcard $(BEARSSL_DIR)/src/*.c) \
               $(wildcard $(BEARSSL_DIR)/src/aead/*.c) \
               $(wildcard $(BEARSSL_DIR)/src/codec/*.c) \
               $(wildcard $(BEARSSL_DIR)/src/ec/*.c) \
               $(wildcard $(BEARSSL_DIR)/src/hash/*.c) \
               $(wildcard $(BEARSSL_DIR)/src/int/*.c) \
               $(wildcard $(BEARSSL_DIR)/src/kdf/*.c) \
               $(wildcard $(BEARSSL_DIR)/src/mac/*.c) \
               $(wildcard $(BEARSSL_DIR)/src/rand/*.c) \
               $(wildcard $(BEARSSL_DIR)/src/rsa/*.c) \
               $(wildcard $(BEARSSL_DIR)/src/ssl/*.c) \
               $(wildcard $(BEARSSL_DIR)/src/symcipher/*.c) \
               $(wildcard $(BEARSSL_DIR)/src/x509/*.c)
BEARSSL_OBJS = $(patsubst %.c,%.o,$(BEARSSL_SRCS))

# Runtime objects
CRT0_OBJ = crt0.o
LIBALJEFRA_OBJ = libAlJefra.o

# Output
TARGET = ai_agent.app

.PHONY: all clean bearssl

all: $(TARGET)

# Build runtime objects
$(CRT0_OBJ): $(DEMO_SRC)/crt0.c
	$(CC) $(CFLAGS) -o $@ $<

$(LIBALJEFRA_OBJ): $(API_SRC)/libAlJefra.c $(API_SRC)/libAlJefra.h
	$(CC) $(CFLAGS) -o $@ $<

# Build netstack/TLS/HTTP objects
%.o: %.c netstack.h
	$(CC) $(CFLAGS) -o $@ $<

# Build BearSSL objects
$(BEARSSL_DIR)/src/%.o: $(BEARSSL_DIR)/src/%.c
	$(CC) $(CFLAGS) -I$(BEARSSL_DIR)/src -o $@ $<

# Link everything
$(TARGET): $(CRT0_OBJ) $(LIBALJEFRA_OBJ) $(ALL_OBJS) $(BEARSSL_OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

# Build without BearSSL (for testing TCP/IP only)
nettest_main.o: nettest_main.c netstack.h
	$(CC) $(CFLAGS) -o $@ $<

nettest.app: $(CRT0_OBJ) $(LIBALJEFRA_OBJ) nettest_main.o $(NETSTACK_SRCS:.c=.o)
	$(LD) $(LDFLAGS) -o $@ $^

minitest.o: minitest.c netstack.h
	$(CC) $(CFLAGS) -o $@ $<

minitest.app: $(CRT0_OBJ) $(LIBALJEFRA_OBJ) minitest.o $(NETSTACK_SRCS:.c=.o)
	$(LD) $(LDFLAGS) -o $@ $^

tlstest.o: tlstest.c netstack.h tls.h
	$(CC) $(CFLAGS) -o $@ $<

TLS_TEST_OBJS = $(NETSTACK_SRCS:.c=.o) $(TLS_SRCS:.c=.o) libc_shim.o
tlstest.app: $(CRT0_OBJ) $(LIBALJEFRA_OBJ) tlstest.o $(TLS_TEST_OBJS) $(BEARSSL_OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

clean:
	rm -f *.o $(TARGET) nettest.app minitest.app tlstest.app
	rm -f $(BEARSSL_OBJS)
