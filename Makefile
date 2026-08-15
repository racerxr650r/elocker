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

# libtree-sitter is discovered with pkg-config, with an overridable fallback
# so a hard-coded /usr/lib path never enters the build. -ldl is separate: the
# registry dlopen's grammars, and on glibc before 2.34 that is not in libc.
TS_CFLAGS   ?= $(shell $(PKG_CONFIG) --cflags tree-sitter 2>/dev/null)
TS_LIBS     ?= $(shell $(PKG_CONFIG) --libs tree-sitter 2>/dev/null || echo -ltree-sitter)
# _XOPEN_SOURCE/_DEFAULT_SOURCE are required for fts(3) on glibc and must be
# set before any include; they live here rather than in the .c files.
CPPFLAGS    += -Iinclude -D_XOPEN_SOURCE=700 -D_DEFAULT_SOURCE $(TS_CFLAGS)
CFLAGS      ?= -O2 -g
LDFLAGS     +=
LDLIBS      += $(TS_LIBS) -ldl

# Flags the build requires whatever the caller chose, appended in the recipes
# rather than folded into CFLAGS.
#
# `CFLAGS += ...` here would look equivalent and is not: a variable set on the
# command line overrides every assignment in the makefile, `+=` included. So
# `make all CFLAGS="-O2 -g -Werror"` — which is exactly what the CI build job
# runs — would drop -std=c11, the warning set, and the header-dependency
# generation, leaving -Werror with almost nothing left to fail on.
ELC_CFLAGS  := -std=c11 $(WARNINGS) -MMD -MP

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
#
# The lists are per module, matching that inventory. A wrap applies to every
# object in the binary being linked, and every unit binary links every module,
# so a symbol wrapped globally would oblige every test file to define a
# `__wrap_` for it whether or not it mocks anything. WRAP_SYMS stays for a
# symbol that genuinely must be intercepted everywhere; it is empty today.
#
# `comma` must be defined before WRAP_FLAGS: `:=` expands immediately, so a
# later definition would leave the separator empty and silently produce
# `-Wl--wrap=...`, which the compiler rejects. WRAP_FLAGS itself must be `=`
# rather than `:=`, since `$*` only has a value inside the pattern recipe.
comma              := ,
WRAP_SYMS          ?=
WRAP_SYMS_cli      ?= malloc
WRAP_SYMS_discover ?= realpath
WRAP_SYMS_report   ?= realloc
WRAP_FLAGS          = $(addprefix -Wl$(comma)--wrap=,$(WRAP_SYMS) $(WRAP_SYMS_$*))

# --------------------------------------------------------------------- help
.PHONY: help
help: ## Display this help message
	@awk 'BEGIN {FS = ":.*##"; printf "\nUsage:\n  make \033[36m<target>\033[0m\n\nTargets:\n"} \
		/^[a-zA-Z_0-9-]+:.*##/ {printf "  \033[36m%-14s\033[0m %s\n", $$1, $$2}' $(MAKEFILE_LIST)
	@printf "\n"

# ------------------------------------------------------------------ prereqs
# Every library elc links is built from its upstream release, not taken from
# a distribution package. The reason is response time: when an advisory lands
# against one of these, the fix is to bump a version below and rebuild, which
# can happen the same day. Waiting on a distribution to rebuild and ship is
# not a control this project has.
#
# To answer an advisory: bump the version, run the matching prereqs-<lib>
# target, then `make clean && make test` to confirm nothing broke.
#
# Criterion is the deliberate exception. It is a test framework that is never
# linked into the shipped binary, so a vulnerability in it reaches no user of
# elc; it comes from the distribution, where it is one apt upgrade away.

TREE_SITTER_VER ?= 0.26.2
LIBGIT2_VER     ?= 1.9.0
IGRAPH_VER      ?= 1.0.1
EXPAT_VER       ?= 2.8.3

# Grammars are pinned like the libraries, and for the same reason. Each is a
# separate upstream project on its own release cadence, and the ABI it
# generates must stay inside libtree-sitter's supported range — a grammar
# built against a newer generator than the linked library understands fails
# at load with a version error rather than at build.
GRAMMAR_C_VER   ?= 0.24.2

SRC_PREFIX      ?= /usr/local
SRC_WORK        ?= $(BUILD)/prereq-src

# Toolchain, test framework, and the headers the source builds need.
PKGS_BUILD  ?= build-essential pkg-config python3 cmake curl zlib1g-dev
PKGS_TEST   ?= libcriterion-dev

# Test and inspection tools. These are executables the suites invoke, never
# libraries elc links — `libxml2-utils` here is the `xmllint` binary used to
# assert that emitted XML and GraphML are well-formed (doc/STP.md §6), which
# is not the same thing as elc depending on libxml2. It does not.
PKGS_TOOLS  ?= valgrind strace graphviz libxml2-utils binutils

PKGS        := $(PKGS_BUILD) $(PKGS_TEST) $(PKGS_TOOLS)

.PHONY: prereqs
prereqs: ## Install the toolchain and build every linked library from source (needs sudo)
	@command -v apt-get >/dev/null 2>&1 || { \
		echo "prereqs: only apt is automated. Install the equivalents of:" >&2; \
		echo "  $(PKGS)" >&2; \
		echo "then run: make prereqs-src" >&2; \
		exit 1; }
	sudo apt-get update -qq
	sudo apt-get install -y --no-install-recommends $(PKGS)
	@$(MAKE) --no-print-directory prereqs-src
	@$(MAKE) --no-print-directory check-prereqs

.PHONY: prereqs-src
prereqs-src: prereqs-tree-sitter prereqs-libgit2 prereqs-igraph prereqs-expat ## Build every linked library from source (needs sudo)
	@sudo ldconfig
	@echo "prereqs-src: all four libraries built and installed under $(SRC_PREFIX)"

# Fetch and unpack an upstream release into the work directory.
#   $(1) archive URL   $(2) directory the archive unpacks to
define fetch
	@mkdir -p $(SRC_WORK)
	@rm -rf $(SRC_WORK)/$(2)
	@echo "  fetching $(1)"
	@curl -fsSL "$(1)" | tar xz -C $(SRC_WORK)
endef

.PHONY: prereqs-tree-sitter
prereqs-tree-sitter: ## Build libtree-sitter from source (needs sudo)
	@echo "tree-sitter $(TREE_SITTER_VER)"
	$(call fetch,https://github.com/tree-sitter/tree-sitter/archive/refs/tags/v$(TREE_SITTER_VER).tar.gz,tree-sitter-$(TREE_SITTER_VER))
	@$(MAKE) -C $(SRC_WORK)/tree-sitter-$(TREE_SITTER_VER) PREFIX=$(SRC_PREFIX)
	@sudo $(MAKE) -C $(SRC_WORK)/tree-sitter-$(TREE_SITTER_VER) install PREFIX=$(SRC_PREFIX)

# libgit2's network transports are compiled out. elc reads local
# repositories and never speaks to a remote (HLR-040), so HTTPS and SSH
# support is attack surface with no corresponding capability — and dropping
# it removes the OpenSSL and libssh2 dependencies along with it.
.PHONY: prereqs-libgit2
prereqs-libgit2: ## Build libgit2 from source, no network transports (needs sudo)
	@echo "libgit2 $(LIBGIT2_VER) (transports disabled)"
	$(call fetch,https://github.com/libgit2/libgit2/archive/refs/tags/v$(LIBGIT2_VER).tar.gz,libgit2-$(LIBGIT2_VER))
	@cmake -S $(SRC_WORK)/libgit2-$(LIBGIT2_VER) -B $(SRC_WORK)/libgit2-build \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_INSTALL_PREFIX=$(SRC_PREFIX) \
		-DBUILD_SHARED_LIBS=ON \
		-DBUILD_TESTS=OFF \
		-DBUILD_CLI=OFF \
		-DUSE_HTTPS=OFF \
		-DUSE_SSH=OFF
	@cmake --build $(SRC_WORK)/libgit2-build --parallel
	@sudo cmake --install $(SRC_WORK)/libgit2-build

# GraphML support is switched off at configure time: elc writes GraphML
# itself, so igraph's reader and writer are unused, and enabling them links
# a second XML library the project has no other need for.
.PHONY: prereqs-igraph
prereqs-igraph: ## Build igraph from source, GraphML off (needs sudo)
	@echo "igraph $(IGRAPH_VER) (GraphML off)"
	$(call fetch,https://github.com/igraph/igraph/releases/download/$(IGRAPH_VER)/igraph-$(IGRAPH_VER).tar.gz,igraph-$(IGRAPH_VER))
	@cmake -S $(SRC_WORK)/igraph-$(IGRAPH_VER) -B $(SRC_WORK)/igraph-build \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_INSTALL_PREFIX=$(SRC_PREFIX) \
		-DBUILD_SHARED_LIBS=ON \
		-DIGRAPH_GRAPHML_SUPPORT=OFF
	@cmake --build $(SRC_WORK)/igraph-build --parallel
	@sudo cmake --install $(SRC_WORK)/igraph-build

.PHONY: prereqs-expat
prereqs-expat: ## Build Expat from source (needs sudo)
	@echo "expat $(EXPAT_VER)"
	$(call fetch,https://github.com/libexpat/libexpat/releases/download/R_$(subst .,_,$(EXPAT_VER))/expat-$(EXPAT_VER).tar.gz,expat-$(EXPAT_VER))
	@cmake -S $(SRC_WORK)/expat-$(EXPAT_VER) -B $(SRC_WORK)/expat-build \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_INSTALL_PREFIX=$(SRC_PREFIX) \
		-DBUILD_SHARED_LIBS=ON \
		-DEXPAT_BUILD_TESTS=OFF \
		-DEXPAT_BUILD_EXAMPLES=OFF \
		-DEXPAT_BUILD_TOOLS=OFF \
		-DEXPAT_BUILD_DOCS=OFF
	@cmake --build $(SRC_WORK)/expat-build --parallel
	@sudo cmake --install $(SRC_WORK)/expat-build

.PHONY: prereqs-clean
prereqs-clean: ## Remove the unpacked dependency sources
	@rm -rf $(SRC_WORK)

.PHONY: check-prereqs
check-prereqs: ## Report which dependencies are present and flag version gaps
	@echo "== tools =="
	@for t in cc ld make python3 pkg-config valgrind strace dot xmllint nm; do \
		if command -v $$t >/dev/null 2>&1; then \
			printf '  %-12s ok\n' "$$t"; \
		else \
			printf '  %-12s MISSING\n' "$$t"; \
		fi; \
	done
	@printf '  %-12s %s (vendored)\n' bats "$$($(BATS) --version 2>/dev/null || echo MISSING)"
	@echo "== libraries =="
	@command -v $(PKG_CONFIG) >/dev/null 2>&1 || { \
		echo "  pkg-config missing; cannot report library versions" >&2; exit 0; }
	@for l in criterion tree-sitter expat libgit2 igraph; do \
		v=$$($(PKG_CONFIG) --modversion $$l 2>/dev/null); \
		if [ -n "$$v" ]; then printf '  %-12s %s\n' "$$l" "$$v"; \
		else printf '  %-12s MISSING\n' "$$l"; fi; \
	done
	@echo "== conformance to doc/SDP.md §0 =="
	@$(MAKE) --no-print-directory _check-min LIB=tree-sitter MIN=0.25 PHASE=2
	@$(MAKE) --no-print-directory _check-min LIB=expat       MIN=2.6  PHASE=5
	@$(MAKE) --no-print-directory _check-min LIB=libgit2     MIN=1.7  PHASE=7
	@$(MAKE) --no-print-directory _check-min LIB=igraph      MIN=1.0  PHASE=8
	@$(MAKE) --no-print-directory _check-min LIB=criterion   MIN=2.4  PHASE=0
	@if $(PKG_CONFIG) --exists igraph 2>/dev/null && \
	    $(PKG_CONFIG) --libs igraph 2>/dev/null | grep -q xml2; then \
		echo "  WARNING igraph was built with GraphML support, so it links"; \
		echo "          libxml2 and would drag it into elc transitively."; \
		echo "          elc does not depend on libxml2: XML reading is Expat's"; \
		echo "          job and elc writes GraphML itself, so the feature is"; \
		echo "          unneeded and the library is unmaintained (since"; \
		echo "          September 2025). Before Phase 8, rebuild igraph with"; \
		echo "          -DIGRAPH_GRAPHML_SUPPORT=OFF (doc/notes.md §1.1)."; \
	fi

# Compare an installed library version against the SDP minimum. A shortfall
# is a warning, not an error: the library is not linked until its phase, so
# a stale distro package blocks nothing today.
.PHONY: _check-min
_check-min:
	@have=$$($(PKG_CONFIG) --modversion $(LIB) 2>/dev/null); \
	if [ -z "$$have" ]; then \
		printf '  %-12s absent — needed from Phase %s\n' "$(LIB)" "$(PHASE)"; \
	elif [ "$$(printf '%s\n%s\n' "$(MIN)" "$$have" | sort -V | head -1)" = "$(MIN)" ]; then \
		printf '  %-12s %s >= %s ok\n' "$(LIB)" "$$have" "$(MIN)"; \
	else \
		printf '  %-12s %s < %s BELOW MINIMUM — run: make prereqs-src\n' \
			"$(LIB)" "$$have" "$(MIN)"; \
	fi

# -------------------------------------------------------------------- build
# Grammars are runtime data, not objects: they are dlopen'd by name from
# runtime/parsers/ and never linked. They are gitignored (*.so), so a fresh
# clone builds them once and every later build skips them — make sees the
# file and stops. `clean` deliberately leaves them: `make asan` cleans twice,
# and refetching an upstream tarball on each of those is a network round trip
# for nothing. `make clean-grammars` removes them when that is what is meant.
GRAMMARS    := runtime/parsers/c.so

.PHONY: grammars
grammars: $(GRAMMARS) ## Build the runtime language grammars

# Build one grammar into runtime/parsers/<lang>.so.
#   $(1) language name   $(2) upstream repository   $(3) version
#
# The generated parser.c ships in the upstream release, so no tree-sitter CLI
# and no code generation is involved — HLR-040 forbids requiring the latter at
# build time. Third-party generated code is compiled at the project's warning
# settings minus the pedantry it was never written to satisfy; -fvisibility is
# not narrowed, since tree_sitter_<lang> must remain resolvable by dlsym.
define build_grammar
	@echo "$(2) $(3)"
	$(call fetch,https://github.com/tree-sitter/$(2)/archive/refs/tags/v$(3).tar.gz,$(2)-$(3))
	@mkdir -p runtime/parsers
	$(CC) -O2 -fPIC -shared -I$(SRC_WORK)/$(2)-$(3)/src \
		-o runtime/parsers/$(1).so $(SRC_WORK)/$(2)-$(3)/src/parser.c \
		$(wildcard $(SRC_WORK)/$(2)-$(3)/src/scanner.c)
endef

runtime/parsers/c.so:
	$(call build_grammar,c,tree-sitter-c,$(GRAMMAR_C_VER))

.PHONY: clean-grammars
clean-grammars: ## Remove the built grammars, forcing a refetch
	@rm -f $(GRAMMARS)

.PHONY: all
all: $(BIN) $(BUILD)/runtime $(GRAMMARS) ## Build elc, the grammars, and the runtime symlink

$(BIN): $(OBJ) | $(BUILD)
	$(CC) $(CFLAGS) $(ELC_CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(ELC_CFLAGS) -c -o $@ $<

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

# ELC_RUNTIME_DIR is set here for the same reason the Bats suites set it: a
# unit binary lives in build/unit/, so the runtime adjacent to *it* does not
# exist, and the registry tests need a real grammar to load. Pointing at the
# in-tree runtime keeps the unit level independent of any installed copy.
.PHONY: unit
unit: $(UNIT_BIN) $(GRAMMARS) ## Build and run the Criterion unit binaries
	@fail=0; for b in $(UNIT_BIN); do \
		printf '\n== %s ==\n' "$$b"; \
		ELC_RUNTIME_DIR=$(CURDIR)/runtime $$b --tap || fail=1; \
	done; exit $$fail

$(BUILD)/unit/%: test/unit/%.c $(LIB_OBJ) | $(BUILD)/unit
	@$(PKG_CONFIG) --exists criterion 2>/dev/null || { \
		echo "make: Criterion not found — install libcriterion-dev" >&2; exit 1; }
	$(CC) $(CPPFLAGS) $(CFLAGS) $(ELC_CFLAGS) -o $@ $< $(LIB_OBJ) \
		$(shell $(PKG_CONFIG) --cflags --libs criterion) \
		$(LDFLAGS) $(WRAP_FLAGS) $(LDLIBS)

$(BUILD)/unit:
	@mkdir -p $(BUILD)/unit

.PHONY: integration
integration: all ## Run the CLI-level Bats suites
	@$(BATS) test/integration

# The fixture data lives in one directory per property under test/fixtures/;
# the suites themselves stay flat beside them, so bats needs no recursive
# discovery — which uses `find -L` and would walk the cyclic symlink the
# traversal fixture deliberately contains.
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
	@rc=0; $(MAKE) --no-print-directory \
		CFLAGS="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer" \
		LDFLAGS="-fsanitize=address,undefined" test || rc=$$?; \
	$(MAKE) --no-print-directory clean; \
	exit $$rc
# The trailing clean runs even when the suite fails. Leaving instrumented
# objects behind poisons every later target: valgrind refuses to run against
# an ASan binary, so a single asan failure would cascade into a wall of
# unrelated valgrind failures.

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
