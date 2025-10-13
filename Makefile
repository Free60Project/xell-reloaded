CROSS=xenon-
CC=$(CROSS)gcc
OBJCOPY=$(CROSS)objcopy
LD=$(CROSS)ld
AS=$(CROSS)as
STRIP=$(CROSS)strip

LV1_DIR=source/lv1
GITREV='$(shell git describe --tags)'

# Configuration
CFLAGS = -Wall -Os -I$(LV1_DIR) -ffunction-sections -fdata-sections \
	-m64 -mno-toc -DBYTE_ORDER=BIG_ENDIAN -mno-altivec -D$(CURRENT_TARGET) $(CYGNOS_DEF)

AFLAGS = -Iinclude -m64
LDFLAGS = -nostdlib -n -m64 -Wl,--gc-sections

OBJS =	$(LV1_DIR)/startup.o \
	$(LV1_DIR)/main.o \
	$(LV1_DIR)/cache.o \
	$(LV1_DIR)/ctype.o \
	$(LV1_DIR)/string.o \
	$(LV1_DIR)/time.o \
	$(LV1_DIR)/vsprintf.o \
	$(LV1_DIR)/puff/puff.o

TARGETS = xell-1f xell-2f xell-gggggg xell-gggggg_cygnos_demon xell-1f_cygnos_demon xell-2f_cygnos_demon

# Build rules
all: $(foreach name,$(TARGETS),$(addprefix $(name).,build))

.PHONY: clean %.build

clean:
	@echo Cleaning...
	@$(MAKE) --no-print-directory -f Makefile_lv2.mk clean
	@rm -rf $(OBJS) $(foreach name,$(TARGETS),$(addprefix $(name).,bin elf)) stage2.elf32.gz stage2.elf32.7z
	
dist: clean all
	@rm -rf XeLL_Reloaded-2stages-*.tar.gz
	@mkdir -p release/_DEBUG
	@cp *.bin release/
	@gunzip *.gz
	@cp stage2.elf release/_DEBUG
	@cp stage2.elf32 release/
	@cp AUTHORS release/
	@cp CHANGELOG release/
	@cp README release/
	@cd release; tar czvf XeLL_Reloaded-2stages-$(GITREV).tar.gz *; mv *.tar.gz ..
	@rm -rf release 
	@$(MAKE) clean

%.build:
	@echo Building $* ...
	@$(MAKE) --no-print-directory $*.bin

.c.o:
	@echo [$(notdir $<)]
	@$(CC) $(CFLAGS) -c -o $@ $*.c

.S.o:
	@echo [$(notdir $<)]
	@$(CC) $(AFLAGS) -c -o $@ $*.S

xell-gggggg.elf: CURRENT_TARGET = HACK_GGGGGG
xell-1f.elf xell-2f.elf: CURRENT_TARGET = HACK_JTAG

xell-gggggg_cygnos_demon.elf: CURRENT_TARGET = HACK_GGGGGG
xell-gggggg_cygnos_demon.elf: CYGNOS_DEF = -DCYGNOS
xell-1f_cygnos_demon.elf xell-2f_cygnos_demon.elf: CURRENT_TARGET = HACK_JTAG
xell-1f_cygnos_demon.elf xell-2f_cygnos_demon.elf: CYGNOS_DEF = -DCYGNOS

%.elf: $(LV1_DIR)/%.lds $(OBJS)
	@$(CC) -n -T $< $(LDFLAGS) -o $@ $(OBJS)

%.bin:
# Build stage 1
	@$(MAKE) --no-print-directory $*.elf
# Build stage 2
	@rm -f stage2.elf32.gz
	@rm -rf $(OBJS)
	@$(MAKE) --no-print-directory -f Makefile_lv2.mk
	@$(STRIP) stage2.elf32
	@gzip -n9 stage2.elf32	
# 256KB - 16KB of stage 2 - FOOTER bytes == 245744
	@test `stat -L -c %s stage2.elf32.gz` -le 245744 || (echo "stage2.elf32.gz too large!"; exit 1)
	@$(OBJCOPY) -O binary $*.elf $@
# Ensure stage1 is small enough (<= 16KB)
	@test `stat -L -c %s $@` -le 16384 || (echo "Too large"; exit 1)
	@truncate --size=262128 $@ # 256k - footer size
	@echo -n "xxxxxxxxxxxxxxxx" >> $@ # add footer
	@dd if=stage2.elf32.gz of=$@ conv=notrunc bs=16384 seek=1 # inject stage2
