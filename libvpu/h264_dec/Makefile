#!/bin/bash

# Makefile using libtool for vpu common library.

TOP_DIR = $(shell pwd)

# define compiler
CROSS_COMPILER ?=
ifeq ($(origin CC), default)
CC = $(CROSS_COMPILER)gcc
STRIP = $(CROSS_COMPILER)strip
endif 
 	
# define install
SDK_DIR ?= $(TOP_DIR)/../../../sdk

# define the source code
SRC_CODE := $(wildcard *.c)

INCLUDE = -I. -I$(TOP_DIR)

# define generation
OBJ = $(SRC_CODE:.c=.o)

LIB_NAME = libvpu_avcdec.so

# define the build flag
CFLAGS += $(INCLUDE) -fPIC
LDFLAGS = 

# build steps

.PHONY : all install clean

all : $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -shared $(OBJ) -o $(LIB_NAME) 

$(OBJ) : %.o : %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean :
	@rm -rf *.o *.so

install : all
	@mkdir -p $(SDK_DIR)/lib
	@cp $(LIB_NAME) $(SDK_DIR)/lib
