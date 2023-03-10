include $(abs_top_srcdir)/Makefrag

# delete all the tests under "tests" when running the provided binary, to prevent overriding copied binaries
tests = \
	priority_mp8_dynamic_h \
	priority_mp8_static_h \
	priority_mp8_dynamica_h \
	priority_mp8_statica_h \
	priority_mp8_dynamicb_h \
	priority_mp8_staticb_h \
	priority_mp8_dynamic_l \
	priority_mp8_static_l \
	priority_mp8_dynamica_l \
	priority_mp8_statica_l \
	priority_mp8_dynamicb_l \
	priority_mp8_staticb_l \
	priority_mp8_dynamic \
	priority_mp8_static \
	priority_mp8_dynamica \
	priority_mp8_statica \
	priority_mp8_dynamicb \
	priority_mp8_staticb 

tests_baremetal = $(tests:=-baremetal)
runs_baremetal = $(addsuffix .run,$(tests_baremetal))
ifdef BAREMETAL_ONLY
	tests_linux =
else
	tests_linux = $(tests:=-linux)
endif

BENCH_COMMON = $(abs_top_srcdir)/riscv-tests/benchmarks/common
GEMMINI_HEADERS = $(abs_top_srcdir)/include/gemmini.h $(abs_top_srcdir)/include/gemmini_params.h $(abs_top_srcdir)/include/gemmini_nn.h $(abs_top_srcdir)/include/gemmini_testutils.h

CFLAGS := $(CFLAGS) \
	-DPREALLOCATE=1 \
	-DMULTITHREAD=1 \
	-mcmodel=medany \
	-std=gnu99 \
	-O2 \
	-ffast-math \
	-fno-common \
	-fno-builtin-printf \
	-march=rv64gc -Wa,-march=rv64gcxhwacha \
	-lm \
	-lgcc \
	-lpthread \
	-I$(abs_top_srcdir)/riscv-tests \
	-I$(abs_top_srcdir)/riscv-tests/env \
	-I$(abs_top_srcdir) \
	-I$(BENCH_COMMON) \
	-DID_STRING=$(ID_STRING) \
	-Wno-incompatible-pointer-types

CFLAGS_BAREMETAL := \
	$(CFLAGS) \
	-nostdlib \
	-nostartfiles \
	-static \
	-T $(BENCH_COMMON)/test.ld \
	-DBAREMETAL=1 \

all: $(tests_linux)

vpath %.c $(src_dir)

%-baremetal: %.c $(src_dir)/images.h $(GEMMINI_HEADERS)
	$(CC_BAREMETAL) $(CFLAGS_BAREMETAL) $< $(LFLAGS) -o $@ \
		$(wildcard $(BENCH_COMMON)/*.c) $(wildcard $(BENCH_COMMON)/*.S) $(LIBS)

%-linux: %.c $(src_dir)/images.h $(GEMMINI_HEADERS)
	$(CC_LINUX) $(CFLAGS) $< $(LFLAGS) -o $@

run-baremetal: $(runs_baremetal)

%-baremetal.run: %-baremetal
	$(RUNNER)$^

junk += $(tests_baremetal) $(tests_linux)

