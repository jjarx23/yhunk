TITLE := yhunk
OUT := $(TITLE)
CC := gcc
CFLAGS := -c 
LDFLAGS := -L$$HOME -lpthread
GCFLAGS := -ggdb -Og
OBJDIR := ../builds/$(TITLE)_obj/
SRCDIRS := $(wildcard */*/*/) $(wildcard */*/) $(wildcard */)
OBJDIRS := $(OBJDIR) $(SRCDIRS:%/=$(OBJDIR)%/)
SRC := $(wildcard */*/*/*.c */*/*.c */*.c *.c)
OBJ := $(SRC:%.c=$(OBJDIR)%.o)

ifneq ($(debug),1)
CFLAGS += -O3
else
CFLAGS +=  $(GCFLAGS)
endif

ifeq ($(as),dyn)
CFLAGS += -D AS_LIB -fPIC
LDFLAGS += -shared
OUT := lib$(TITLE).so
else
ifeq ($(as), static)
CFLAGS := -D AS_LIB
OUT := lib$(OUT).a
endif
endif

ALL: PREBUILD APP POSTBUILD
	@echo $(OUT) built
	@echo

PREBUILD: $(OBJDIRS)
	@echo created obj dirs: $(OBJDIRS)
	@echo

%/:
	@mkdir -p $@

APP: $(OBJ)
ifeq ($(as), static)
	@arc rcs $(OUT) $^
else
	@$(CC) $(LDFLAGS) $^ -o $(OUT)
endif
	@echo

$(OBJDIR)%.o:%.c
	@$(CC) $(CFLAGS) $^ -o $@
	@echo $@ built
	@echo $(CFLAGS)

POSTBUILD:
	@mv $(OUT) $$HOME
	@chmod +x $$HOME/$(OUT)
	@echo done!
	@echo

clean:
	@rm -r $(OBJDIR)
	@echo $(OBJDIR) cleaned
	@echo

.PHONY: ALL PREBUILD APP POSTBUILD clean
