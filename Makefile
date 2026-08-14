# Makefile — the only build entry point for elc.
#
# Non-recursive: one top-level Makefile, no `make -C` into subdirectories.
# Conventions are binding and documented in
# .github/skills/elocker-dev/SKILL.md ("Build").

.DEFAULT_GOAL := help

# ---------------------------------------------------------------- toolchain
# Every variable here is overridable from the command line or environment.
CC          ?= cc
PKG_CONFIG  ?= pkg-config
PREFIX      ?= /usr/local
DESTDIR     ?=
BATS        ?= test/helpers/bats-core/bin/bats

WARNINGS    := -Wall -Wextra -Wpedantic
# _XOPEN_SOURCE/_DEFAULT_SOURCE are required for fts(3) on glibc and must be
# set before any include; they live here rather than in the .c files.
CPPFLAGS    += -Iinclude -D_XOPEN_SOURCE=700 -D_DEFAULT_SOURCE
CFLAGS      ?= -O2 -g
CFLAGS      += -std=c11 $(WARNINGS) -MMD -MP
LDFLAGS     +=
LDLIBS      +=

# ------------------------------------------------------------------- layout
BUILD       := build
BIN         := $(BUILD)/elc
SRC         := $(wildcard src/*.c)
OBJ         := $(patsubst src/%.c,$(BUILD)/%.o,$(SRC))
DEP         := $(OBJ:.o=.d)

# Objects for the unit level: every module except main.c, which owns the
# real entry point and would collide with the test runner's.
LIB_SRC     := $(filter-out src/main.c,$(SRC))
LIB_OBJ     := $(patsubst src/%.c,$(BUILD)/%.o,$(LIB_SRC))

UNIT_SRC    := $(wildcard test/unit/*.c)
UNIT_BIN    := $(patsubst test/unit/%.c,$(BUILD)/unit/%,$(UNIT_SRC))

# Symbols intercepted at link time for unit mocking (STP §2.2). Wrapping is
# confined to the unit level; every other level links the real binary.
# The canonical per-module inventory lives in doc/SDD.md's data dictionary.
WRAP_SYMS   ?= malloc
WRAP_FLAGS  := $(addprefix -Wl$(comma)--wrap=,$(WRAP_SYMS))
comma       := ,

# --------------------------------------------------------------------- help
.PHONY: help
help: ## Display this help message
	@awk 'BEGIN {FS = ":.*##"; printf "\nUsage:\n  make \033[36m<target>\033[0m\n\nTargets:\n"} \
		/^[a-zA-Z_0-9-]+:.*##/ {printf "  \033[36m%-14s\033[0m %s\n", $$1, $$2}' $(MAKEFILE_LIST)
	@printf "\n"

# -------------------------------------------------------------------- build
.PHONY: all
all: $(BIN) $(BUILD)/runtime ## Build elc and the runtime symlink

$(BIN): $(OBJ) | $(BUILD)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD):
	@mkdir -p $(BUILD)

# elc resolves its grammars from a runtime/ directory beside the binary
# unless ELC_RUNTIME_DIR is set (HLR-059). The symlink makes build/elc work
# without an install step.
$(BUILD)/runtime: | $(BUILD)
	@ln -sfn ../runtime $(BUILD)/runtime

.PHONY: debug
debug: ## Build with -O0 -g3 -DDEBUG
	@$(MAKE) --no-print-directory CFLAGS="-O0 -g3 -DDEBUG" all

-include $(DEP)

# --------------------------------------------------------------------- test
.PHONY: test
test: unit integration fixtures instrumented ## Run every test level

.PHONY: unit
unit: $(UNIT_BIN) ## Build and run the Criterion unit binaries
	@fail=0; for b in $(UNIT_BIN); do \
		printf '\n== %s ==\n' "$$b"; $$b --tap || fail=1; \
	done; exit $$fail

$(BUILD)/unit/%: test/unit/%.c $(LIB_OBJ) | $(BUILD)/unit
	@$(PKG_CONFIG) --exists criterion 2>/dev/null || { \
		echo "make: Criterion not found — install libcriterion-dev" >&2; exit 1; }
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(LIB_OBJ) \
		$(shell $(PKG_CONFIG) --cflags --libs criterion) \
		$(LDFLAGS) $(WRAP_FLAGS)

$(BUILD)/unit:
	@mkdir -p $(BUILD)/unit

.PHONY: integration
integration: all ## Run the CLI-level Bats suites
	@$(BATS) test/integration

.PHONY: fixtures
fixtures: all ## Run the fixture-conformance suites
	@$(BATS) test/fixtures

.PHONY: instrumented
instrumented: all ## Run the environment-observing suites
	@$(BATS) test/instrumented

# ----------------------------------------------------------------- analysis
.PHONY: asan
asan: ## Rebuild with ASan and UBSan and re-run the whole suite
	@$(MAKE) --no-print-directory clean
	@$(MAKE) --no-print-directory \
		CFLAGS="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer" \
		LDFLAGS="-fsanitize=address,undefined" test
	@$(MAKE) --no-print-directory clean

.PHONY: valgrind
valgrind: all ## Re-run integration and fixtures under valgrind
	@command -v valgrind >/dev/null || { \
		echo "make: valgrind not found" >&2; exit 1; }
	ELC_VALGRIND=1 $(BATS) test/integration test/fixtures

# -------------------------------------------------------------- spec / docs
.PHONY: spec
spec: ## Validate Project.xml and check the rendered documents are current
	@python3 tools/lint_project.py --no-warnings
	@tmp=$$(mktemp -d); rc=0; \
	for d in SDD HLRs LLRs STP Traceability; do \
		python3 tools/render_doc.py tools/templates/$$d.md.j2 $$d \
			--out $$tmp/$$d.md >/dev/null 2>&1; \
		diff -q $$tmp/$$d.md doc/$$d.md >/dev/null 2>&1 || { \
			echo "spec: doc/$$d.md is stale — re-render it" >&2; rc=1; }; \
	done; rm -rf $$tmp; exit $$rc

.PHONY: coverage
coverage: ## Fail if verification coverage has regressed
	@gaps=$$(python3 tools/lint_project.py 2>&1 | grep -c 'has no test verifying it'); \
	base=$$(cat test/gap-baseline.txt); \
	if [ "$$gaps" -gt "$$base" ]; then \
		echo "coverage regressed: $$gaps gaps against a baseline of $$base" >&2; \
		exit 1; \
	fi; \
	echo "coverage: $$gaps gaps, baseline $$base"

# ------------------------------------------------------------------ install
.PHONY: install
install: all ## Install elc and runtime under $(DESTDIR)$(PREFIX)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(BIN) $(DESTDIR)$(PREFIX)/bin/elc
	install -d $(DESTDIR)$(PREFIX)/share/elc
	cp -r runtime $(DESTDIR)$(PREFIX)/share/elc/
	install -d $(DESTDIR)$(PREFIX)/share/man/man1
	install -m 644 doc/elc.1 $(DESTDIR)$(PREFIX)/share/man/man1/elc.1
	install -d $(DESTDIR)$(PREFIX)/share/doc/elc
	install -m 644 doc/User_Manual.md $(DESTDIR)$(PREFIX)/share/doc/elc/

# -------------------------------------------------------------------- clean
.PHONY: clean
clean: ## Remove build artifacts
	@rm -rf $(BUILD)
