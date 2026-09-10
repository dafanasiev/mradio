# Thin wrapper over CMake. Nothing here is required to build the project -
# `cmake -S . -B build -G Ninja && cmake --build build` still works - but these
# are the commands worth having under the fingers.

CMAKE       ?= cmake
CTEST       ?= ctest
GENERATOR   ?= Ninja
BUILD_DIR   ?= build
JOBS        ?= $(shell nproc 2>/dev/null || echo 4)

# Warnings are errors by default: the strict set in cmake/MradioPackage.cmake
# only earns its keep if a warning actually stops the build.
CMAKE_FLAGS ?= -DMRADIO_WERROR=ON

COMMA := ,

.DEFAULT_GOAL := build

.PHONY: build
build: ## Configure and build into build/
	@$(CMAKE) -S . -B $(BUILD_DIR) -G $(GENERATOR) $(CMAKE_FLAGS)
	@$(CMAKE) --build $(BUILD_DIR) -j $(JOBS)

.PHONY: test
test: build ## Build, then run the whole test suite
	@$(CTEST) --test-dir $(BUILD_DIR) --output-on-failure -j $(JOBS)

# Each sanitizer gets a build directory of its own, so switching between them
# and back to a normal build never costs a full rebuild.
#
# Underscores in the suffix become commas, because -fsanitize takes a list and
# a comma cannot appear in a make target name: sanitize_address_undefined
# builds with -fsanitize=address,undefined.
#
# address and thread cannot be combined; that is a limitation of the sanitizers
# themselves, not of this Makefile.
#
# These are deliberately neither listed in .PHONY nor given stub rules: make
# skips the implicit-rule search for a .PHONY target, and an explicit rule with
# an empty recipe shadows the pattern rule. Either one turns `make
# sanitize_thread` into a silent no-op. Hence the HELP comments below, which
# document the variants without declaring them as targets.
#
# HELP: sanitize_address|Build and test with -fsanitize=address
# HELP: sanitize_undefined|Build and test with -fsanitize=undefined
# HELP: sanitize_thread|Build and test with -fsanitize=thread
# HELP: sanitize_address_undefined|Build and test with -fsanitize=address,undefined
sanitize_%:
	@$(CMAKE) -S . -B build-$* -G $(GENERATOR) $(CMAKE_FLAGS) \
	    -DMRADIO_SANITIZE=$(subst _,$(COMMA),$*)
	@$(CMAKE) --build build-$* -j $(JOBS)
	@$(CTEST) --test-dir build-$* --output-on-failure -j $(JOBS)

.PHONY: clean
clean: ## Remove every build directory
	@rm -rf $(BUILD_DIR) build-*
	@echo "removed build directories"

.PHONY: help
help: ## List the targets
	@{ grep -hE '^[a-zA-Z_-]+:.*?## ' $(MAKEFILE_LIST) \
	     | sed -E 's/:.*## /|/'; \
	   grep -hE '^# HELP: ' $(MAKEFILE_LIST) | sed 's/^# HELP: //'; } \
	  | sort -t'|' -k1,1 \
	  | awk -F'|' '{printf "  \033[1m%-28s\033[0m %s\n", $$1, $$2}'
