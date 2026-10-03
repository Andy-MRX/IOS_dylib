# Makefile - 在 macOS + Xcode 环境编译 arm64 + arm64e fat dylib
# Apple clang 会生成真正的 PAC（ptrauth）代码，满足 arm64e 系统进程要求

TARGET := payload.dylib
SDK    := $(shell xcrun --sdk iphoneos --show-sdk-path)
CC     := xcrun --sdk iphoneos clang
INSTALL_NAME := /var/jb/Library/MobileSubstrate/DynamicLibraries/payload.dylib

ARCHS  := -arch arm64 -arch arm64e
CFLAGS := -O2 -Wall -Wno-unused-function -fPIC
LDFLAGS := -dynamiclib -isysroot $(SDK) -install_name $(INSTALL_NAME)

all: $(TARGET)

$(TARGET): wrapper.c payload_blob.h
	$(CC) $(ARCHS) $(CFLAGS) $(LDFLAGS) -o $@ wrapper.c
	ldid -S $@

clean:
	rm -f $(TARGET)
