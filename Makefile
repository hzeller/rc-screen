# <h.zeller@acm.org>
##

TARGET_ARCH=-mmcu=attiny24
CXX=avr-g++
CXXFLAGS=-Os -g -Wall -mcall-prologues
AVRDUDE     = avrdude -p t24 -c avrusb500
FLASH_CMD   = $(AVRDUDE) -e -U flash:w:main.hex
LINK=avr-g++ -g $(TARGET_ARCH)
OBJECTS=rc-screen.o

all : main.hex

main.elf: $(OBJECTS)
	$(LINK) -o $@ $(OBJECTS)
	avr-size $@
	avr-objdump -S main.elf > /tmp/screen-`avr-size -d main.elf | grep main.elf | awk '{print $$4}'`.S

disasm: main.elf
	avr-objdump -S main.elf

main.hex: main.elf
	avr-objcopy -j .text -j .data -O ihex main.elf main.hex

flash: main.hex
	$(FLASH_CMD)

clean:
	rm -f $(OBJECTS) main.elf main.hex

# internal oscillator.
### Fuse high byte: 0xDD
# 7 rstdisbl	1   disable external reset: disabled (i.e.: reset enabled).
# 6 dwen	1   debug wire: yes.
# - spien	0   serial programming: enabled.
# 4 wdton	1   watchdog timer on: disabled.
#
# 3 eesave      1   save eeprom on chip erase: disabled.
# 2 bodlevel2	1\
# 1 bodlevel1	0 +  brown out detection 2.7 Volt (page 308)
# 0 bodlevel0	1/

### low byte:
# 7 ckdiv8	1   divide by 8: no
# 6 ckout	1   clk out: disabled.
# 5 sut1	1-+ crystal oscillator, fast rising power.
# 4 sut0	0/
#
# 3 cksel3	0\
# 2 cksel2	0 + internal RC oscillator  (page 31)
# 1 cksel1	1/
# 0 cksel0	0   chrystal with SUT 10 -> crystal oscillator, fast rising power.
fuse:
	$(AVRDUDE) -U hfuse:w:0xdd:m -U lfuse:w:0xe2:m
