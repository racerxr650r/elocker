/* report.h — the format-independent report model.
 *
 * report.c assembles every measurement into one structure and imposes the
 * ordering that makes the output deterministic. **Every sort lives here.**
 * No renderer sorts, and no library's enumeration order reaches the output;
 * this file is the single place a reviewer must check to be satisfied that
 * HLR-032's byte-identical guarantee holds (doc/SDD.md §13, LLR-RPT-10/11).
 *
 * Phase 1 carries per-file physical line counts and the project totals over
 * them. Findings, measurements, omissions, and the rest of the model arrive
 * with the analyses that produce them.
 */
#ifndef ELC_REPORT_H
#define ELC_REPORT_H

#include <stddef.h>
#include <stdint.h>

#include "discover.h"
#include "elc.h"

/* One function's fan-out, as the report presents it: by name and location
 * rather than by node identifier, which means nothing to a reader and does
 * not survive a record round trip. */
typedef struct {
	char     *function;  /* owned */
	char     *file;      /* owned */
	uint32_t  line;
	uint32_t  fan_out;
} FanOutRow;

/* One recursive cycle, as a rendered list of member names (HLR-089). */
typedef struct {
	char **members;      /* owned */
	size_t count;
} CycleRow;

/* One step of the deepest call chain (HLR-088). */
typedef struct {
	char     *function;  /* owned */
	char     *file;      /* owned */
	uint32_t  line;
} ChainRow;

/* One global object as the report presents it: by name, with the functions
 * that write it and the functions that read it (HLR-091).
 *
 * The two sets arrive already joined into text. A node identifier means
 * nothing to a reader and does not survive a record round trip, and the sets
 * are read rather than computed with — the same reasoning that turns a cycle
 * into a list of names.
 */
typedef struct {
	char         *object;       /* owned */
	char         *writers;      /* comma-separated names; owned */
	char         *readers;      /* comma-separated names; owned */
	/* The disconnected participants of a hidden channel, grouped by the
	 * region of the call graph each belongs to (HLR-093). Empty for every
	 * other verdict: the grouping is the finding. */
	char         *participants; /* owned */
	GlobalVerdict verdict;
} GlobalStateRow;

/* One function no path reaches from any root (HLR-096). */
typedef struct {
	char     *function;  /* owned */
	char     *file;      /* owned */
	uint32_t  line;
} UnreachableRow;

/* One statement within a function that cannot execute (HLR-137). */
typedef struct {
	char     *file;      /* owned */
	char     *function;  /* owned */
	uint32_t  start_line;
	uint32_t  end_line;
	DeadCause cause;
} DeadRow;

/* One edge by which a declared execution scope reaches another (HLR-094). */
typedef struct {
	char *from_scope;    /* owned */
	char *from_function; /* owned */
	char *to_scope;      /* owned */
	char *to_function;   /* owned */
	char *object;        /* the shared global, or NULL for a call; owned */
} CrossScopeRow;

/* One component's coupling as the report presents it, by path rather than by
 * index (HLR-080 – HLR-082).
 *
 * The instability is carried as a rendered string rather than a double, for
 * the reason the cycle members are carried as names: "undefined" is one of its
 * legitimate values, and a renderer choosing between a number and a word is a
 * decision that would then be made four times and could differ between them.
 */
typedef struct {
	char     *component;   /* owned */
	uint32_t  ca;
	uint32_t  ce;
	char     *instability; /* owned; "undefined" where both are zero */
	bool      bottleneck;
} CouplingRow;

/* One cyclic dependency between components (HLR-083).
 *
 * Both halves travel: the group is what has to be broken up, and the loop is
 * which edge to cut. Rendered as text because a component index means nothing
 * to a reader and does not survive a record round trip.
 */
typedef struct {
	char *components; /* the whole group, comma-separated; owned */
	char *path;       /* a loop through it, arrow-joined and closed; owned */
} CycleDependencyRow;

/* One call offending against the declared layering (HLR-079, HLR-118). */
typedef struct {
	char              *from_stratum;  /* owned */
	char              *from_function; /* owned */
	char              *from_file;     /* owned */
	char              *to_stratum;    /* owned */
	char              *to_function;   /* owned */
	char              *to_file;       /* owned */
	uint32_t           layers_crossed;
	LayerViolationKind kind;
} LayeringRow;

/* One finding as the report presents it: a measurement that fell outside its
 * accepted range, with the severity and the citation that say so.
 *
 * The severity and the source arrive as text, because both are decided once by
 * the catalogue and every renderer and the record must say the same thing. A
 * report that named MISRA in one format and nothing in another would make the
 * attribution unverifiable, which is the point of having one (HLR-099).
 */
typedef struct {
	char *severity;    /* from the closed set; owned (HLR-123) */
	char *measurement; /* what was measured; owned             */
	char *subject;     /* the function, component or object; owned */
	char *where;       /* file, or empty; owned                */
	uint32_t line;     /* 0 where the finding has no single line */
	char *detail;      /* the measurement, rendered; owned     */
	char *source;      /* the published source; owned          */
} FindingRow;

/* Files discovered but not analysed, for want of a language module. The
 * report accounts for every discovered file, so a skip is visible rather
 * than a silent absence (HLR-012). */
typedef struct {
	char  **paths;
	size_t  count;
	size_t  capacity;
} PathList;

/* One function listed for its file because its complexity met or exceeded
 * the threshold (HLR-021). Both fields are borrowed from the report's own
 * files, which outlive the list.
 *
 * The list is built here rather than filtered by a renderer: a renderer is a
 * pure consumer, and a threshold applied at render time would be applied
 * once per format and could differ between them.
 */
typedef struct {
	const char           *file;
	const FunctionMetric *function;
} ThresholdEntry;

typedef struct {
	ThresholdEntry *items; /* ordered by file, then by function start line */
	size_t          count;
	size_t          capacity;
} ThresholdList;

/* Per-file metrics as they accumulate during the run, before assembly.
 * Owns every FileMetrics handed to it. */
typedef struct {
	FileMetrics **files;
	size_t        count;
	size_t        capacity;
	PathList      skipped;
} MetricsAccumulator;

/* Project-level totals across every analysed file (HLR-024), and the
 * most-complex callouts of HLR-026.
 *
 * A callout's `where` fields are NULL when the run analysed nothing. Ties are
 * broken by the stable presentation order, so the callout is a property of
 * the report rather than of the order files were discovered — without that,
 * two runs over the same tree could name different functions and HLR-032
 * would fail.
 */
typedef struct {
	size_t   file_count;
	uint64_t physical_lines;
	uint64_t eloc;
	uint64_t function_count;

	const char *largest_file;      /* highest file-level ELOC; borrowed */
	uint32_t    largest_file_eloc;

	const char *most_complex;      /* function name; borrowed           */
	const char *most_complex_file; /* the file defining it; borrowed    */
	uint32_t    most_complex_value;
} ProjectSummary;

/* One language's share of the project totals, so that the contribution of
 * each language present in the target is separately visible (HLR-025). */
typedef struct {
	const char *language; /* borrowed from a language module */
	size_t      file_count;
	uint64_t    physical_lines;
	uint64_t    eloc;
} LanguageTotals;

typedef struct {
	LanguageTotals *items; /* sorted by language name */
	size_t          count;
	size_t          capacity;
} LanguageList;


/* The model every renderer consumes. Every collection is sorted before a
 * renderer sees it. */
typedef struct {
	ProjectSummary summary;
	FileMetrics  **files;         /* sorted by path; owned            */
	size_t         file_count;
	LanguageList   languages;     /* sorted by name; owned (HLR-025)  */
	RouteList      routes;        /* per directory target (HLR-127)   */
	size_t         unresolved_calls; /* call sites with no resolvable
	                                  * target (HLR-077)              */

	/* The call-tree measurements, copied from the TreeResults main owns
	 * (SDD §18). Copied rather than referenced for the same reason the
	 * routes are: the model outlives the inputs to it, and regeneration
	 * from a record has no analysis to point at. */
	FanOutRow     *fan_out;       /* one per function; owned (HLR-085) */
	size_t         fan_out_count;
	CycleRow      *cycles;        /* owned (HLR-089)                   */
	size_t         cycle_count;
	DepthState     depth_state;
	uint32_t       depth;         /* HLR-087                           */
	ChainRow      *deepest;       /* owned; the chain in full (HLR-088)*/
	size_t         deepest_count;
	ThresholdList  over_threshold; /* the per-file listing (HLR-021)  */
	uint32_t       complexity_threshold; /* the value it was built at */

	/* The global-state and reachability measurements, copied from the
	 * StateResults main owns, for the reason the call-tree rows are:
	 * the model outlives the analysis, and regeneration has none to
	 * point at (SDD §18). */
	GlobalStateRow *global_state;   /* sorted by object; owned (HLR-091) */
	size_t          global_state_count;
	ReachState      reach_state;
	UnreachableRow *unreachable;    /* sorted by file, line; owned      */
	size_t          unreachable_count;
	char          **unreachable_globals; /* sorted; owned (HLR-096)     */
	size_t          unreachable_global_count;
	ScopeState      scope_state;
	CrossScopeRow  *cross_scope;    /* sorted; owned (HLR-094)          */
	size_t          cross_scope_count;

	/* Dead code within functions, and the languages it was not looked for
	 * in. The second is not a detail: a language with no dead-code query
	 * is reported unanalysed, never clean, and the two are different
	 * claims (HLR-137, HLR-139). */
	/* The component-level measurements, copied from the ArchResults main
	 * owns, for the reason the call-tree rows are (SDD §18). */
	CouplingRow        *coupling;     /* sorted by component; owned      */
	size_t              coupling_count;
	uint32_t            bottleneck_threshold; /* the value it was built at */
	CycleDependencyRow *dep_cycles;   /* sorted; owned (HLR-083)         */
	size_t              dep_cycle_count;
	StrataState         strata_state;
	LayeringRow        *layering;     /* sorted; owned (HLR-079, HLR-118) */
	size_t              layering_count;

	/* Every measurement that crossed a published line, with its severity
	 * and citation. Ranked most severe first: the list exists to be acted
	 * on from the top (HLR-098, HLR-123). */
	FindingRow     *findings;       /* owned                            */
	size_t          finding_count;

	DeadRow        *dead;           /* sorted by file, line; owned      */
	size_t          dead_count;
	PathList        dead_unanalysed; /* language names, sorted; owned   */

	PathList       skipped_files; /* sorted by path; owned (HLR-012)  */
} Report;

/* Record a file skipped for want of a language module, copying its path.
 * Returns 0 on success (LLR-RPT-07). */
int metrics_add_skipped(MetricsAccumulator *acc, const char *path);

/* Append one file's metrics, taking ownership of them. Grows by doubling
 * through a checked reallocation; on failure the accumulator is left intact
 * and the caller still owns `metrics` (LLR-RPT-16). Returns 0 on success. */
int metrics_add(MetricsAccumulator *acc, FileMetrics *metrics);

/* Release the accumulator and every FileMetrics it still owns. */
void metrics_free(MetricsAccumulator *acc);

/* Produce the ordered, format-independent model.
 *
 * On success the accumulator's contents move into `*out`, which owns them
 * thereafter, and the accumulator is left empty. A run in which no file was
 * analysed yields a complete model with zero totals, which renders normally
 * (HLR-066, LLR-RPT-12). Returns 0 on success.
 */
int report_assemble(MetricsAccumulator *acc, const RouteList *routes,
                    const ElcOptions *opts, Report *out);

/* Record the count of unresolved call sites on an assembled report.
 *
 * Set after assembly rather than passed into it, because the graph is built
 * *from* the assembled model — its node order is the report's file order —
 * so the count does not exist yet when report_assemble runs (HLR-077).
 */
void report_set_unresolved(Report *report, size_t unresolved);

/* Copy the intra-procedural dead-code findings onto an assembled report,
 * resolving each span's function index to the name a reader can act on.
 *
 * Reads the fact list rather than the graph: dead code within a function is a
 * property of one file's syntax and needs no whole-project resolution, which
 * is why it is recorded during the parse and carried straight through
 * (HLR-137, LLR-DED-06). Must be called before the facts are released.
 */
int report_set_dead(Report *report, const FactList *facts);

/* The published source a global-state verdict is attributed to, or NULL where
 * there is no finding to attribute (HLR-099, LLR-GLB-04).
 *
 * Here rather than in a renderer so that one answer exists for every format,
 * and so that a regenerated report attributes a verdict the same way as a live
 * run without the record having to carry the citation.
 */
const char *global_verdict_attribution(GlobalVerdict verdict);

/* Release the report model and everything it owns. Safe on NULL. */
void report_free(Report *report);

#endif /* ELC_REPORT_H */
