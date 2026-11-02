# Host build of the BSW examples and their unit tests.
# Folder rules: docs/adr/ADR-001-source-tree-layout.md
#
#   make test     build and run all module tests
#   make sim      build the interactive CanTp simulator (out/CanTp_Sim)
#   make clean    remove build output
#
# Add a new module <M>: create bsw/<M>/{inc,src}, gen/<M>, test/<M>, then
# append <M> to MODULES. Add a missing neighbor: create stubs/<Owner> and
# append <Owner> to STUBS.

CC      := gcc
CFLAGS  := -std=c99 -Wall -Wextra -Wpedantic -Werror -MMD -MP -pipe
OUT     := out
EXE     := $(if $(findstring mingw,$(shell $(CC) -dumpmachine)),.exe,)

# MinGW gcc/ld need a writable temp directory; keep it inside the build output.
export TMP  := $(OUT)/tmp
export TEMP := $(OUT)/tmp
$(shell mkdir -p $(OUT)/tmp)

MODULES := Dcm CanTp
STUBS   := General Rte PduR Det LSduR

# Public include path: public headers of every module, generated configuration,
# stub headers and the shared test helpers. bsw/<M>/src is NOT listed here.
PUBLIC_INC := $(foreach m,$(MODULES),-Ibsw/$(m)/inc -Igen/$(m)) \
              $(foreach s,$(STUBS),-Istubs/$(s)) \
              -Itest/Common

# Files under bsw/<M>/ and gen/<M>/ belong to module <M> and may also include
# its private headers from bsw/<M>/src. Everything else sees public headers only.
path_words  = $(subst /, ,$(1))
PRIVATE_INC = $(if $(filter bsw gen,$(firstword $(call path_words,$<))),\
                  -Ibsw/$(word 2,$(call path_words,$<))/src)

module_srcs = $(wildcard bsw/$(1)/src/*.c) $(wildcard gen/$(1)/*.c)
to_obj      = $(patsubst %.c,$(OUT)/obj/%.o,$(1))

STUB_SRCS        := $(foreach s,$(STUBS),$(wildcard stubs/$(s)/*.c))
COMMON_TEST_SRCS := $(wildcard test/Common/*.c)
TEST_BINS        := $(foreach m,$(MODULES),$(OUT)/Test_$(m)$(EXE))
SIM_BIN          := $(OUT)/CanTp_Sim$(EXE)

.PHONY: all test sim clean
all: $(TEST_BINS) $(SIM_BIN)

test: $(TEST_BINS)
	@for t in $^; do ./$$t || exit 1; done

sim: $(SIM_BIN)

clean:
	rm -rf $(OUT)

$(OUT)/obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(PUBLIC_INC) $(PRIVATE_INC) -c $< -o $@

# One test binary per module: module + stubs + shared helpers + test/<M>.
define TEST_BIN_RULE
$(OUT)/Test_$(1)$(EXE): $(call to_obj,$(call module_srcs,$(1)) $(STUB_SRCS) $(COMMON_TEST_SRCS) $(wildcard test/$(1)/*.c))
	$$(CC) $$^ -o $$@
endef
$(foreach m,$(MODULES),$(eval $(call TEST_BIN_RULE,$(m))))

# Interactive CanTp simulator: CanTp + stubs + tester-side frame codec + console.
# test/CanTp/sim/ is not part of the unit test binary (wildcard is not recursive).
$(OUT)/obj/test/CanTp/sim/%.o: PUBLIC_INC += -Itest/CanTp
$(SIM_BIN): $(call to_obj,$(call module_srcs,CanTp) $(STUB_SRCS) test/CanTp/TesterTp.c $(wildcard test/CanTp/sim/*.c))
	$(CC) $^ -o $@

-include $(shell find $(OUT) -name '*.d' 2>/dev/null)
