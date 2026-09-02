/* elc.h — types shared across the elc pipeline.
 *
 * The header grows one phase at a time: each phase adds the fields the SDD's
 * data dictionary describes for the stage it builds, and no more, so that a
 * field in this header always has code behind it. See doc/SDD.md §18.
 */
#ifndef ELC_H
#define ELC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Process exit status (HLR-120).
 *
 * The three classes are distinct so a caller can tell a degraded run from a
 * run that never happened. No finding severity ever contributes (HLR-100).
 */
enum {
	ELC_EXIT_OK      = 0, /* every discovered file processed, or skipped   */
	ELC_EXIT_FAILURE = 1, /* run completed, but a file failed to be read
	                       * or parsed (HLR-035, HLR-037)                  */
	ELC_EXIT_FATAL   = 2  /* run did not complete: usage error, invalid
	                       * target, fatal runtime location, rejected
	                       * saved record (HLR-062, HLR-063, HLR-036)      */
};

/* Name of the environment variable that overrides the runtime location
 * adjacent to the executable (HLR-059). */
#define ELC_RUNTIME_DIR_ENV "ELC_RUNTIME_DIR"

/* The complexity at or above which a function is listed for its file
 * (HLR-021, HLR-022). Reporting only: no threshold ever reaches the exit
 * status (HLR-023). */
#define ELC_DEFAULT_COMPLEXITY_THRESHOLD 15u

/* The coupling at which a component is called an architectural bottleneck: it
 * is flagged when its afferent *and* efferent couplings are each at least this
 * (HLR-081).
 *
 * Unlike every threshold in Appendix A, this one is `elc`'s own heuristic
 * rather than a published standard, and is marked as such wherever it is
 * reported (HLR-099). Reporting only: no threshold ever reaches the exit
 * status (HLR-023, HLR-100). */
#define ELC_DEFAULT_BOTTLENECK_THRESHOLD 5u

/* The purification thresholds of HLR-168 through HLR-170, and the defaults
 * they take.
 *
 * **Every one of these is `elc`'s own heuristic**, not a published standard,
 * and is marked as such wherever a classification made against it is reported
 * (HLR-171, HLR-099). They rest on nothing but this project's judgement, which
 * is why each is user-configurable: a heuristic that cannot be adjusted is one
 * whose disagreements have nowhere to go.
 *
 * The four centrality thresholds are **rank positions**, expressed as a
 * percentage of the other functions in the graph, never raw scores. A
 * betweenness value means nothing on its own — it scales with the size of the
 * graph, so a fixed cut-off would classify every function in a large project
 * and none in a small one. The core depth is the one absolute figure, because
 * a coreness is a small integer and the depth is what HLR-170 asks a user to
 * state.
 */
#define ELC_DEFAULT_SINK_AUTHORITY   90u /* a sink outranks this many       */
#define ELC_DEFAULT_SINK_HUB         10u /* and calls less than this many   */
#define ELC_DEFAULT_GOD_BETWEENNESS  90u /* a god object outranks this many */
#define ELC_DEFAULT_GOD_HUB          90u /* on both counts                  */
#define ELC_DEFAULT_CORE_DEPTH        2u /* below this core, peripheral     */

typedef struct {
	uint32_t sink_authority;  /* percent (HLR-168) */
	uint32_t sink_hub;        /* percent (HLR-168) */
	uint32_t god_betweenness; /* percent (HLR-169) */
	uint32_t god_hub;         /* percent (HLR-169) */
	uint32_t core_depth;      /* a coreness (HLR-170) */
} PurifyThresholds;

/* The fan-out bands of PVD Appendix A.2, after Henry–Kafura.
 *
 * **Exhaustive by construction**: every value from 0 upward falls in exactly
 * one band. 0–2 is below the healthy range, 3–7 is healthy, 8–10 is
 * acceptable, and none of the three produces a finding; 11–15 warns, and
 * above 15 is critical. The acceptable band is the one an earlier reading of
 * the thresholds left as a gap, which is why the requirement states the bands
 * are exhaustive rather than leaving it to be inferred (HLR-086). */
#define ELC_FANOUT_HEALTHY_MIN 3u
#define ELC_FANOUT_HEALTHY_MAX 7u
#define ELC_FANOUT_WARNING    10u  /* above this, weak abstraction   */
#define ELC_FANOUT_CRITICAL   15u  /* above this, a god function     */

/* Call-depth bands, from embedded practice rather than a numbered rule: on a
 * target with a couple of kilobytes of SRAM, depths beyond 8 to 12 layers risk
 * the stack colliding with the heap (PVD Appendix A.2). */
#define ELC_DEPTH_WARNING      8u
#define ELC_DEPTH_CRITICAL    12u

/* The cyclomatic-complexity bands (HLR-185).
 *
 * McCabe's own limit is 10, and NIST SP 500-235 records 15 as the highest
 * limit projects have used successfully — and only where an organisation has
 * the review practices to justify it. So above 10 warns and above 15 is
 * critical, and both numbers are somebody else's.
 *
 * These are separate from `ELC_DEFAULT_COMPLEXITY_THRESHOLD`, which is the
 * value `--complexity-threshold` sets and governs a *listing* rather than a
 * severity (HLR-022, HLR-023). The two share a number by coincidence of history, not
 * by construction: changing the listing threshold must not move a band. */
#define ELC_COMPLEXITY_WARNING  10u
#define ELC_COMPLEXITY_CRITICAL 15u

/* The Mock Burden Score weights (HLR-221), and the Testing Burden bands
 * (HLR-224) — **the third and fourth thresholds `elc` invented**.
 *
 * The base tax is charged to every analysed function: on a target with no
 * operating system, even a mock that does nothing is a symbol that has to be
 * defined and linked, so nothing is free. The return and parameter weights
 * charge what a mock must *decide* — nothing for a `void` return, a scalar
 * for a primitive one, and storage that outlives the call for a pointer or an
 * aggregate.
 *
 * The weights live here and the *kinds* live in `signature.scm`. That split
 * is what lets LLR-MBS-02 say the binary holds no language knowledge: this
 * header knows that an aggregate costs 0.25 and knows nothing whatever about
 * which C spellings are aggregates.
 *
 * The bands are `elc`'s own, like the two above them, and for a sharper
 * reason: the index is unpublished, so there is no calibration anywhere to
 * borrow — not even a published figure that had to be rejected, which is what
 * the Adapted Maintainability Index had before it was retired. */
#define ELC_MBS_BASE_TAX        0.25
#define ELC_MBS_RETURN_VOID     0.00
#define ELC_MBS_RETURN_PRIM     0.10
#define ELC_MBS_RETURN_AGG      0.25
#define ELC_MBS_PARAM_PRIM      0.10
#define ELC_MBS_PARAM_AGG       0.25

#define ELC_TBI_WARNING        20.0  /* at or above, heavier mock management */
#define ELC_TBI_CRITICAL       45.0  /* at or above, refactoring indicated   */

/* The band a Testing Burden Index falls in, as the string the report prints
 * and the interactive payload carries (HLR-224, HLR-225).
 *
 * Tested downwards so that an index at the critical bound yields one band and
 * not two, and defined once here so that the text report, the record and the
 * drawing are given the same decision rather than each comparing the bounds
 * again (LLR-CYT-06).
 */
static inline const char *elc_tbi_status(double tbi)
{
	if (tbi >= ELC_TBI_CRITICAL)
		return "critical";
	if (tbi >= ELC_TBI_WARNING)
		return "warning";
	return "healthy";
}

/* The fan-in band (HLR-186), and **the second threshold `elc` invented**.
 *
 * No published source bands fan-in. Twenty-five distinct callers is where a
 * function stops being a routine and starts being an interface that cannot be
 * changed, and that is a judgement rather than a citation — so the catalogue
 * row carries `ELC_OWN_HEURISTIC` and the report says so wherever the finding
 * appears. There is no critical band: `elc` has no basis for a second line it
 * did not draw from anywhere either. */
#define ELC_FANIN_WARNING      25u

/* The structure of the XML record this build writes and accepts (HLR-061).
 *
 * Incremented whenever an element is removed or its meaning changes — not
 * when one is added, since a reader ignores elements it does not recognise.
 * A record carrying any other version is rejected rather than read
 * optimistically (HLR-058). */
#define ELC_XML_FORMAT_VERSION 2

/* What the run is being asked to do. */
typedef enum {
	MODE_ANALYSE = 0,
	MODE_REGENERATE   /* rebuild a report from a saved record (HLR-055) */
} RunMode;

/* The rendered form of the report. */
typedef enum {
	FORMAT_TABLE = 0, /* the default (HLR-027)                          */
	FORMAT_CSV,       /* one record per function, flat (HLR-028)         */
	FORMAT_XML,       /* the complete record of a run (HLR-054)          */
	FORMAT_MARKDOWN,  /* GitHub-Flavored Markdown (HLR-029)              */
	FORMAT_HTML       /* the interactive drawing of the graph (HLR-215).
	                   *
	                   * Selected by `.html` like every other format, and
	                   * unlike the others it presents its information in
	                   * the context of the drawing rather than as the
	                   * tiers HLR-031 makes uniform — which is why that
	                   * requirement exempts it, as it already exempts CSV
	                   * and the record.                                 */
} OutputFormat;

/* One declared architectural stratum: a named layer, the component patterns
 * assigned to it, and its position in the declared dependency direction
 * (HLR-078).
 *
 * The ordinal is what makes a direction out of a set of names. Layer 0 is the
 * top — the layer permitted to depend on those below it — so a call from a
 * higher ordinal to a lower one runs against the declaration. Strata are never
 * discovered from the filesystem: a directory layout is a convention, and
 * inferring an architecture from one would report violations against a design
 * nobody stated.
 */
typedef struct {
	char   *name;          /* owned */
	char  **patterns;      /* owned, each and the array */
	size_t  pattern_count;
	size_t  ordinal;       /* 0 is the topmost declared layer */
} StratumDecl;

typedef struct {
	StratumDecl *items;
	size_t       count;
	size_t       capacity;
} StratumList;

/* One declared execution scope: a name, and the component patterns belonging
 * to it (HLR-094).
 *
 * A component is a file, so the patterns are matched against file paths with
 * fnmatch(3) — the same shape `--stratum` will use, which is why the two are
 * parsed by sibling functions rather than by one that guesses which it is.
 */
typedef struct {
	char   *name;          /* owned */
	char  **patterns;      /* owned, each and the array */
	size_t  pattern_count;
} ScopeDecl;

typedef struct {
	ScopeDecl *items;
	size_t     count;
	size_t     capacity;
} ScopeList;

/* The complete, validated configuration of one run.
 *
 * Populated only by cli_parse() and read-only thereafter (HLR-039): there is
 * no configuration file and no dotfile discovery, so this structure and the
 * runtime directory are the whole of elc's configuration surface.
 */
typedef struct {
	RunMode       mode;
	OutputFormat  format;
	const char   *input_path;   /* the saved record, in regeneration mode */
	const char   *output_path;  /* NULL when writing to stdout (HLR-030) */
	uint32_t      complexity_threshold; /* listing only; never the exit
	                                     * status (HLR-022, HLR-023)     */
	/* The entry points reachability and depth are measured from
	 * (HLR-095). Never inferred and never read from a file: a tool that
	 * guessed at `main` would be wrong for a library, an interrupt table,
	 * or a plugin, and would be wrong silently. Empty means the analyses
	 * that need it are omitted with a stated reason, not that everything
	 * is unreachable (HLR-115).
	 *
	 * The symbols are borrowed from argv; the array holding them is
	 * owned, because getopt hands them over one at a time. */
	const char  **entry_points;
	size_t        entry_point_count;
	size_t        entry_point_capacity;
	/* Custom rule files named on the command line, each in the `lang:path`
	 * form (HLR-107). Empty means the run uses only the rules the runtime
	 * location holds, which is usually none.
	 *
	 * Borrowed from argv like the entry points, and unsplit: the language
	 * and the path are both substrings of one argument, and splitting here
	 * would allocate two strings for a decision `registry.c` has to make
	 * anyway — it is the module that knows which languages exist. */
	const char  **rules;
	size_t        rule_count;
	size_t        rule_capacity;
	/* The conditional-compilation symbols in force, each as given: `NAME`
	 * or `NAME=VALUE` (HLR-131). Borrowed from argv like the rules.
	 *
	 * **An empty set prunes nothing**, and that is not an optimisation: a
	 * symbol elc was not told about is one it cannot decide, since a build
	 * may define it in a header or on a command line elc never sees. So no
	 * definitions means every definedness test is undecidable, which is
	 * exactly the "adding the option changes no existing result" that
	 * HLR-131 requires — reached by the rule rather than by a special
	 * case. */
	const char  **defines;
	size_t        define_count;
	size_t        define_capacity;
	/* The linked image every measurement is restricted to the functions of,
	 * or NULL for no filtering (HLR-140). Borrowed from argv.
	 *
	 * The path only. The image is read by `elfsyms.c`, which owns the
	 * failure, so a run with no image differs from a filtered one in
	 * exactly one place — and the parser stays the module that reads argv
	 * rather than becoming one that reads files (LLR-CLI-22). */
	const char   *image_path;
	/* The execution scopes cross-scope access is measured against
	 * (HLR-094). Empty means the analysis is omitted with a stated
	 * reason, exactly as an empty entry-point set does. Owned outright,
	 * unlike the entry points: a declaration is split into a name and a
	 * pattern list, and neither substring exists in argv. */
	ScopeList     scopes;
	/* The architectural strata layering is validated against (HLR-078),
	 * and the declared direction of dependency between them. Owned
	 * outright, as the scopes are, and for the same reason.
	 *
	 * With no strata the analysis is omitted with a stated reason. The
	 * ordinals come from the order the strata were declared unless
	 * `--stratum-order` states them, which is resolved once after parsing
	 * so that the two options may appear in either order. */
	StratumList   strata;
	const char   *stratum_order;  /* borrowed from argv; NULL if absent */
	/* Ca and Ce floor at which a component is a bottleneck (HLR-081).
	 * `elc`'s own heuristic, and marked as such wherever reported. */
	uint32_t      bottleneck_threshold;
	/* The thresholds the recovery view is purified against (HLR-168 –
	 * HLR-171). `elc`'s own heuristics, every one of them, and marked as
	 * such wherever a classification made against them is reported.
	 *
	 * They govern a *view* and nothing else: no measurement, finding, or
	 * artefact reported outside architecture recovery is computed over a
	 * purified graph, so changing one of these cannot move a number
	 * (HLR-167). */
	PurifyThresholds purify;
	/* Write a debug companion beside the report (HLR-194). Off unless
	 * asked for, and silently nothing when the report goes to standard
	 * output, by the companion rule every other artefact follows: the
	 * name is derived from the report's and there is then none to derive
	 * (HLR-119). */
	bool          debug_log;
	bool          graphml;      /* export the SDG (HLR-106); off unless
	                             * asked for, and silently nothing when
	                             * the report goes to stdout             */
	/* Write the Dependency Structure Matrix as a CSV companion beside the
	 * report (HLR-180). Off unless asked for, and silently nothing when
	 * the report goes to standard output, exactly as the GraphML export
	 * is: the companion's name is derived from the report's, and there is
	 * then no name to derive.
	 *
	 * Unlike the GraphML export, this one is available in regeneration
	 * mode. A saved record carries the matrix, so there is something to
	 * write from (HLR-054). */
	bool          dsm;
	/* The purification manifest to read, or NULL for none (HLR-176).
	 *
	 * Borrowed from argv, like the image path and for the same reason: the
	 * file is read by `purify.c`, which owns the failure, so the parser
	 * stays the module that reads argv rather than becoming one that reads
	 * files (LLR-CLI-22).
	 *
	 * **Only ever this.** No manifest is discovered from the working
	 * directory, the target, an ancestor of either, or a dotfile: a
	 * manifest is read because the user named it, exactly as a custom rule
	 * file is, and the zero-configuration guarantee is unchanged by the
	 * option existing (HLR-039, HLR-176).
	 */
	const char   *manifest_path;
	/* Write the purification manifest beside the report (HLR-175). Off
	 * unless asked for, and silently nothing when the report goes to
	 * standard output: the manifest is a companion artefact and takes its
	 * name from the report's path by the rule every companion follows
	 * (HLR-104, HLR-119). */
	bool          write_manifest;
	/* Write the raw and purified drawings beside the report (HLR-178). One
	 * flag for the pair, because a single drawing of the recovery view
	 * cannot show what purification acted on — the two exist to be
	 * compared, and producing one without the other would answer half the
	 * question. Off unless asked for, and governed by the same companion
	 * rule as the rest. */
	bool          purify_dot;
	/* The `.dot` call tree runs the other way round: it is written unless
	 * refused (HLR-103), so the flag records the refusal rather than the
	 * request. Stored negated so that a zeroed ElcOptions means the
	 * default, which is what every unit test constructs (LLR-WAR-01). */
	bool          no_dot;
	/* Present the verbose report — every tier of HLR-031 — rather than the
	 * summary tiers alone (HLR-150, HLR-151). A property of the rendering
	 * and of nothing else: it selects how much of the model is printed,
	 * never what is measured, and never the exit status.
	 *
	 * Stored as the request rather than as the default, so that a zeroed
	 * ElcOptions means the summary, which is what every unit test
	 * constructs. The complete-record formats ignore it outright
	 * (HLR-152). */
	bool          verbose;
	/* Expansion is on by default and this is the request to turn it off,
	 * so a zeroed ElcOptions expands — the same convention `verbose`
	 * takes. `cc` overrides the driver, and is the only way a user tells
	 * `elc` which toolchain to ask; nothing is read from the environment,
	 * which would breach HLR-039. */
	bool          no_expand;
	const char   *cc;           /* borrowed from argv; not owned          */
	/* Flags passed to the preprocessor verbatim — `-I`, `-D`, `-std=`.
	 * Supplied rather than invented, which is the whole difference: a
	 * path elc guessed at would read a header the user did not name
	 * (HLR-039), while one they typed is an argument like any other, and
	 * without a way to give one, expansion falls back on every project
	 * whose headers are not beside its sources. */
	const char  **cc_flags;     /* borrowed from argv; not owned          */
	size_t        cc_flag_count;
	size_t        cc_flag_capacity;
	const char  **targets;      /* borrowed from argv; not owned          */
	size_t        target_count;
} ElcOptions;

/* The metrics for one reported function, including nested named functions.
 *
 * Phase 2 carries identity. `eloc` arrives in Phase 3, `complexity` in
 * Phase 4, and `node_id` with the graph in Phase 8 (doc/SDD.md §18).
 */
/* Whether the language exposes a function outside the file or module that
 * defines it (HLR-209).
 *
 * Three states and not two. A language whose module supplies no visibility
 * query answers neither, and that is a different claim from "public" — the
 * asymmetry HLR-138 draws for a language with no dead-code query, applied to a
 * third kind of absence.
 */
typedef enum {
	VISIBILITY_UNKNOWN = 0,
	VISIBILITY_PUBLIC,
	VISIBILITY_PRIVATE
} Visibility;

typedef struct {
	char     *name;       /* copied out of the mapping before it is
	                       * released, since the name outlives it        */
	uint32_t  start_line; /* 1-based; TSPoint.row is 0-based and
	                       * converted exactly once                      */
	uint32_t  end_line;   /* 1-based                                     */
	/* What the language says about this function's reach, from that
	 * language's own visibility query. Unknown where the module supplies
	 * none, which the report states rather than resolving (HLR-209). */
	Visibility visibility;
	uint32_t  eloc;       /* statements attributed to this function
	                       * alone, never to one enclosing it (HLR-068)  */
	uint32_t  complexity; /* 1 + the decision points attributed to it    */
	/* The flow degrees, attached to the function rather than kept in a
	 * table beside it (HLR-183). `analyze.c` cannot fill them — they are
	 * properties of the whole-project graph, not of one file's syntax — so
	 * they are zero until `report_attach_flow` runs, and zero is also the
	 * measured value for a function at either end of the call graph. The
	 * two are indistinguishable here on purpose: a function no analysed
	 * function calls and a function measured before the graph existed both
	 * report nothing, and the report distinguishes them by whether the
	 * graph was built at all, never by inspecting a degree (HLR-085,
	 * HLR-156). */
	uint32_t  fan_in;
	uint32_t  fan_out;
	/* What this function costs to replace with a mock (HLR-221).
	 *
	 * A property of the function's own signature and of nothing else, so
	 * unlike the degrees above it this one *is* known from one file's
	 * syntax and is filled here, by `analyze.c`, as the function is
	 * extracted. It is the callee's figure: what a caller pays is the sum
	 * of this field over everything it calls, which is `wf_out`.
	 *
	 * Never zero for an analysed function — the base mocking tax is
	 * charged to every one of them — so zero means "not measured", which
	 * is what a function recovered from debug information rather than
	 * parsed reports (HLR-171). */
	double    mock_burden;
	/* The weighted fan-out and the Testing Burden Index formed from it
	 * (HLR-222, HLR-223). Both are properties of the whole-project graph,
	 * so both are zero until `report_attach_flow` runs, exactly as the
	 * degrees above are — and for `wf_out` zero is also the measured value
	 * for a function that calls nothing resolvable, the two being
	 * indistinguishable here for the same reason and by the same rule. */
	double    wf_out;
	double    tbi;
} FunctionMetric;

/* One function the source defines and the linked image does not (HLR-143).
 *
 * The second of the two directions of mismatch, and the finding the `--elf`
 * option exists to produce: it is dead code established by what the linker
 * did, rather than inferred from the call graph (HLR-096). Named and located,
 * because the reader's next action is to open the file.
 */
typedef struct {
	char     *name;       /* as the source writes it; owned            */
	uint32_t  line;       /* 1-based, where the definition starts      */
} AbsentFunction;

/* Per-file totals and the functions the file defines. */
typedef struct {
	char           *path;           /* canonical absolute path; owned   */
	/* The directory containing this file, derived once from the path
	 * discovery already canonicalised (HLR-160).
	 *
	 * A component *is* a file, so this is the directory a component
	 * belongs to, and it is recorded rather than recomputed because more
	 * than one analysis groups by it — the dependency matrix over
	 * directories most of all. Two consumers each slicing the path for
	 * themselves is how two of them come to disagree about which
	 * directory a file is in.
	 *
	 * No trailing slash, and "/" for a file at the root, so that the
	 * directory of `/a/b.c` and of `/a/c.c` compare equal by strcmp. */
	char           *directory;      /* owned                            */
	char           *language;       /* owned; a copy of the language
	                                 * module's name, so that a model
	                                 * rebuilt from a saved record — where
	                                 * no module exists — releases it the
	                                 * same way                          */
	uint32_t        physical_lines; /* newline count from the mapping    */
	/* Lines the grammar could not follow, counted distinctly. Non-zero
	 * means every figure below covers the rest of the file and not this
	 * part of it — carried so that a partial measurement can never be
	 * mistaken for a complete one (HLR-035). */
	uint32_t        unparsed_lines;
	/* Conditional regions this configuration could not decide, left active
	 * in full (HLR-133). Reported, because the completeness of the pruning
	 * is a fact about the measurement in the way the unresolved-call count
	 * is a fact about the graph — a figure whose accuracy is unstated
	 * cannot be acted on. */
	uint32_t        undecided_regions;
	/* Conditional regions this configuration could not decide and the
	 * image's line information could — a region the build compiled no
	 * instruction for, or one it plainly did (HLR-211).
	 *
	 * Counted apart from the regions a `-D` settled and from those still
	 * undecided, because it is a different claim from either. A `-D` is
	 * what the user says the configuration is; this is evidence about the
	 * build in front of `elc`, strong enough to act on and not proof —
	 * HLR-154's limit on what an absent line means applies to it — and a
	 * figure that lumped the two together would let a reader take the
	 * second for the first. Zero on every run with no image. */
	uint32_t        image_decided_regions;
	/* How this file's text reached the parser: expanded by the
	 * preprocessor, or measured as written and why (HLR-206). Two files in
	 * one report may have been measured two different ways, and nothing in
	 * the figures says which. Stored as an int rather than the enum so
	 * that elc.h stays free of preproc.h, which nothing else here needs. */
	int             preproc_status;
	/* Repairs made where expansion did not happen, in total and by rule
	 * (HLR-199). Zero for an expanded file, which needed none: the two
	 * paths are exclusive and the report says which each file took. */
	size_t          repairs;
	size_t          repair_counts[3];
	/* Standard-library headers this file's expansion drew on, and how many
	 * of them are C++ rather than C (HLR-207). Empty for a file that fell
	 * back, which is the absence of an answer and not the answer "none". */
	char          **stdlib_headers;
	unsigned char  *stdlib_kinds;
	size_t          stdlib_count;
	size_t          stdlib_cxx;
	uint32_t        eloc;           /* file-level ELOC, including code
	                                 * outside any function (HLR-019)    */
	/* The part of that total belonging to no function — an initialised
	 * object at file scope, say. Always measured; reported only when a
	 * filter is in force, because the image's *function* set says nothing
	 * about code that is not a function, and folding it into the totals
	 * would hide the one part of them the filter did not narrow
	 * (HLR-145, LLR-ANL-53). */
	uint32_t        scope_eloc;
	/* Lines excluded because the image's debug line information shows this
	 * build compiled no instruction for them (HLR-153). Counted per file
	 * and summed for the report, so that a reader can see how far the
	 * figures were narrowed by the image rather than inferring it. */
	uint32_t        pruned_lines;
	/* True where the image's line information does not cover this file —
	 * because the translation unit holding it was compiled without debug
	 * information, or because the image's line information is partial.
	 *
	 * The flag governs the pruning rather than merely describing it: no
	 * line in an uncovered file is excluded on this account (HLR-154).
	 * Absence of a line from a mapping that never described the file is
	 * evidence of nothing at all, and treating it as evidence would
	 * silently delete measured code — a report that is confidently wrong
	 * and indistinguishable from a correct one.
	 *
	 * False on every run with no image, where the question does not arise
	 * and nothing is pruned either way. */
	bool            coverage_unestablished;
	FunctionMetric *functions;      /* dynamic array, grown by doubling  */
	size_t          function_count;
	/* The functions this file defines that the image does not, in the order
	 * the parse found them — which is the query's, not the source's, so the
	 * report sorts them before presenting them. Empty when no image was
	 * supplied (HLR-143). */
	AbsentFunction *absent;
	size_t          absent_count;
	size_t          absent_capacity;
} FileMetrics;

/* Why a depth figure is or is not present.
 *
 * Three of the four are absences with different causes, and they are kept
 * distinct because a reader who sees no depth deserves to know which one
 * happened. "You declared nothing" and "what you declared does not exist
 * here" call for different actions.
 */
typedef enum {
	DEPTH_MEASURED = 0,
	DEPTH_OMITTED_NO_ENTRY_POINTS,   /* none declared (HLR-115)        */
	DEPTH_OMITTED_ENTRY_UNRESOLVED,  /* declared, none of them found   */
	DEPTH_UNBOUNDED_RECURSION        /* no finite answer exists        */
} DepthState;

/* Whether reachability was measured, and if not, why not.
 *
 * The same three-way distinction the depth carries, and for the same reason:
 * "nothing was declared" and "what you declared is not here" send a reader to
 * different actions. Reporting nothing unreachable in both cases is required
 * either way — `elc` never calls a function dead for want of a declaration
 * (HLR-115, LLR-STA-01).
 */
typedef enum {
	REACH_MEASURED = 0,
	REACH_OMITTED_NO_ENTRY_POINTS,  /* none declared (HLR-115)        */
	REACH_OMITTED_ENTRY_UNRESOLVED  /* declared, none of them found   */
} ReachState;

/* Whether cross-scope access was measured (HLR-094, HLR-115). */
typedef enum {
	SCOPES_MEASURED = 0,
	SCOPES_OMITTED_NONE_DECLARED
} ScopeState;

/* The severity of a finding: a closed, ordered set, exactly one per finding
 * (HLR-123).
 *
 * **A label within the report and nothing more.** No severity reaches the exit
 * status, which is reserved for the failure conditions of Section 7 — deciding
 * what a critical finding warrants is the caller's business, not `elc`'s
 * (HLR-100). The ordering exists so that where two bands apply to one
 * measurement the higher wins, and so the report can be ranked.
 */
typedef enum {
	SEVERITY_INFO = 0,
	SEVERITY_WARNING,
	SEVERITY_CRITICAL
} Severity;

/* Which measurement a finding is about.
 *
 * One entry per row of the threshold catalogue (SDD §12.2.1), and the
 * catalogue is the only place a band is written down. A kind the catalogue
 * holds no row for is reported as a bare value with no severity rather than
 * being dropped or given an invented band (HLR-098, LLR-THR-08); no kind
 * below is in that position today, and the path stays because the next
 * measurement without a published band will be.
 */
typedef enum {
	MEASURE_FAN_OUT = 0,       /* per function   (HLR-086)  */
	MEASURE_FAN_IN,            /* per function   (HLR-186)  */
	MEASURE_COMPLEXITY,        /* per function   (HLR-185)  */
	MEASURE_CALL_DEPTH,        /* per project    (HLR-087)  */
	MEASURE_RECURSION,         /* per cycle      (HLR-089)  */
	MEASURE_COMPONENT_CYCLE,   /* per cycle      (HLR-083)  */
	MEASURE_SCOPE_REDUCTION,   /* per global     (HLR-092)  */
	MEASURE_HIDDEN_CHANNEL,    /* per global     (HLR-093)  */
	MEASURE_INSTABILITY,       /* per component  (HLR-082)  */
	MEASURE_BOTTLENECK,        /* per component  (HLR-081)  */
	MEASURE_MISRA_LIBRARY,     /* per call site  (HLR-207)  */
	MEASURE_TESTING_BURDEN,    /* per function   (HLR-224)  */
	MEASURE_KIND_COUNT
} MeasurementKind;

/* Whether the layering validation ran, and if not, why not.
 *
 * Two states rather than the three reachability carries, and the difference is
 * the spec's rather than an oversight: a stratum pattern matching no component
 * is a *diagnostic* and the empty layer is retained (LLR-ARC-04), so
 * "declared but matching nothing" never becomes an omission the way an
 * unresolved entry point does.
 */
typedef enum {
	STRATA_MEASURED = 0,
	STRATA_OMITTED_NONE_DECLARED   /* none declared (HLR-115) */
} StrataState;

/* How a call offends against the declared layering.
 *
 * The two are **orthogonal**, which is why they are separate kinds rather than
 * one "layering violation". They fall out of one ordinal comparison: a call
 * descending two layers bypasses an intervening one without inverting
 * anything, a call ascending one layer inverts the declared direction without
 * bypassing anything, and a call ascending two does both (HLR-079, HLR-118).
 */
typedef enum {
	LAYER_SKIP_LEVEL = 0,  /* bypasses an intervening layer (HLR-079) */
	LAYER_INVERTED         /* runs against the direction  (HLR-118)   */
} LayerViolationKind;

/* What `classify_globals` concluded about one global object. */
typedef enum {
	GLOBAL_ORDINARY = 0,     /* shared within one call-connected region */
	GLOBAL_SCOPE_REDUCTION,  /* touched by a single function (HLR-092)  */
	GLOBAL_HIDDEN_CHANNEL    /* spans disconnected regions (HLR-093)    */
} GlobalVerdict;

/* ------------------------------------------------------- the graph facts --
 *
 * What the single parse records for the System Dependence Graph, alongside
 * the metrics. They are *facts*, not edges: a call site names a callee that
 * may or may not be defined in this project, and resolving it needs the
 * whole-project symbol table that only exists once every file is analysed
 * (HLR-073, HLR-076).
 *
 * Enclosing functions are recorded as an index into the same file's
 * FileMetrics.functions, so the two structures are read together and neither
 * repeats what the other holds.
 */

/* An index meaning "outside every reported function" — a call or a global
 * access at file scope, which belongs to the file and to no node. */
#define ELC_NO_FUNCTION SIZE_MAX

typedef struct {
	char     *callee;   /* the identifier at the call site; owned      */
	size_t    caller;   /* into FileMetrics.functions, or ELC_NO_FUNCTION */
	uint32_t  line;     /* 1-based                                     */
} CallSite;

/* How a function touches a global object. A declaration is recorded too:
 * without it there is no way to tell a global from any other identifier the
 * read and write patterns happen to match. */
typedef enum {
	GLOBAL_DECLARATION = 0,
	GLOBAL_READ,
	GLOBAL_WRITE
} GlobalAccessKind;

typedef struct {
	char            *name;     /* the object's identifier; owned       */
	size_t           function; /* into functions, or ELC_NO_FUNCTION   */
	uint32_t         line;     /* 1-based                              */
	GlobalAccessKind kind;
} GlobalAccess;

/* Why a statement cannot execute.
 *
 * The two are kept apart because the reader's next action differs: a
 * statement after a `return` is deleted, while a branch under `if (0)` is
 * usually a switch someone meant to flip back (HLR-137).
 */
typedef enum {
	DEAD_AFTER_TERMINATOR = 0, /* a sibling of an unconditional exit    */
	DEAD_LITERAL_CONDITION     /* the branch a written literal excludes */
} DeadCause;

/* One span within a function that cannot execute (HLR-137).
 *
 * Line-ranged rather than byte-ranged: the reader's next action is to open
 * the file, and a byte offset is not where they look.
 */
typedef struct {
	size_t    function;   /* into functions, or ELC_NO_FUNCTION       */
	uint32_t  start_line; /* 1-based                                  */
	uint32_t  end_line;   /* 1-based; a dead branch may span many     */
	DeadCause cause;
} DeadSpan;

/* One match of a user-supplied rule (HLR-109).
 *
 * Line-ranged and nothing else. A rule match is not a finding: `elc` reports
 * that the query matched and forms no view about whether the rule was worth
 * writing, so there is no severity here and no attribution — nothing to attach
 * either to (HLR-111).
 *
 * The identity is the rule file's basename and the capture name that matched,
 * joined, so one file expresses as many named rules as it holds captures.
 */
typedef struct {
	char     *rule;       /* "<basename>.<capture>"; owned            */
	uint32_t  start_line; /* 1-based                                  */
	uint32_t  end_line;   /* 1-based; a match may span many lines     */
} RuleMatch;

/* The raw graph facts from one file's parse (doc/SDD.md §18). */
typedef struct {
	char         *path;            /* canonical absolute path; owned   */
	CallSite     *calls;
	size_t        call_count;
	size_t        call_capacity;
	GlobalAccess *globals;
	size_t        global_count;
	size_t        global_capacity;
	char        **address_taken;   /* identifiers used as values; owned */
	size_t        address_taken_count;
	size_t        address_taken_capacity;
	DeadSpan     *dead;            /* statements that cannot execute   */
	size_t        dead_count;
	size_t        dead_capacity;
	RuleMatch    *rule_matches;    /* custom-rule matches (HLR-109)    */
	size_t        rule_match_count;
	size_t        rule_match_capacity;
	/* False when the language supplied no dead-code query. "Not looked
	 * for" and "none found" are different claims, and a reader who
	 * cannot tell them apart has been told nothing (HLR-139). */
	bool          dead_analysed;
} FileFacts;

typedef struct {
	FileFacts **items;   /* one per analysed file; owned              */
	size_t      count;
	size_t      capacity;
} FactList;

#endif /* ELC_H */
