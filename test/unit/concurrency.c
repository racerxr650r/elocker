/* test/unit/concurrency.c — unit tests for src/concurrency.c (HLR-227 - 231).
 *
 * Built from hand-made facts and reports, as the graph and call-tree tests
 * are: what is under test is the admission rules and the set arithmetic, and a
 * fixture that had to be parsed first would put a query file's behaviour into
 * every assertion about them.
 */
#include <criterion/criterion.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "analyze.h"
#include "concurrency.h"
#include "elc.h"
#include "graph.h"
#include "report.h"

/* ------------------------------------------------------------ scaffolding */

static FileMetrics *file_with(const char *path, const char *const *names,
                              size_t count)
{
	FileMetrics *m = calloc(1, sizeof *m);

	cr_assert_not_null(m);
	m->path     = strdup(path);
	m->language = strdup("c");
	m->functions = calloc(count ? count : 1, sizeof *m->functions);
	cr_assert_not_null(m->functions);
	for (size_t i = 0; i < count; i++) {
		m->functions[i].name       = strdup(names[i]);
		m->functions[i].start_line = (uint32_t)(i * 10 + 1);
		m->functions[i].end_line   = (uint32_t)(i * 10 + 5);
	}
	m->function_count = count;
	return m;
}

static FileFacts *facts_for(const char *path)
{
	FileFacts *f = calloc(1, sizeof *f);

	cr_assert_not_null(f);
	f->path = strdup(path);
	return f;
}

static void add_call(FileFacts *f, const char *callee, size_t caller)
{
	f->calls = realloc(f->calls, (f->call_count + 1) * sizeof *f->calls);
	cr_assert_not_null(f->calls);
	f->calls[f->call_count].callee = strdup(callee);
	f->calls[f->call_count].caller = caller;
	f->calls[f->call_count].line   = 1;
	f->call_count++;
	f->call_capacity = f->call_count;
}

static size_t node_of(const Sdg *g, const char *name)
{
	for (size_t i = 0; i < g->node_count; i++)
		if (strcmp(g->nodes[i].name, name) == 0)
			return i;
	return SIZE_MAX;
}

/* A graph over `names`, with the given calls, and `taken` address-taken. */
typedef struct {
	Sdg          graph;
	Report       report;
	FileMetrics *file;
	FileFacts   *facts;
} Scene;

static void scene(Scene *s, const char *const *names, size_t count,
                  const size_t *from, const char *const *to, size_t calls,
                  const char *taken)
{
	MetricsAccumulator acc  = { 0 };
	ElcOptions         none = { 0 };
	FileMetrics       *files[1];
	FactList           list = { 0 };

	memset(s, 0, sizeof *s);
	s->file  = file_with("/t/a.c", names, count);
	s->facts = facts_for("/t/a.c");

	files[0] = s->file;
	cr_assert_eq(metrics_add(&acc, s->file), 0);
	cr_assert_eq(report_assemble(&acc, NULL, &none, &s->report), 0);
	metrics_free(&acc);

	for (size_t i = 0; i < calls; i++)
		add_call(s->facts, to[i], from[i]);

	list.items = &s->facts;
	list.count = 1;
	cr_assert_eq(graph_build(&list, &s->report, &s->graph), 0);

	if (taken) {
		size_t n = node_of(&s->graph, taken);

		cr_assert_neq(n, SIZE_MAX);
		s->graph.nodes[n].address_taken = true;
	}
}

static void scene_free(Scene *s)
{
	graph_free(&s->graph);
	report_free(&s->report);
	filefacts_free(s->facts);
}

/* Options declaring `main` as the entry point. */
static ElcOptions with_main(void)
{
	static const char *entries[] = { "main" };
	ElcOptions         o         = { 0 };

	o.entry_points      = entries;
	o.entry_point_count = 1;
	return o;
}

/* ------------------------------------------------------- asynchronous roots */

/* Verifies LLR-ASY-01: the shape a vector-table entry has — nothing calls it,
 * its address is installed somewhere, and the linker kept it. */
Test(concurrency, an_address_taken_function_of_in_degree_zero_is_an_async_root)
{
	static const char *const names[] = { "main", "isr", "helper" };
	static const size_t      from[]  = { 0, 1 };
	static const char *const to[]    = { "helper", "helper" };
	Scene      s;
	ElcOptions o = with_main();
	RootSet    r;

	scene(&s, names, 3, from, to, 2, "isr");
	cr_assert_eq(concurrency_roots(&s.graph, &o, NULL, &r), 0);
	cr_assert_eq(r.count, 1, "exactly the handler is a root");
	cr_assert_eq(r.items[0].node, node_of(&s.graph, "isr"));
	cr_assert_eq(r.items[0].origin, ROOT_BY_ADDRESS);

	rootset_free(&r);
	scene_free(&s);
}

/* Verifies LLR-ASY-01: **the false positive the fourth condition exists to
 * exclude**, and the reason this test matters more than the one above it.
 *
 * An exported API function has in-degree zero and a live symbol and is not
 * asynchronous. Without the address-taken condition a library's whole public
 * interface would be the root set, and every finding downstream would inherit
 * that while reading as though it had been measured. */
Test(concurrency, an_exported_function_never_called_is_not_an_async_root)
{
	static const char *const names[] = { "main", "public_api" };
	Scene      s;
	ElcOptions o = with_main();
	RootSet    r;

	/* No address taken, and nothing calls it: exactly the shape the naive
	 * rule admits. */
	scene(&s, names, 2, NULL, NULL, 0, NULL);
	o.isr_regex = "^isr_";   /* evidence supplied, but it matches nothing */

	cr_assert_eq(concurrency_roots(&s.graph, &o, NULL, &r), 0);
	cr_assert_eq(r.count, 0,
	             "an exported function nothing calls is not a handler");

	rootset_free(&r);
	scene_free(&s);
}

/* Verifies LLR-ASY-01: the declared entry point has in-degree zero by
 * construction and would otherwise satisfy every other condition, making the
 * intersection of HLR-228 the whole program. */
Test(concurrency, the_declared_entry_point_is_never_an_async_root)
{
	static const char *const names[] = { "main" };
	Scene      s;
	ElcOptions o = with_main();
	RootSet    r;

	scene(&s, names, 1, NULL, NULL, 0, "main");
	cr_assert_eq(concurrency_roots(&s.graph, &o, NULL, &r), 0);
	cr_assert_eq(r.count, 0, "main is the application, not a handler");

	rootset_free(&r);
	scene_free(&s);
}

/* Verifies LLR-ASY-01 and LLR-ASY-03: where the build installs handlers by a
 * means the source does not show, the pattern supplies the set — and the
 * origin is recorded, a naming convention being a weaker claim than an
 * address. */
Test(concurrency, a_name_matching_the_pattern_is_a_root_without_an_address)
{
	static const char *const names[] = { "main", "isr_timer" };
	Scene      s;
	ElcOptions o = with_main();
	RootSet    r;

	scene(&s, names, 2, NULL, NULL, 0, NULL);
	o.isr_regex = "^isr_";

	cr_assert_eq(concurrency_roots(&s.graph, &o, NULL, &r), 0);
	cr_assert_eq(r.count, 1);
	cr_assert_eq(r.items[0].origin, ROOT_BY_PATTERN,
	             "a pattern-derived root must not claim an address");

	rootset_free(&r);
	scene_free(&s);
}

/* Verifies LLR-ASY-02: **the substitution this function exists to refuse.**
 *
 * With no image and no pattern there is no evidence that any function is
 * asynchronous. The set is empty and the reason recorded, so the caller
 * reports an omission a reader can see rather than findings that inherited an
 * invented root set. */
Test(concurrency, no_image_and_no_pattern_yields_an_omission_not_a_root_set)
{
	static const char *const names[] = { "main", "orphan" };
	Scene      s;
	ElcOptions o = with_main();
	RootSet    r;

	/* Nothing address-taken, so the source carries no evidence either —
	 * an address taken *is* evidence, and a fixture supplying one would be
	 * testing a different case. */
	scene(&s, names, 2, NULL, NULL, 0, NULL);
	cr_assert_eq(concurrency_roots(&s.graph, &o, NULL, &r), 0);
	cr_assert_eq(r.count, 0, "nothing is invented from in-degree alone");
	cr_assert_eq(r.state, ROOTS_NO_EVIDENCE,
	             "and the reason is recorded, so silence is not read as "
	             "a clean bill of health");

	rootset_free(&r);
	scene_free(&s);
}

/* ------------------------------------------------------------- re-entrancy */

/* Verifies LLR-RNT-01: the intersection is the property. */
Test(concurrency, a_function_both_trees_reach_is_re_entrant)
{
	static const char *const names[] = { "main", "isr", "shared" };
	static const size_t      from[]  = { 0, 1 };
	static const char *const to[]    = { "shared", "shared" };
	Scene      s;
	ElcOptions o = with_main();
	RootSet    r;
	uint32_t   entry;
	bool      *marked;

	scene(&s, names, 3, from, to, 2, "isr");
	cr_assert_eq(concurrency_roots(&s.graph, &o, NULL, &r), 0);

	entry  = (uint32_t)node_of(&s.graph, "main");
	marked = calloc(s.graph.node_count, sizeof *marked);
	cr_assert_not_null(marked);
	cr_assert_eq(concurrency_reentrant(&s.graph, &entry, 1, &r, marked), 0);

	cr_assert(marked[node_of(&s.graph, "shared")]);

	free(marked);
	rootset_free(&r);
	scene_free(&s);
}

/* Verifies LLR-RNT-03: a function only the asynchronous tree reaches is not
 * re-entrant — nothing interrupts it in the middle of itself.
 *
 * Asserted separately because an implementation taking the *union* rather than
 * the intersection passes the test above and fails this one. */
Test(concurrency, a_function_only_the_async_tree_reaches_is_not_re_entrant)
{
	static const char *const names[] = { "main", "isr", "isr_only" };
	static const size_t      from[]  = { 1 };
	static const char *const to[]    = { "isr_only" };
	Scene      s;
	ElcOptions o = with_main();
	RootSet    r;
	uint32_t   entry;
	bool      *marked;

	scene(&s, names, 3, from, to, 1, "isr");
	cr_assert_eq(concurrency_roots(&s.graph, &o, NULL, &r), 0);

	entry  = (uint32_t)node_of(&s.graph, "main");
	marked = calloc(s.graph.node_count, sizeof *marked);
	cr_assert_not_null(marked);
	cr_assert_eq(concurrency_reentrant(&s.graph, &entry, 1, &r, marked), 0);

	cr_assert_not(marked[node_of(&s.graph, "isr_only")],
	              "reached from one thread only, so never re-entered");

	free(marked);
	rootset_free(&r);
	scene_free(&s);
}

/* ---------------------------------------------------------- the qualifier */

static void add_global(FileFacts *f, const char *name, size_t function,
                       GlobalAccessKind kind)
{
	f->globals = realloc(f->globals,
	                     (f->global_count + 1) * sizeof *f->globals);
	cr_assert_not_null(f->globals);
	f->globals[f->global_count].name     = strdup(name);
	f->globals[f->global_count].function = function;
	f->globals[f->global_count].line     = 1;
	f->globals[f->global_count].kind     = kind;
	f->global_count++;
	f->global_capacity = f->global_count;
}

/* main and isr, with `obj` touched by whichever of them the flags say, and
 * declared with the qualifiers given. Returns the findings. */
static void qualifier_scene(Scene *s, FindingList *out, bool volatil,
                            bool mmio, bool touch_main, bool touch_async)
{
	static const char *const names[] = { "main", "isr" };
	MetricsAccumulator acc  = { 0 };
	ElcOptions         none = { 0 };
	FactList           list = { 0 };
	bool              *from_main, *from_async;
	size_t             m_node, i_node;

	memset(s, 0, sizeof *s);
	memset(out, 0, sizeof *out);
	s->file  = file_with("/t/a.c", names, 2);
	s->facts = facts_for("/t/a.c");

	cr_assert_eq(metrics_add(&acc, s->file), 0);
	cr_assert_eq(report_assemble(&acc, NULL, &none, &s->report), 0);
	metrics_free(&acc);

	add_global(s->facts, "obj", ELC_NO_FUNCTION, GLOBAL_DECLARATION);
	if (volatil)
		add_global(s->facts, "obj", ELC_NO_FUNCTION, GLOBAL_VOLATILE);
	if (mmio)
		add_global(s->facts, "obj", ELC_NO_FUNCTION, GLOBAL_MMIO);
	if (touch_main)
		add_global(s->facts, "obj", 0, GLOBAL_WRITE);
	if (touch_async)
		add_global(s->facts, "obj", 1, GLOBAL_READ);

	list.items = &s->facts;
	list.count = 1;
	cr_assert_eq(graph_build(&list, &s->report, &s->graph), 0);

	m_node = node_of(&s->graph, "main");
	i_node = node_of(&s->graph, "isr");
	from_main  = calloc(s->graph.node_count, sizeof *from_main);
	from_async = calloc(s->graph.node_count, sizeof *from_async);
	cr_assert_not_null(from_main);
	cr_assert_not_null(from_async);
	from_main[m_node]  = true;
	from_async[i_node] = true;

	cr_assert_eq(concurrency_qualifiers(&s->graph, from_main, from_async,
	                                    out), 0);
	free(from_main);
	free(from_async);
}

static size_t findings_of(const FindingList *f, MeasurementKind kind)
{
	size_t n = 0;

	for (size_t i = 0; i < f->count; i++)
		if (f->items[i].kind == kind)
			n++;
	return n;
}

/* Verifies LLR-VOL-01: sound whatever else is true of the object. Two threads
 * sharing an unqualified object is a defect independent of target, compiler
 * and optimisation level. */
Test(concurrency, a_shared_global_without_the_qualifier_is_critical)
{
	Scene       s;
	FindingList f;

	qualifier_scene(&s, &f, false, false, true, true);
	cr_assert_eq(findings_of(&f, MEASURE_SHARED_UNQUALIFIED), 1);

	findinglist_free(&f);
	scene_free(&s);
}

/* Verifies LLR-VOL-01: the same object correctly declared produces nothing, so
 * the finding marks the defect rather than the sharing. */
Test(concurrency, a_shared_global_with_the_qualifier_yields_no_finding)
{
	Scene       s;
	FindingList f;

	qualifier_scene(&s, &f, true, false, true, true);
	cr_assert_eq(findings_of(&f, MEASURE_SHARED_UNQUALIFIED), 0);
	cr_assert_eq(findings_of(&f, MEASURE_VOLATILE_CONFINED), 0,
	             "shared is not confined");

	findinglist_free(&f);
	scene_free(&s);
}

/* Verifies LLR-VOL-02: the qualifier forbids optimisations nothing requires to
 * be forbidden, and the cost is real. */
Test(concurrency, a_qualified_global_confined_to_one_tree_is_a_warning)
{
	Scene       s;
	FindingList f;

	qualifier_scene(&s, &f, true, false, true, false);
	cr_assert_eq(findings_of(&f, MEASURE_VOLATILE_CONFINED), 1);

	findinglist_free(&f);
	scene_free(&s);
}

/* Verifies LLR-VOL-02: **the exemption, asserted rather than assumed.**
 *
 * A qualified object whose declaration casts an integer to a pointer is a
 * peripheral register. It is confined to one tree by construction, so without
 * this exemption the finding above would fire on every one of them — advising
 * the removal of a qualifier whose absence is a miscompile. */
Test(concurrency, a_memory_mapped_register_is_never_reported_unnecessary)
{
	Scene       s;
	FindingList f;

	qualifier_scene(&s, &f, true, true, true, false);
	cr_assert_eq(findings_of(&f, MEASURE_VOLATILE_CONFINED), 0,
	             "a register must keep its qualifier and must not be "
	             "told to drop it");

	findinglist_free(&f);
	scene_free(&s);
}

/* Verifies LLR-VOL-02 and HLR-101: the finding states the measurement and
 * advises nothing. Neither "remove" nor "refactor" appears, and it says a
 * cause outside elc's view may still require the qualifier. */
Test(concurrency, the_unnecessary_finding_states_the_measurement_and_advises_nothing)
{
	Scene       s;
	FindingList f;
	const char *detail = NULL;

	qualifier_scene(&s, &f, true, false, true, false);
	for (size_t i = 0; i < f.count; i++)
		if (f.items[i].kind == MEASURE_VOLATILE_CONFINED)
			detail = f.items[i].detail;

	cr_assert_not_null(detail);
	cr_assert_null(strstr(detail, "remove"));
	cr_assert_null(strstr(detail, "refactor"));
	cr_assert_not_null(strstr(detail, "may still require it"),
	                   "the hedge is the point: elc cannot see every "
	                   "reason a qualifier is needed");

	findinglist_free(&f);
	scene_free(&s);
}

/* Verifies LLR-RNT-02: sharing an object with a handler is not being called by
 * it. Counting global edges would mark most of a program re-entrant on the
 * strength of one shared counter. */
Test(concurrency, a_global_edge_does_not_make_a_function_re_entrant)
{
	Scene       s;
	FindingList f;
	RootSet     r;
	ElcOptions  o = with_main();
	uint32_t    entry;
	bool       *marked;

	/* main writes and isr reads, so a global edge joins them — and no call
	 * edge does. */
	qualifier_scene(&s, &f, true, false, true, true);
	s.graph.nodes[node_of(&s.graph, "isr")].address_taken = true;

	cr_assert_eq(concurrency_roots(&s.graph, &o, NULL, &r), 0);
	entry  = (uint32_t)node_of(&s.graph, "main");
	marked = calloc(s.graph.node_count, sizeof *marked);
	cr_assert_not_null(marked);
	cr_assert_eq(concurrency_reentrant(&s.graph, &entry, 1, &r, marked), 0);

	cr_assert_not(marked[node_of(&s.graph, "main")]);
	cr_assert_not(marked[node_of(&s.graph, "isr")]);

	free(marked);
	rootset_free(&r);
	findinglist_free(&f);
	scene_free(&s);
}

/* Verifies LLR-ASY-01: the image says what survived the linker, and a
 * function it does not define is entered by nothing whatever its in-degree.
 *
 * The set is built by hand rather than read from an object file: what is under
 * test is the admission rule, and compiling an image to exercise it would put
 * the linker's behaviour into an assertion about `elc`'s. That `elfsyms_open`
 * reads a real image correctly is `test/unit/elfsyms.c`'s subject. */
Test(concurrency, a_function_absent_from_the_image_is_not_an_async_root)
{
	static const char *const names[] = { "main", "kept", "discarded" };
	Scene      s;
	ElcOptions o = with_main();
	RootSet    r;
	SymbolSet  image;
	char      *defined[1];

	scene(&s, names, 3, NULL, NULL, 0, "kept");
	s.graph.nodes[node_of(&s.graph, "discarded")].address_taken = true;

	memset(&image, 0, sizeof image);
	defined[0]   = strdup("kept");
	cr_assert_not_null(defined[0]);
	image.names  = defined;
	image.count  = 1;

	cr_assert_eq(concurrency_roots(&s.graph, &o, &image, &r), 0);
	cr_assert_eq(r.count, 1,
	             "only the function the image defines is admitted");
	cr_assert_eq(r.items[0].node, node_of(&s.graph, "kept"));

	free(defined[0]);
	rootset_free(&r);
	scene_free(&s);
}
