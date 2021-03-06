PROJECT=winde
MCU = atmega64
TARGET = $(PROJECT).elf
CC = avr-gcc
OBJECTS = winde.o

## Options common to compile, link and assembly rules
COMMON = -mmcu=$(MCU)

## Compile options common for all C compilation units.
CFLAGS = $(COMMON)
CFLAGS += -gdwarf-2 -std=gnu1x -DF_CPU=4000000UL -Os -funsigned-char -funsigned-bitfields -fpack-struct -fshort-enums -save-temps
CFLAGS += -MD -MP -MT $(*F).o -MF dep/$(@F).d
CFLAGS += '-DVERSION="1.0"' -DGIT_VERSION="\"`git describe --all --long`\""
CFLAGS += -Wall

INCLUDES=-I..

## Assembly specific flags
ASMFLAGS = $(COMMON)
ASMFLAGS += $(CFLAGS)
ASMFLAGS += -x assembler-with-cpp -Wa,-gdwarf2

LIBS =
LIBDIRS =

## Linker flags
LDFLAGS = $(COMMON)
LDFLAGS +=  -Wl,-Map=$(PROJECT).map
LDFLAGS += -T /usr/lib/ldscripts/avr5.x

## Intel Hex file production flags
HEX_FLASH_FLAGS = -R .eeprom -R .fuse -R .lock -R .signature

HEX_EEPROM_FLAGS = -j .eeprom
HEX_EEPROM_FLAGS += --set-section-flags=.eeprom="alloc,load"
HEX_EEPROM_FLAGS += --change-section-lma .eeprom=0 --no-change-warnings

## Build
all: $(TARGET) $(PROJECT).hex $(PROJECT).eep $(PROJECT).lss size

## Compile
%.o: ../%.c
	$(CC) $(INCLUDES) $(CFLAGS) -c  $<

##Link
$(TARGET): $(OBJECTS)
	 $(CC) $(LDFLAGS) $(OBJECTS) $(LIBDIRS) $(LIBS) -o $(TARGET)

%.hex: $(TARGET)
	avr-objcopy -O ihex $(HEX_FLASH_FLAGS)  $< $@

%.eep: $(TARGET)
	-avr-objcopy $(HEX_EEPROM_FLAGS) -O ihex $< $@ || exit 0

%.lss: $(TARGET)
	avr-objdump -h -S $< > $@

size: ${TARGET}
	@echo
	@avr-size ${TARGET}

## Clean target
.PHONY: clean
clean:
	-rm -rf $(OBJECTS) $(PROJECT).elf dep/* $(PROJECT).hex $(PROJECT).eep $(PROJECT).lss $(PROJECT).map

## Other dependencies
-include $(shell mkdir dep 2>/dev/null) $(wildcard dep/*)

dude: all
	avrdude -V -p $(MCU) -c jtag2 -P usb -e -U $(PROJECT).hex -v
