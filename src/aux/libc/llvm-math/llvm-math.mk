# Portable LLVM libc math; all upstream headers are staged before compilation.
LIBC_MATH_DIR := $(LIBC_DIR)/llvm-math
LIBC_MATH_BUILD := $(LIBC_BUILD)/llvm-math
LIBC_MATH_UPSTREAM := $(REPO_ROOT)/submodules/llvm-project/libc
LIBC_MATH_INPUTS := $(shell find "$(LIBC_MATH_UPSTREAM)/hdr" "$(LIBC_MATH_UPSTREAM)/src/__support" "$(LIBC_MATH_UPSTREAM)/include/llvm-libc-macros" "$(LIBC_MATH_UPSTREAM)/include/llvm-libc-types" -type f)
LIBC_MATH_STAGE := $(LIBC_MATH_BUILD)/source/.prepared
LIBC_MATH_OBJECT := $(LIBC_MATH_BUILD)/math.o
LIBC_OBJECTS += $(LIBC_MATH_OBJECT)
LIBC_MATH_CXX ?= clang++

$(LIBC_MATH_STAGE): $(LIBC_MATH_INPUTS) $(LIBC_MATH_DIR)/llvm-math.mk
	@case "$(abspath $(LIBC_MATH_BUILD))" in \
		"$(REPO_ROOT)/build/"*) ;; \
		*) echo 'error: libc math staging must stay under build/' >&2; exit 1 ;; \
	esac
	mkdir -p "$(@D)/src" "$(@D)/include"
	chmod -R u+w "$(@D)"
	cp -a "$(LIBC_MATH_UPSTREAM)/include/llvm-libc-types" "$(@D)/include/"
	cp -a "$(LIBC_MATH_UPSTREAM)/include/llvm-libc-macros" "$(@D)/include/"
	cp -a "$(LIBC_MATH_UPSTREAM)/hdr" "$(@D)/"
	cp -a "$(LIBC_MATH_UPSTREAM)/src/__support" "$(@D)/src/"
	cp "$(REPO_ROOT)/submodules/llvm-project/libc/LICENSE.TXT" "$(@D)/LICENSE.TXT"
	chmod -R u+w "$(@D)"
	touch "$@"

$(LIBC_MATH_OBJECT): $(LIBC_MATH_DIR)/math.cpp $(LIBC_MATH_STAGE) $(SDK_HEADERS)
	$(LIBC_MATH_CXX) $(CPPFLAGS) $(CFLAGS) $(filter-out -std=c11,$(LIBC_CFLAGS)) -std=c++17 \
		-fno-exceptions -fno-rtti -DLIBC_NAMESPACE=waste_llvm_libc \
		-DLIBC_ERRNO_MODE=6 -DLIBC_MATH_HAS_SKIP_ACCURATE_PASS \
		-DLIBC_MATH_HAS_NO_EXCEPT -DLIBC_MATH_HAS_ERRNO \
		-I"$(LIBC_MATH_BUILD)/source" -MMD -MP -c "$<" -o "$@"

-include $(LIBC_MATH_OBJECT:.o=.d)
