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

/* The structure of the XML record this build writes and accepts (HLR-061).
 *
 * Incremented whenever an element is removed or its meaning changes — not
 * when one is added, since a reader ignores elements it does not recognise.
 * A record carrying any other version is rejected rather than read
 * optimistically (HLR-058). */
#define ELC_XML_FORMAT_VERSION 1

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
	FORMAT_MARKDOWN   /* GitHub-Flavored Markdown (HLR-029)              */
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
	bool          graphml;      /* export the SDG (HLR-106); off unless
	                             * asked for, and silently nothing when
	                             * the report goes to stdout             */
	/* The `.dot` call tree runs the other way round: it is written unless
	 * refused (HLR-103), so the flag records the refusal rather than the
	 * request. Stored negated so that a zeroed ElcOptions means the
	 * default, which is what every unit test constructs (LLR-WAR-01). */
	bool          no_dot;
	const char  **targets;      /* borrowed from argv; not owned          */
	size_t        target_count;
} ElcOptions;

/* The metrics for one reported function, including nested named functions.
 *
 * Phase 2 carries identity. `eloc` arrives in Phase 3, `complexity` in
 * Phase 4, and `node_id` with the graph in Phase 8 (doc/SDD.md §18).
 */
typedef struct {
	char     *name;       /* copied out of the mapping before it is
	                       * released, since the name outlives it        */
	uint32_t  start_line; /* 1-based; TSPoint.row is 0-based and
	                       * converted exactly once                      */
	uint32_t  end_line;   /* 1-based                                     */
	uint32_t  eloc;       /* statements attributed to this function
	                       * alone, never to one enclosing it (HLR-068)  */
	uint32_t  complexity; /* 1 + the decision points attributed to it    */
} FunctionMetric;

/* Per-file totals and the functions the file defines. */
typedef struct {
	char           *path;           /* canonical absolute path; owned   */
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
	uint32_t        eloc;           /* file-level ELOC, including code
	                                 * outside any function (HLR-019)    */
	FunctionMetric *functions;      /* dynamic array, grown by doubling  */
	size_t          function_count;
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
 * One entry per row of the threshold catalogue (SDD §12.2.1). A measurement
 * whose kind has no catalogue entry is reported as a bare value with no
 * severity rather than being dropped or given an invented band (HLR-098,
 * LLR-THR-08).
 */
typedef enum {
	MEASURE_FAN_OUT = 0,       /* per function   (HLR-086)  */
	MEASURE_CALL_DEPTH,        /* per project    (HLR-087)  */
	MEASURE_RECURSION,         /* per cycle      (HLR-089)  */
	MEASURE_COMPONENT_CYCLE,   /* per cycle      (HLR-083)  */
	MEASURE_SCOPE_REDUCTION,   /* per global     (HLR-092)  */
	MEASURE_HIDDEN_CHANNEL,    /* per global     (HLR-093)  */
	MEASURE_INSTABILITY,       /* per component  (HLR-082)  */
	MEASURE_BOTTLENECK,        /* per component  (HLR-081)  */
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
