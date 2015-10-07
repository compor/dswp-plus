# DSWP makefile

# variables
OUT_DIR=./libs/
PASS_LIB=libdswp.so

# compiler flags
CFLAGS += -MMD
CXXFLAGS = -rdynamic $(shell llvm-config --cxxflags) -g -O0

# targets

.PHONY: all runtime-tests gdb/% valgrind/% time/% tidy clean clean-examples

all: $(PASS_LIB) runtime/libruntime.a

PASS_OBJS = DSWP_0.o DSWP_1.o DSWP_2.o DSWP_3.o DSWP_4.o DSWP_5.o DSWP_DEBUG.o \
	        Utils.o raw_os_ostream.o
RUNTIME_OBJS = runtime/queue.o runtime/simple_sync.o runtime/runtime_debug.o
RT_TEST_OBJS = runtime/tests/sync_test.o runtime/tests/test.o
-include $(PASS_OBJS:%.o=%.d) $(RUNTIME_OBJS:%.o=%.d) $(RT_TEST_OBJS:%.o=%.d)


# the pass library
$(PASS_LIB): $(PASS_OBJS)
	$(CXX) -dylib -flat_namespace -shared -g -O0 $^ -o $@
# We're including raw_os_ostream.o because we can't just link in libLLVMSupport:
# http://lists.cs.uiuc.edu/pipermail/llvmdev/2010-June/032508.html

# the runtime library
runtime/libruntime.a: $(RUNTIME_OBJS)
	ar rcs $@ $^

# should really be using target-specific rules, but couldn't get them to work
runtime/tests/%.o: runtime/tests/%.c
	$(COMPILE.c) -pthread -Iruntime/ $< -o $@
runtime/%.o: runtime/%.c
	$(COMPILE.c) -pthread $< -o $@

runtime/tests/%: runtime/tests/%.o runtime/libruntime.a
	$(LINK.o) -pthread $^ -o $@
runtime-tests: runtime/tests/test runtime/tests/sync_test
	runtime/tests/test
	runtime/tests/sync_test

### compiling examples to bitcode
Example/%.bc: Example/%.c
	clang -O0 -c -emit-llvm $< -o $@
Example/%.bc: Example/%.cpp
	clang++ -O0 -c -emit-llvm $< -o $@

### optimizing bitcode
Example/%-m2r.bc: Example/%.bc
	opt -mem2reg $< -o $@
Example/%-o2.bc: Example/%.bc
	opt -O2 $< -o $@
Example/%-o3.bc: Example/%.bc
	opt -O3 $< -o $@
Example/%-DSWP.bc: Example/%.bc $(LIB_PASS)
	opt -load ./$(LIB_PASS) -dswp $< -o $@
gdb/%: Example/%.bc $(LIB_PASS)
	gdb --args opt -load ./$(LIB_PASS) -dswp $< -o /dev/null
valgrind/%: Example/%.bc $(LIB_PASS)
	valgrind $(VALGRIND_ARGS) opt -load ./$(LIB_PASS) -dswp $< -o /dev/null

### (dis)assembling bitcode
Example/%.bc.ll: Example/%.bc
	llvm-dis $< -o $@
Example/%.bc.ll.ps: Example/%.bc.ll
	enscript $< -q -E -fCourier10 --tabsize=4 -p $@
Example/%.bc.ll.pdf: Example/%.bc.ll.ps
	ps2pdf $< $@
Example/%.o: Example/%.bc
	clang -O0 -c $< -o $@
Example/%.out: Example/%.bc runtime/libruntime.a
	clang -O0 -pthread $< runtime/libruntime.a -lm -o $@
run/%: Example/%.out
	$<
time/%: Example/%.out
	time $<

### cleaning up
tidy:
	rm -f *.o runtime/*.o runtime/tests/*.o *.d runtime/*.d runtime/tests/*.d
tidy-output:
	rm -f dag partition showgraph
clean-examples: tidy-output
	rm -f Example/*.bc Example/*.bc.ll Example/*.o Example/*.out
clean-objs: tidy
	rm -f $(LIB_PASS) runtime/libruntime.a runtime/tests/test runtime/tests/sync_test
clean: clean-objs clean-examples
