# Makefile — the only build entry point for elc.
#
# Non-recursive: one top-level Makefile, no `make -C` into subdirectories.
# Conventions are binding and documented in
# .github/skills/elocker-dev/SKILL.md ("Build").
#
# The block below is the help text. `make help` prints it, stripped of the
# `#>` marker, and it is the default goal — so the usage summary is one text,
# read the same way whether you open this file or run it. Reconstructing the
# summary by pattern-matching the target declarations, as this used to, meant
# the help existed only as a side effect of a regular expression, and a reader
# of the file saw nothing at all.
#
# Keep it in step with the targets: `make instrumented` fails if a .PHONY
# target is missing from here, or listed here and absent from the file.
#
#>Usage:
#>  make <target>
#>
#>Build:
#>  all             Build elc, the grammars, and the runtime symlink
#>  debug           Build with -O0 -g3 -DDEBUG
#>  grammars        Build the runtime language grammars
#>  clean           Remove build artifacts
#>  clean-grammars  Remove the built grammars, forcing a refetch
#>  install         Install elc and runtime under $(DESTDIR)$(PREFIX)
#>
#>Test — all three must be clean before a change is done:
#>  test            Run every test level
#>  unit            Build and run the Criterion unit binaries
#>  integration     Run the CLI-level Bats suites
#>  fixtures        Run the fixture-conformance suites
#>  instrumented    Run the environment-observing suites
#>  asan            Rebuild with ASan and UBSan and re-run the whole suite
#>  valgrind        Re-run integration and fixtures under valgrind
#>
#>Specification:
#>  spec            Validate Project.xml and check the rendered documents are current
#>  coverage        Fail if verification coverage has regressed
#>
#>Dependencies — each library is built from its pinned upstream release:
#>  prereqs         Install the toolchain and build every linked library from source (needs sudo)
#>  prereqs-src     Build every linked library from source (needs sudo)
#>  prereqs-tree-sitter  Build libtree-sitter from source (needs sudo)
#>  prereqs-libgit2      Build libgit2 from source, no network transports (needs sudo)
#>  prereqs-igraph       Build igraph from source, GraphML off (needs sudo)
#>  prereqs-expat        Build Expat from source (needs sudo)
#>  prereqs-clean   Remove the unpacked dependency sources
#>  check-prereqs   Report which dependencies are present and flag version gaps
#>
#>  help            Display this help message

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

# Expat is linked for the XML *read* path only. The write path is hand-rolled
# text emission and needs nothing (doc/SDD.md §16).
EXPAT_CFLAGS ?= $(shell $(PKG_CONFIG) --cflags expat 2>/dev/null)
EXPAT_LIBS   ?= $(shell $(PKG_CONFIG) --libs expat 2>/dev/null || echo -lexpat)

# libgit2 gives tracked-file enumeration. Not .gitignore evaluation: files are
# excluded because git does not track them, which is the answer `git ls-files`
# gives and needs no ignore rules interpreted (doc/SDD.md §5).
GIT2_CFLAGS  ?= $(shell $(PKG_CONFIG) --cflags libgit2 2>/dev/null)
GIT2_LIBS    ?= $(shell $(PKG_CONFIG) --libs libgit2 2>/dev/null || echo -lgit2)

# igraph holds the graph's topology so that the traversals of Phases 9-11 are
# a mature library's algorithms rather than ours (HLR-113). Built with
# GraphML support off: elc writes GraphML itself, and enabling igraph's would
# link a second XML library the project has no other use for (SDD §18).
IGRAPH_CFLAGS ?= $(shell $(PKG_CONFIG) --cflags igraph 2>/dev/null)
IGRAPH_LIBS   ?= $(shell $(PKG_CONFIG) --libs igraph 2>/dev/null || echo -ligraph)

# libelf parses the linked image --elf names, for the reason igraph holds the
# graph: ELF is a well-specified format with a mature implementation, and
# hand-rolling one would put endianness, class and section-header handling into
# this project's defect surface (HLR-113, doc/SDD.md §18).
#
# -lstdc++ goes with it, and is not a new dependency: libstdc++ is already
# loaded because igraph is partly C++ inside, and elc has linked it since
# Phase 8. What changed in Phase 16 is that elc now *references* a symbol in
# it — __cxa_demangle, which decodes the Itanium ABI and with it C++ and Rust's
# legacy scheme. A symbol resolved through an indirect DT_NEEDED is not
# resolved at all by a modern ld, so the library has to be named here even
# though it was always loaded (doc/notes.md §1.1).
ELF_CFLAGS   ?= $(shell $(PKG_CONFIG) --cflags libelf 2>/dev/null)
ELF_LIBS     ?= $(shell $(PKG_CONFIG) --libs libelf 2>/dev/null || echo -lelf)

# libdw reads the debug line information the image carries, where it carries
# any, so that pruning can reach line granularity (HLR-153). It is the same
# upstream project as libelf and is taken from the distribution for the same
# reason, which is why it extends that exception rather than opening a new one
# (doc/notes.md §1.1).
#
# The reason the library is taken at all is HLR-113's: the DWARF line-number
# programme is a state machine whose file and directory tables changed shape
# at version 5 and reach into .debug_line_str, and every compiler this project
# is aimed at now emits version 5 by default. Hand-rolling it would put a
# format parser into elc's defect surface, which is exactly the argument that
# took libelf for the container.
#
# **libdw, never libdwfl.** The low-level dwarf_* interface reads the ELF
# descriptor elfsyms.c already holds. The Dwfl layer above it resolves
# separate debug information by .gnu_debuglink and build-id, which means
# opening a file under /usr/lib/debug that the user never named — forbidden by
# HLR-141 and observed by test/instrumented/environment.bats.
DW_CFLAGS    ?= $(shell $(PKG_CONFIG) --cflags libdw 2>/dev/null)
DW_LIBS      ?= $(shell $(PKG_CONFIG) --libs libdw 2>/dev/null || echo -ldw)

# Jansson reads and writes the purification manifest (HLR-175 - HLR-177). It
# is the one place elc uses a library to *write* a format rather than
# hand-rolling emission, and the exception is the round trip: the manifest is
# the only artefact elc must also parse, and a hand-rolled writer paired with a
# library reader would be two implementations of one format with elc on both
# ends of the disagreement (doc/SDD.md §20.2.3).
JANSSON_CFLAGS ?= $(shell $(PKG_CONFIG) --cflags jansson 2>/dev/null)
JANSSON_LIBS   ?= $(shell $(PKG_CONFIG) --libs jansson 2>/dev/null || echo -ljansson)
# _XOPEN_SOURCE/_DEFAULT_SOURCE are required for fts(3) on glibc and must be
# set before any include; they live here rather than in the .c files.
CPPFLAGS    += -Iinclude -D_XOPEN_SOURCE=700 -D_DEFAULT_SOURCE $(TS_CFLAGS) $(EXPAT_CFLAGS) $(GIT2_CFLAGS) $(IGRAPH_CFLAGS) $(ELF_CFLAGS) $(DW_CFLAGS) $(JANSSON_CFLAGS)
CFLAGS      ?= -O2 -g
LDFLAGS     +=
# `-lm` for the logarithms the Maintainability Index is formed from
# (HLR-191). Named explicitly rather than left to the linker: glibc splits the
# maths functions into their own object, and a `log` reached through another
# library's dependency links on one distribution and fails on the next.
LDLIBS      += $(TS_LIBS) $(EXPAT_LIBS) $(GIT2_LIBS) $(IGRAPH_LIBS) $(ELF_LIBS) $(DW_LIBS) $(JANSSON_LIBS) -lstdc++ -ldl -lm

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
# One text, two ways of reading it: the block at the top of this file is what
# `make help` prints. The marker is `#>` because it cannot occur in ordinary
# prose, so no comment can join the help by accident.
.PHONY: help
help:
	@printf '\n'
	@sed -n 's/^#>//p' $(firstword $(MAKEFILE_LIST))
	@printf '\n'

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
#
# libelf is the second, and unlike Criterion it *is* linked, so the exception
# is worth stating rather than assuming. elfutils does not ship libelf as a
# library that builds on its own: configuring the project drags in bison, flex,
# gettext, and three compression libraries, every one of them a distribution
# package. Building it from source would therefore import more distribution
# packages than taking libelf from the distribution does, which inverts the
# reason the rule exists. It is listed in PKGS_BUILD below and tracked in
# doc/notes.md §1.1.
#
# libdw is the same exception and not a third one. It is the same elfutils
# tree, built by the same configure run, shipped by the same distribution
# package family, and taken for the same reason — Phase 20 added it to read
# debug line information (HLR-153), and it arrives with the libelf that was
# already here.
#
# Jansson is the third, and the only one taken from the distribution because
# building it from source is actively *unsafe*. GNU ld links libjansson — for
# its JSON map-file output — so installing another copy under $(SRC_PREFIX),
# which ldconfig ranks ahead of the distribution's, replaces the system
# linker's jansson for the whole machine. It is not even a version question:
# Debian and Ubuntu patch the symbol version node to `libjansson.so.4` while
# upstream's own build names it `JANSSON_4`, so `ld` looks for
# `json_delete@libjansson.so.4`, does not find it, and exits 127 before
# linking anything. A tool that cannot link is a worse outcome than an
# unpinned dependency, and the dependency in question reads one optional file.
# The distribution ships 2.14, which is the minimum `check-prereqs` asks for.

TREE_SITTER_VER ?= 0.26.2
LIBGIT2_VER     ?= 1.9.0
IGRAPH_VER      ?= 1.0.1
EXPAT_VER       ?= 2.8.3

# Grammars are pinned like the libraries, and for the same reason. Each is a
# separate upstream project on its own release cadence, and the ABI it
# generates must stay inside libtree-sitter's supported range — a grammar
# built against a newer generator than the linked library understands fails
# at load with a version error rather than at build.
GRAMMAR_C_VER      ?= 0.24.2
GRAMMAR_CPP_VER    ?= 0.23.4
GRAMMAR_RUST_VER   ?= 0.24.2
GRAMMAR_PYTHON_VER ?= 0.25.0


SRC_PREFIX      ?= /usr/local
SRC_WORK        ?= $(BUILD)/prereq-src

# Toolchain, test framework, and the headers the source builds need.
PKGS_BUILD  ?= build-essential pkg-config python3 cmake curl zlib1g-dev \
               libelf-dev libdw-dev libjansson-dev
PKGS_TEST   ?= libcriterion-dev

# Test and inspection tools. These are executables the suites invoke, never
# libraries elc links — `libxml2-utils` here is the `xmllint` binary used to
# assert that emitted XML and GraphML are well-formed (doc/STP.md §6), which
# is not the same thing as elc depending on libxml2. It does not.
PKGS_TOOLS  ?= valgrind strace graphviz libxml2-utils binutils

PKGS        := $(PKGS_BUILD) $(PKGS_TEST) $(PKGS_TOOLS)

# Refuse to run any prereq target as root, and say why.
#
# The recipes below sudo exactly where they need to — `apt-get`, `make install`,
# `cmake --install` — and nowhere else. Run the whole target under sudo and the
# *unpacking* runs as root too, and that is the part that does lasting damage:
# an unprivileged `tar x` ignores the uid and gid recorded in an archive and
# gives everything to the invoking user, while a root `tar x` honours them.
# igraph's release tarball carries 501:staff — the account of whoever packaged
# it — so a sudo-run leaves directories here owned by root and by a stranger's
# uid, and the developer who ran the build cannot remove their own build tree.
# `prereqs-clean`, the target named for removing it, then fails.
#
# Caught here rather than diagnosed afterwards, because afterwards costs a
# password and a refetch.
.PHONY: _not-root
_not-root:
	@[ "$$(id -u)" -ne 0 ] || { \
		echo "prereqs: do not run this under sudo." >&2; \
		echo "  The recipes escalate where they need to and nowhere" >&2; \
		echo "  else. Running the whole target as root leaves" >&2; \
		echo "  $(SRC_WORK) owned by root and by whatever uid each" >&2; \
		echo "  upstream tarball happens to carry, which you will then" >&2; \
		echo "  need a password to delete." >&2; \
		echo "  Run it as yourself; you will be prompted where sudo" >&2; \
		echo "  is actually required." >&2; \
		exit 1; }

.PHONY: prereqs
prereqs: _not-root
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
prereqs-src: _not-root prereqs-tree-sitter prereqs-libgit2 prereqs-igraph \
             prereqs-expat
	@sudo ldconfig
	@echo "prereqs-src: every linked library built and installed under $(SRC_PREFIX)"

# Fetch and unpack an upstream release into the work directory.
#   $(1) archive URL   $(2) directory the archive unpacks to
define fetch
	@mkdir -p $(SRC_WORK)
	@rm -rf $(SRC_WORK)/$(2)
	@echo "  fetching $(1)"
	@curl -fsSL "$(1)" | tar xz -C $(SRC_WORK)
endef

.PHONY: prereqs-tree-sitter
prereqs-tree-sitter: _not-root
	@echo "tree-sitter $(TREE_SITTER_VER)"
	$(call fetch,https://github.com/tree-sitter/tree-sitter/archive/refs/tags/v$(TREE_SITTER_VER).tar.gz,tree-sitter-$(TREE_SITTER_VER))
	@$(MAKE) -C $(SRC_WORK)/tree-sitter-$(TREE_SITTER_VER) PREFIX=$(SRC_PREFIX)
	@sudo $(MAKE) -C $(SRC_WORK)/tree-sitter-$(TREE_SITTER_VER) install PREFIX=$(SRC_PREFIX)

# libgit2's network transports are compiled out. elc reads local
# repositories and never speaks to a remote (HLR-040), so HTTPS and SSH
# support is attack surface with no corresponding capability — and dropping
# it removes the OpenSSL and libssh2 dependencies along with it.
.PHONY: prereqs-libgit2
prereqs-libgit2: _not-root
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

# Two features switched off at configure time, both for the same reason: they
# link something into elc that elc's requirements say is not there.
#
#   GraphML  elc writes GraphML itself, so igraph's reader and writer are
#            unused, and enabling them links a second XML library the project
#            has no other need for.
#   OpenMP   igraph's default build links libgomp, which allocates a thread
#            pool during the dynamic linker's init — before main runs. elc is
#            single-threaded by requirement (HLR-041), and a thread runtime
#            in the link line is a standing invitation for that to stop being
#            true the first time a parallel algorithm is called. It also puts
#            104 bytes of load-time state in every run, which the project's
#            valgrind flags count as an error.
#
# And one pinned rather than switched off. IGRAPH_USE_INTERNAL_GMP defaults to
# AUTO: system GMP when its headers are present, bundled Mini-GMP otherwise.
# That makes elc's link line depend on what happened to be installed when
# igraph was built — it differed between a developer machine and CI, and the
# instrumented allowlist is a fixed list, so "it depends" is the one answer it
# cannot accept. Pinned to the bundled copy, which also keeps it inside the
# pinned-source story of doc/notes.md §1.1 rather than taking a distribution
# library. igraph uses GMP only in bliss, for the automorphism-group counts of
# graph isomorphism, which no elc analysis performs.
.PHONY: prereqs-igraph
prereqs-igraph: _not-root
	@echo "igraph $(IGRAPH_VER) (GraphML off, OpenMP off, internal GMP)"
	$(call fetch,https://github.com/igraph/igraph/releases/download/$(IGRAPH_VER)/igraph-$(IGRAPH_VER).tar.gz,igraph-$(IGRAPH_VER))
	@cmake -S $(SRC_WORK)/igraph-$(IGRAPH_VER) -B $(SRC_WORK)/igraph-build \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_INSTALL_PREFIX=$(SRC_PREFIX) \
		-DBUILD_SHARED_LIBS=ON \
		-DIGRAPH_GRAPHML_SUPPORT=OFF \
		-DIGRAPH_OPENMP_SUPPORT=OFF \
		-DIGRAPH_USE_INTERNAL_GMP=ON
	@cmake --build $(SRC_WORK)/igraph-build --parallel
	@sudo cmake --install $(SRC_WORK)/igraph-build

.PHONY: prereqs-expat
prereqs-expat: _not-root
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
prereqs-clean:
	@rm -rf $(SRC_WORK) 2>/dev/null || { \
		echo "prereqs-clean: $(SRC_WORK) holds files this user cannot" >&2; \
		echo "  remove, which means a prereq target was once run under" >&2; \
		echo "  sudo. Nothing installed is affected — this tree is only" >&2; \
		echo "  the unpacked sources. To take it back:" >&2; \
		echo "" >&2; \
		echo "    sudo chown -R $$(id -u):$$(id -g) $(SRC_WORK)" >&2; \
		echo "" >&2; \
		echo "  then run make prereqs-clean again." >&2; \
		exit 1; }

.PHONY: check-prereqs
check-prereqs:
	@echo "== tools =="
	@for t in cc ld make python3 pkg-config valgrind strace dot xmllint nm git; do \
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
	@for l in criterion tree-sitter expat libgit2 igraph libelf libdw jansson; do \
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
	@$(MAKE) --no-print-directory _check-min LIB=libelf      MIN=0.18 PHASE=16
	@$(MAKE) --no-print-directory _check-min LIB=libdw       MIN=0.18 PHASE=20
	@$(MAKE) --no-print-directory _check-min LIB=jansson     MIN=2.14 PHASE=23
	@echo "== grammars =="
	@$(MAKE) --no-print-directory _check-grammar LANG=c \
		REPO=tree-sitter/tree-sitter-c KIND=tag PIN=$(GRAMMAR_C_VER)
	@$(MAKE) --no-print-directory _check-grammar LANG=cpp \
		REPO=tree-sitter/tree-sitter-cpp KIND=tag PIN=$(GRAMMAR_CPP_VER)
	@$(MAKE) --no-print-directory _check-grammar LANG=rust \
		REPO=tree-sitter/tree-sitter-rust KIND=tag PIN=$(GRAMMAR_RUST_VER)
	@$(MAKE) --no-print-directory _check-grammar LANG=python \
		REPO=tree-sitter/tree-sitter-python KIND=tag PIN=$(GRAMMAR_PYTHON_VER)
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
	@if [ -f $(SRC_PREFIX)/lib/libigraph.so ] && \
	    ldd $(SRC_PREFIX)/lib/libigraph.so 2>/dev/null | grep -q libgomp; then \
		echo "  WARNING igraph was built with OpenMP support, so it links"; \
		echo "          libgomp, whose runtime allocates a thread pool during"; \
		echo "          the dynamic linker's init - before main runs. elc is"; \
		echo "          single-threaded by requirement (HLR-041), and the"; \
		echo "          instrumented dependency allowlist rejects libgomp."; \
		echo "          Rebuild with 'make prereqs-igraph', which passes"; \
		echo "          -DIGRAPH_OPENMP_SUPPORT=OFF (doc/notes.md §1.1)."; \
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

# Report one grammar's pin against what upstream carries now.
#
# A pin makes a build reproducible and an update deliberate; it does not say
# an update is needed. For a repository with releases the usual channels do —
# advisories and release notes are keyed to version numbers. A grammar
# pinned by commit rather than by tag has none to key to, so being behind
# would otherwise be invisible, and this is what makes it visible instead
# (LLR-BLD-17).
#
# Behind is a warning, never an error, exactly as _check-min treats a version
# shortfall: check-prereqs is a diagnostic a person runs, CI never invokes it,
# and it must neither fail nor hang when there is no network. The query is
# capped at five seconds and reports "unknown" rather than blocking.
.PHONY: _check-grammar
_check-grammar:
	@printf '  %-10s %-12s ' "$(LANG)" "$$(echo '$(PIN)' | cut -c1-12)"; \
	if ! command -v curl >/dev/null 2>&1; then \
		echo "(curl absent — upstream unknown)"; \
	elif [ "$(KIND)" = tag ]; then \
		latest=$$(curl -fsSL --max-time 5 \
			"https://api.github.com/repos/$(REPO)/tags" 2>/dev/null | \
			sed -n 's/.*"name": "v\{0,1\}\([^"]*\)".*/\1/p' | head -1); \
		if [ -z "$$latest" ]; then echo "(upstream unknown)"; \
		elif [ "$$latest" = "$(PIN)" ]; then echo "current"; \
		else echo "BEHIND — upstream has $$latest"; fi; \
	else \
		head=$$(curl -fsSL --max-time 5 \
			"https://api.github.com/repos/$(REPO)/commits/HEAD" 2>/dev/null | \
			sed -n 's/.*"sha": "\([^"]*\)".*/\1/p' | head -1); \
		if [ -z "$$head" ]; then echo "(upstream unknown)"; \
		elif [ "$$head" = "$(PIN)" ]; then echo "current (no releases upstream)"; \
		else echo "BEHIND — head is $$(echo $$head | cut -c1-12)"; fi; \
	fi

# -------------------------------------------------------------------- build
# Grammars are runtime data, not objects: they are dlopen'd by name from
# runtime/parsers/ and never linked. They are gitignored (*.so), so a fresh
# clone builds them once and every later build skips them — make sees the
# file and stops. `clean` deliberately leaves them: `make asan` cleans twice,
# and refetching an upstream tarball on each of those is a network round trip
# for nothing. `make clean-grammars` removes them when that is what is meant.
GRAMMARS    := runtime/parsers/c.so runtime/parsers/cpp.so \
               runtime/parsers/rust.so runtime/parsers/python.so

.PHONY: grammars
grammars: $(GRAMMARS)

# Build one grammar into runtime/parsers/<lang>.so.
#   $(1) language   $(2) owner/repo   $(3) archive ref   $(4) unpacked directory
#
# The owner is a parameter rather than baked in. Every grammar shipped today
# lives under the tree-sitter organisation, and a hardcoded owner would read
# as though one from elsewhere could not be added as data. It can, and the
# project has shipped one that did.
#
# The archive ref is a parameter for the same kind of reason. A repository
# that cuts releases is fetched by tag; one that does not is fetched by
# commit, and the two spell the URL differently.
#
# The generated parser.c ships in the upstream release, so no tree-sitter CLI
# and no code generation is involved — HLR-040 forbids requiring the latter at
# build time. Third-party generated code is compiled at the project's warning
# settings minus the pedantry it was never written to satisfy; -fvisibility is
# not narrowed, since tree_sitter_<lang> must remain resolvable by dlsym.
#
# The scanner is found with a shell glob, **not** `$$(wildcard)`. Make expands
# a recipe before running any of it, so a `$$(wildcard)` here would be
# evaluated before `fetch` had unpacked anything and would quietly find
# nothing — linking a grammar without its external scanner. C has no scanner,
# which is why that went unnoticed until four grammars that do have one
# arrived.
define build_grammar
	@echo "$(2) $(3)"
	$(call fetch,https://github.com/$(2)/archive/$(3).tar.gz,$(4))
	@mkdir -p runtime/parsers
	$(CC) -O2 -fPIC -shared -I$(SRC_WORK)/$(4)/src \
		-o runtime/parsers/$(1).so $(SRC_WORK)/$(4)/src/parser.c \
		$$(ls $(SRC_WORK)/$(4)/src/scanner.c 2>/dev/null)
endef

runtime/parsers/c.so:
	$(call build_grammar,c,tree-sitter/tree-sitter-c,refs/tags/v$(GRAMMAR_C_VER),tree-sitter-c-$(GRAMMAR_C_VER))

runtime/parsers/cpp.so:
	$(call build_grammar,cpp,tree-sitter/tree-sitter-cpp,refs/tags/v$(GRAMMAR_CPP_VER),tree-sitter-cpp-$(GRAMMAR_CPP_VER))

runtime/parsers/rust.so:
	$(call build_grammar,rust,tree-sitter/tree-sitter-rust,refs/tags/v$(GRAMMAR_RUST_VER),tree-sitter-rust-$(GRAMMAR_RUST_VER))

runtime/parsers/python.so:
	$(call build_grammar,python,tree-sitter/tree-sitter-python,refs/tags/v$(GRAMMAR_PYTHON_VER),tree-sitter-python-$(GRAMMAR_PYTHON_VER))


.PHONY: clean-grammars
clean-grammars:
	@rm -f $(GRAMMARS)

.PHONY: all
all: $(BIN) $(BUILD)/runtime $(GRAMMARS)
	@$(MAKE) --no-print-directory _warning-summary

$(BIN): $(OBJ) | $(BUILD)
	$(CC) $(CFLAGS) $(ELC_CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# Compiling one translation unit of the application image.
#
# The compiler's diagnostics go to the terminal as they always have, and a copy
# is kept beside the object so the build can total them when it finishes. A
# warning that scrolled past fifteen compile lines ago is a warning nobody
# reads, and this project treats one as a defect (doc/STP.md §6) — so a build
# that emitted warnings and exited 0 in silence was the one outcome the rule
# most needed to make visible.
#
# The status comes from the compiler, not from the copy. Piping into `tee` would
# report tee's success instead and turn a failed compile into a silent one,
# which is the trap this arrangement exists to avoid rather than to set.
#
# The copy is per object rather than one shared log, for two reasons: `make -j`
# would interleave a shared one, and a per-object file survives an incremental
# build — so the total describes the warnings *this image was built with*, not
# merely those from the files that happened to be recompiled just now.
$(BUILD)/%.o: src/%.c | $(BUILD)
	@echo "$(CC) $(CPPFLAGS) $(CFLAGS) $(ELC_CFLAGS) -c -o $@ $<"
	@$(CC) $(CPPFLAGS) $(CFLAGS) $(ELC_CFLAGS) -c -o $@ $< 2> $@.diag; \
	status=$$?; \
	if [ -s $@.diag ]; then cat $@.diag >&2; fi; \
	cp -f $@.diag $(@:.o=.warn) 2>/dev/null || : ; \
	rm -f $@.diag; \
	exit $$status

# What the image was built with, said once, where it cannot be missed.
#
# Silent when there is nothing to report: a clean build should look clean, and a
# line saying "0 warnings" after every build is a line people stop reading.
.PHONY: _warning-summary
_warning-summary:
	@count=$$(cat $(OBJ:.o=.warn) 2>/dev/null | grep -c 'warning:' || :); \
	if [ "$${count:-0}" -gt 0 ]; then \
		echo ""; \
		echo "  $$count compiler warning(s) in the application image."; \
		echo "  This project treats a warning as a defect (doc/STP.md §6);"; \
		echo "  CI builds with -Werror, so these fail the build there."; \
		echo ""; \
		grep -h 'warning:' $(OBJ:.o=.warn) 2>/dev/null | sed 's/^/    /'; \
		echo ""; \
	fi

$(BUILD):
	@mkdir -p $(BUILD)

# elc resolves its grammars from a runtime/ directory beside the binary
# unless ELC_RUNTIME_DIR is set (HLR-059). The symlink makes build/elc work
# without an install step.
$(BUILD)/runtime: | $(BUILD)
	@ln -sfn ../runtime $(BUILD)/runtime

.PHONY: debug
debug:
	@$(MAKE) --no-print-directory CFLAGS="-O0 -g3 -DDEBUG" all

-include $(DEP)

# --------------------------------------------------------------------- test
.PHONY: test
test: unit integration fixtures instrumented

# ELC_RUNTIME_DIR is set here for the same reason the Bats suites set it: a
# unit binary lives in build/unit/, so the runtime adjacent to *it* does not
# exist, and the registry tests need a real grammar to load. Pointing at the
# in-tree runtime keeps the unit level independent of any installed copy.
.PHONY: unit
unit: $(UNIT_BIN) $(GRAMMARS)
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
integration: all
	@$(BATS) test/integration

# The fixture data lives in one directory per property under test/fixtures/;
# the suites themselves stay flat beside them, so bats needs no recursive
# discovery — which uses `find -L` and would walk the cyclic symlink the
# traversal fixture deliberately contains.
.PHONY: fixtures
fixtures: all
	@$(BATS) test/fixtures

.PHONY: instrumented
instrumented: all
	@$(BATS) test/instrumented

# ----------------------------------------------------------------- analysis
# The sanitizer settings, set here rather than only in CI, and that placement
# is the point. `abort_on_error=1` is what makes a LeakSanitizer report inside
# a *forked Criterion child* reach the parent's exit status; without it a leak
# in a unit test is reported and the run still succeeds. A local gate weaker
# than the CI one is worse than no local gate, because it is trusted: Phase 11
# shipped two leaking tests past a green `make asan` and was caught only by the
# pipeline (LLR-BLD-11).
#
# Overridable so a developer chasing one failure can loosen them, and exported
# to the whole recursive run.
ASAN_OPTIONS  ?= detect_leaks=1:abort_on_error=1:strict_string_checks=1
UBSAN_OPTIONS ?= print_stacktrace=1:halt_on_error=1

.PHONY: asan
asan:
	@$(MAKE) --no-print-directory clean
	@rc=0; ASAN_OPTIONS="$(ASAN_OPTIONS)" UBSAN_OPTIONS="$(UBSAN_OPTIONS)" \
	$(MAKE) --no-print-directory \
		CFLAGS="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer" \
		LDFLAGS="-fsanitize=address,undefined" test || rc=$$?; \
	$(MAKE) --no-print-directory clean; \
	exit $$rc
# The trailing clean runs even when the suite fails. Leaving instrumented
# objects behind poisons every later target: valgrind refuses to run against
# an ASan binary, so a single asan failure would cascade into a wall of
# unrelated valgrind failures.

.PHONY: valgrind
valgrind: all
	@command -v valgrind >/dev/null || { \
		echo "make: valgrind not found" >&2; exit 1; }
	ELC_VALGRIND=1 $(BATS) test/integration test/fixtures

# -------------------------------------------------------------- spec / docs
.PHONY: spec
spec:
	@python3 tools/lint_project.py --no-warnings
	@tmp=$$(mktemp -d); rc=0; \
	for d in SDD HLRs LLRs STP Traceability; do \
		python3 tools/render_doc.py tools/templates/$$d.md.j2 $$d \
			--out $$tmp/$$d.md >/dev/null 2>&1; \
		diff -q $$tmp/$$d.md doc/$$d.md >/dev/null 2>&1 || { \
			echo "spec: doc/$$d.md is stale — re-render it" >&2; rc=1; }; \
	done; rm -rf $$tmp; exit $$rc

.PHONY: coverage
coverage:
	@gaps=$$(python3 tools/lint_project.py 2>&1 | grep -c 'has no test verifying it'); \
	base=$$(cat test/gap-baseline.txt); \
	if [ "$$gaps" -gt "$$base" ]; then \
		echo "coverage regressed: $$gaps gaps against a baseline of $$base" >&2; \
		exit 1; \
	fi; \
	echo "coverage: $$gaps gaps, baseline $$base"

# ------------------------------------------------------------------ install
.PHONY: install
install: all
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(BIN) $(DESTDIR)$(PREFIX)/bin/elc
	install -d $(DESTDIR)$(PREFIX)/share/elc
	cp -r runtime $(DESTDIR)$(PREFIX)/share/elc/
	install -d $(DESTDIR)$(PREFIX)/share/man/man1
	install -m 644 doc/elc.1 $(DESTDIR)$(PREFIX)/share/man/man1/elc.1
	install -d $(DESTDIR)$(PREFIX)/share/doc/elc
	install -m 644 doc/User_Manual.md $(DESTDIR)$(PREFIX)/share/doc/elc/

# -------------------------------------------------------------------- clean
# Everything under $(BUILD) *except* the unpacked dependency sources, which
# `prereqs-clean` removes and this target must not.
#
# The division of labour is the whole reason: the help block promises that
# `clean` removes build artifacts and that `prereqs-clean` removes the
# dependency sources, and a `clean` that took both made one of those two
# targets a lie. Refetching a pinned upstream tarball also costs minutes that
# this target has no business spending — the sanitized pass cleans twice.
#
# It is load-bearing as well as tidy. A prereq target run under sudo leaves
# this tree unremovable by the developer who built it (see `_not-root`), and
# `clean` runs at the head of both `asan` and `valgrind` — so a tree in that
# state used to take both sanitizer gates down with it. Those gates now run
# whatever state the dependency sources are in, which is where they should
# have been all along.
.PHONY: clean
clean:
	@rm -rf $(filter-out $(SRC_WORK),$(wildcard $(BUILD)/*))
