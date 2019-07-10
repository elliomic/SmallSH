SHELL 		= /bin/sh
CC			= gcc
CCFLAGS		= -ansi -pedantic -Wall -Wextra\
			  #-Wtraditional
TARGET		= smallsh
HEADERS		= $(TARGET).h
SRCS		= $(TARGET).c
OBJS		=

.PHONY: clean all debug

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CCFLAGS) $(SRCS) -o $@

debug: $(OBJS)
	$(CC) -g $(CCFLAGS) $(SRCS) -o $(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)
