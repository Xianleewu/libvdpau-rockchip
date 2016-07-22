# Copyright (C) 2010 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

#CFLAGS += -Wall -Werror
#LDFLAGS += -avoid-version -module -shared -export-dynamic

# CFLAGS += -m32
LDFLAGS = -lpthread

TARGET = librkdec-h264d.so

SOURCES := \
	h264d.c \
	h264_stream.c

SOURCES += $(wildcard libvpu/h264_dec/*.c)

HEADERS += $(wildcard libvpu/h264_dec/*.h)

CINCLUDES= \
	-I. -Iinclude

all: $(TARGET)

$(TARGET): $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) $(CINCLUDES) -shared -fPIC \
		-Wl,-soname,$(TARGET) $(SOURCES) -o $@

clean:
	rm -f  $(TARGET)

install: $(TARGET)
	@cp $(TARGET) /usr/lib/arm-linux-gnueabihf/

.PHONY: all clean install
