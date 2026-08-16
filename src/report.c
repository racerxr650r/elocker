/* report.c — the format-independent report model.
 *
 * Assembles every measurement into one structure and imposes the ordering
 * the renderers rely on. Centralising every sort here is deliberate: it is
 * the one file a reviewer must read to be satisfied that HLR-032 holds,
 * rather than auditing six renderers and three analysis modules
 * (doc/SDD.md §13, LLR-RPT-10, LLR-RPT-11).
 *
 * discover.c also sorts, and the two are not redundant. Its sort exists so
 * that de-duplication can collapse equal paths and so that analysis order is
 * not the filesystem's; this one exists so that *presentation* order is a
 * property of the model. A later phase that changes how files are discovered
 * cannot silently change how they are presented.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "analyze.h"
#include "calltree.h"
#include "discover.h"
#include "arch.h"
#include "state.h"
#include "elc.h"
#include "report.h"

int metrics_add(MetricsAccumulator *acc, FileMetrics *metrics)
{
	if (acc->count == acc->capacity) {
		size_t        next   = acc->capacity ? acc->capacity * 2 : 16;
		FileMetrics **bigger = realloc(acc->files, next * sizeof *bigger);

		/* The result goes into a temporary that is checked before the
		 * original is overwritten: `x = realloc(x, n)` loses the whole
		 * accumulator on failure (HLR-125). */
		if (!bigger) {
			fputs("elc: out of memory assembling the report\n",
			      stderr);
			return -1;
		}
		acc->files    = bigger;
		acc->capacity = next;
	}

	acc->files[acc->count++] = metrics;
	return 0;
}

static void pathlist_free(PathList *list)
{
	for (size_t i = 0; i < list->count; i++)
		free(list->paths[i]);
	free(list->paths);
	list->paths    = NULL;
	list->count    = 0;
	list->capacity = 0;
}

int metrics_add_skipped(MetricsAccumulator *acc, const char *path)
{
	PathList *list = &acc->skipped;

	if (list->count == list->capacity) {
		size_t next   = list->capacity ? list->capacity * 2 : 8;
		char **bigger = realloc(list->paths, next * sizeof *bigger);

		if (!bigger) {
			fputs("elc: out of memory recording a skipped file\n",
			      stderr);
			return -1;
		}
		list->paths    = bigger;
		list->capacity = next;
	}

	list->paths[list->count] = strdup(path);
	if (!list->paths[list->count]) {
		fputs("elc: out of memory recording a skipped file\n", stderr);
		return -1;
	}
	list->count++;
	return 0;
}

void metrics_free(MetricsAccumulator *acc)
{
	if (!acc)
		return;

	for (size_t i = 0; i < acc->count; i++)
		filemetrics_free(acc->files[i]);
	free(acc->files);
	acc->files    = NULL;
	acc->count    = 0;
	acc->capacity = 0;
	pathlist_free(&acc->skipped);
}

static int by_path(const void *a, const void *b)
{
	const FileMetrics *x = *(FileMetrics *const *)a;
	const FileMetrics *y = *(FileMetrics *const *)b;

	return strcmp(x->path, y->path);
}

/* Accumulate one file into its language's totals, adding the language on
 * first sight. The list is short — one entry per language present — so a
 * linear search costs less than the structure that would avoid it. */
static int language_add(LanguageList *list, const FileMetrics *file)
{
	const char *name = file->language ? file->language : "";

	for (size_t i = 0; i < list->count; i++) {
		if (strcmp(list->items[i].language, name) != 0)
			continue;
		list->items[i].file_count++;
		list->items[i].physical_lines += file->physical_lines;
		list->items[i].eloc           += file->eloc;
		return 0;
	}

	if (list->count == list->capacity) {
		size_t          next   = list->capacity ? list->capacity * 2 : 8;
		LanguageTotals *bigger = realloc(list->items, next * sizeof *bigger);

		if (!bigger) {
			fputs("elc: out of memory summarising by language\n",
			      stderr);
			return -1;
		}
		list->items    = bigger;
		list->capacity = next;
	}

	list->items[list->count].language       = name;
	list->items[list->count].file_count     = 1;
	list->items[list->count].physical_lines = file->physical_lines;
	list->items[list->count].eloc           = file->eloc;
	list->count++;
	return 0;
}

static int by_language(const void *a, const void *b)
{
	const LanguageTotals *x = a;
	const LanguageTotals *y = b;

	return strcmp(x->language, y->language);
}

/* Routes are ordered by target, not by the order they were named. A section
 * listing targets in command-line order makes `elc a b` and `elc b a`
 * different reports, which is exactly what HLR-033 forbids — and it is the
 * kind of thing that only shows up as a determinism failure, never as a
 * wrong number. */
static int by_route_target(const void *a, const void *b)
{
	const RouteRecord *x = a;
	const RouteRecord *y = b;

	return strcmp(x->target, y->target);
}

static int by_string(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

/* Functions within a file are ordered by start line (LLR-RPT-11), with the
 * name as the tie-break. Two functions can share a start line — a nested one
 * declared on the same line as its enclosing body opens — and a comparator
 * that returned 0 there would leave their order to qsort, which is not
 * stable. The tie-break is what keeps HLR-032 true in that case. */
static int by_start_line(const void *a, const void *b)
{
	const FunctionMetric *x = a;
	const FunctionMetric *y = b;

	if (x->start_line != y->start_line)
		return x->start_line < y->start_line ? -1 : 1;
	return strcmp(x->name, y->name);
}

/* Add one function to the per-file threshold listing. */
static int threshold_add(ThresholdList *list, const char *file,
                         const FunctionMetric *function)
{
	if (list->count == list->capacity) {
		size_t          next   = list->capacity ? list->capacity * 2 : 8;
		ThresholdEntry *bigger = realloc(list->items, next * sizeof *bigger);

		if (!bigger) {
			fputs("elc: out of memory listing threshold breaches\n",
			      stderr);
			return -1;
		}
		list->items    = bigger;
		list->capacity = next;
	}

	list->items[list->count].file     = file;
	list->items[list->count].function = function;
	list->count++;
	return 0;
}

/* Select the highest-ELOC file and the highest-complexity function.
 *
 * Called after the model is ordered, and taking a new candidate only on a
 * *strictly* greater value. Both together are the tie-break: scanning in
 * presentation order and refusing to displace an equal value means the
 * winner is whichever sorts first, which is what makes the callout the same
 * on every run (HLR-026, HLR-033).
 */
static void select_callouts(Report *out)
{
	for (size_t i = 0; i < out->file_count; i++) {
		const FileMetrics *file = out->files[i];

		if (!out->summary.largest_file ||
		    file->eloc > out->summary.largest_file_eloc) {
			out->summary.largest_file      = file->path;
			out->summary.largest_file_eloc = file->eloc;
		}

		for (size_t j = 0; j < file->function_count; j++) {
			const FunctionMetric *fn = &file->functions[j];

			if (!out->summary.most_complex ||
			    fn->complexity > out->summary.most_complex_value) {
				out->summary.most_complex       = fn->name;
				out->summary.most_complex_file  = file->path;
				out->summary.most_complex_value = fn->complexity;
			}
		}
	}
}

int report_assemble(MetricsAccumulator *acc, const RouteList *routes,
                    const ElcOptions *opts, Report *out)
{
	memset(out, 0, sizeof *out);

	/* Copied rather than moved: discovery owns its list until the run
	 * ends, and a regenerated report has none to move (LLR-RPT-17). */
	for (size_t i = 0; routes && i < routes->count; i++)
		if (routelist_add(&out->routes, routes->items[i].target,
		                  routes->items[i].route) != 0) {
			fputs("elc: out of memory recording a discovery route\n",
			      stderr);
			return -1;
		}
	out->complexity_threshold = opts->complexity_threshold;

	out->files      = acc->files;
	out->file_count = acc->count;
	acc->files      = NULL;
	acc->count      = 0;
	acc->capacity   = 0;

	out->skipped_files = acc->skipped;
	memset(&acc->skipped, 0, sizeof acc->skipped);

	for (size_t i = 0; i < out->file_count; i++) {
		out->summary.physical_lines += out->files[i]->physical_lines;
		out->summary.eloc           += out->files[i]->eloc;
		out->summary.function_count += out->files[i]->function_count;

		if (language_add(&out->languages, out->files[i]) != 0)
			return -1;
	}
	out->summary.file_count = out->file_count;

	/* Every collection in the model is ordered here, by an explicit key,
	 * so that no renderer sorts and no enumeration order reaches the
	 * output (LLR-RPT-10, LLR-RPT-11). */
	if (out->file_count > 1)
		qsort(out->files, out->file_count, sizeof *out->files, by_path);

	for (size_t i = 0; i < out->file_count; i++)
		if (out->files[i]->function_count > 1)
			qsort(out->files[i]->functions,
			      out->files[i]->function_count,
			      sizeof *out->files[i]->functions, by_start_line);

	if (out->skipped_files.count > 1)
		qsort(out->skipped_files.paths, out->skipped_files.count,
		      sizeof *out->skipped_files.paths, by_string);

	if (out->languages.count > 1)
		qsort(out->languages.items, out->languages.count,
		      sizeof *out->languages.items, by_language);

	if (out->routes.count > 1)
		qsort(out->routes.items, out->routes.count,
		      sizeof *out->routes.items, by_route_target);

	/* Both of these read the model *after* it is ordered, so the listing
	 * comes out in presentation order and the callouts break their ties
	 * by it (HLR-021, HLR-026). */
	for (size_t i = 0; i < out->file_count; i++) {
		const FileMetrics *file = out->files[i];

		for (size_t j = 0; j < file->function_count; j++) {
			if (file->functions[j].complexity <
			    out->complexity_threshold)
				continue;
			if (threshold_add(&out->over_threshold, file->path,
			                  &file->functions[j]) != 0)
				return -1;
		}
	}

	select_callouts(out);

	return 0;
}

/* Copy the call-tree measurements into the model, resolving node identifiers
 * to the names and locations a reader can act on.
 *
 * The translation is the point. A node id is an index into a table that only
 * exists while the graph does; the report outlives it, is rendered in four
 * formats, and round-trips through a record. Carrying identifiers into it
 * would make every one of those a lookup against a structure that has been
 * freed.
 */
int report_set_calltree(Report *report, const TreeResults *tree, const Sdg *g)
{
	if (!tree || !g)
		return 0;

	report->depth_state = tree->depth_state;
	report->depth       = tree->depth;

	/* --- fan-out, one row per function -------------------------------- */

	report->fan_out = calloc(g->node_count ? g->node_count : 1,
	                         sizeof *report->fan_out);
	if (!report->fan_out)
		return -1;

	for (size_t i = 0; i < g->node_count && i < tree->node_count; i++) {
		FanOutRow *row = &report->fan_out[report->fan_out_count];

		row->function = strdup(g->nodes[i].name);
		row->file     = strdup(g->nodes[i].file);
		if (!row->function || !row->file)
			return -1;
		row->line    = g->nodes[i].line_start;
		row->fan_out = tree->fan_out[i];
		report->fan_out_count++;
	}

	/* --- recursive cycles --------------------------------------------- */

	if (tree->cycle_count > 0) {
		report->cycles = calloc(tree->cycle_count, sizeof *report->cycles);
		if (!report->cycles)
			return -1;

		for (size_t c = 0; c < tree->cycle_count; c++) {
			const RecursiveCycle *cycle = &tree->cycles[c];
			CycleRow             *row   = &report->cycles[c];

			row->members = calloc(cycle->count, sizeof *row->members);
			if (!row->members)
				return -1;
			for (size_t m = 0; m < cycle->count; m++) {
				if (cycle->members[m] >= g->node_count)
					continue;
				row->members[row->count] =
					strdup(g->nodes[cycle->members[m]].name);
				if (!row->members[row->count])
					return -1;
				row->count++;
			}
			report->cycle_count++;
		}
	}

	/* --- the deepest chain, in order ---------------------------------- */

	if (tree->deepest.count > 0) {
		report->deepest = calloc(tree->deepest.count,
		                         sizeof *report->deepest);
		if (!report->deepest)
			return -1;

		for (size_t i = 0; i < tree->deepest.count; i++) {
			uint32_t node = tree->deepest.nodes[i];

			if (node >= g->node_count)
				continue;

			ChainRow *row = &report->deepest[report->deepest_count];

			row->function = strdup(g->nodes[node].name);
			row->file     = strdup(g->nodes[node].file);
			if (!row->function || !row->file)
				return -1;
			row->line = g->nodes[node].line_start;
			report->deepest_count++;
		}
	}

	return 0;
}

void report_set_unresolved(Report *report, size_t unresolved)
{
	report->unresolved_calls = unresolved;
}

/* ------------------------------------------------------- the report model --
 *
 * Node identifiers are translated to names here, for the reason the call-tree
 * rows are: an identifier indexes a table that exists only while the graph
 * does, and the report outlives it, renders in four formats, and round-trips
 * through a record.
 */

static int by_object(const void *a, const void *b)
{
	const GlobalStateRow *x = a;
	const GlobalStateRow *y = b;

	return strcmp(x->object, y->object);
}

/* Cross-scope rows order by the boundary they cross, then by the functions at
 * either end — every key something a reader can name, so two runs over one
 * tree list them the same way (HLR-032). */
static int by_cross_scope(const void *a, const void *b)
{
	const CrossScopeRow *x = a;
	const CrossScopeRow *y = b;
	int                  c = strcmp(x->from_scope, y->from_scope);

	if (c != 0)
		return c;
	c = strcmp(x->to_scope, y->to_scope);
	if (c != 0)
		return c;
	c = strcmp(x->from_function, y->from_function);
	if (c != 0)
		return c;
	c = strcmp(x->to_function, y->to_function);
	if (c != 0)
		return c;
	return strcmp(x->object, y->object);
}

/* A string built by appending, since the joined name lists have no bound. */
typedef struct {
	char  *text;
	size_t length;
	size_t capacity;
} Joined;

static int join(Joined *j, const char *separator, const char *text)
{
	size_t need = strlen(text) + (j->length ? strlen(separator) : 0);

	if (j->length + need + 1 > j->capacity) {
		size_t next  = j->capacity ? j->capacity * 2 : 64;
		char  *grown;

		while (next < j->length + need + 1)
			next *= 2;
		grown = realloc(j->text, next);
		if (!grown)
			return -1;
		j->text     = grown;
		j->capacity = next;
		if (j->length == 0)
			j->text[0] = '\0';
	}

	if (j->length)
		j->length += (size_t)snprintf(j->text + j->length,
		                              j->capacity - j->length, "%s",
		                              separator);
	j->length += (size_t)snprintf(j->text + j->length,
	                              j->capacity - j->length, "%s", text);
	return 0;
}

/* Hand the built string to the caller, or an empty owned string when nothing
 * was appended — so no consumer has to distinguish NULL from "". */
static char *joined_take(Joined *j)
{
	char *text = j->text ? j->text : strdup("");

	memset(j, 0, sizeof *j);
	return text;
}

/* The disconnected participants of a hidden channel, grouped by region.
 *
 * The grouping *is* the finding (HLR-093). "a, b, and c touch this object" is
 * ordinary; "{a, b} never call {c}, and all three share it" is the temporal
 * coupling, and only the second tells the reader where the boundary lies.
 */
static char *participants_of(const Sdg *g, const GlobalRow *row)
{
	Joined out = { 0 };

	for (size_t a = 0; a < row->toucher_count; a++) {
		bool seen = false;

		for (size_t b = 0; b < a && !seen; b++)
			seen = row->touchers[b].region == row->touchers[a].region;
		if (seen)
			continue;

		Joined group = { 0 };

		for (size_t m = 0; m < row->toucher_count; m++) {
			if (row->touchers[m].region != row->touchers[a].region)
				continue;
			if (row->touchers[m].node >= g->node_count)
				continue;
			if (join(&group, ", ",
			         g->nodes[row->touchers[m].node].name) != 0) {
				free(group.text);
				free(out.text);
				return NULL;
			}
		}

		char  *text = joined_take(&group);
		size_t size = text ? strlen(text) + 3 : 0;
		char  *cell = size ? malloc(size) : NULL;

		if (!cell) {
			free(text);
			free(out.text);
			return NULL;
		}
		snprintf(cell, size, "{%s}", text);
		free(text);

		if (join(&out, " ", cell) != 0) {
			free(cell);
			free(out.text);
			return NULL;
		}
		free(cell);
	}

	return joined_take(&out);
}

static int set_globals(Report *report, const StateResults *state, const Sdg *g)
{
	if (state->global_count == 0)
		return 0;

	report->global_state = calloc(state->global_count,
	                              sizeof *report->global_state);
	if (!report->global_state)
		return -1;

	for (size_t i = 0; i < state->global_count; i++) {
		const GlobalRow *src = &state->globals[i];
		GlobalStateRow  *row = &report->global_state[i];
		Joined           writers = { 0 };
		Joined           readers = { 0 };

		row->object  = strdup(src->object);
		row->verdict = src->verdict;
		if (!row->object)
			return -1;

		for (size_t t = 0; t < src->toucher_count; t++) {
			const GlobalToucher *touch = &src->touchers[t];

			if (touch->node >= g->node_count)
				continue;
			if (touch->writes &&
			    join(&writers, ", ", g->nodes[touch->node].name) != 0)
				return -1;
			if (touch->reads &&
			    join(&readers, ", ", g->nodes[touch->node].name) != 0)
				return -1;
		}

		row->writers = joined_take(&writers);
		row->readers = joined_take(&readers);
		row->participants = src->verdict == GLOBAL_HIDDEN_CHANNEL
		                            ? participants_of(g, src)
		                            : strdup("");
		if (!row->writers || !row->readers || !row->participants)
			return -1;

		report->global_state_count++;
	}

	return 0;
}

int report_set_state(Report *report, const StateResults *state, const Sdg *g,
                     const ElcOptions *opts)
{
	if (!state || !g)
		return 0;

	report->reach_state = state->reach_state;
	report->scope_state = state->scope_state;

	if (set_globals(report, state, g) != 0)
		return -1;

	/* --- unreachable functions ---------------------------------------- */

	if (state->unreachable_count > 0) {
		report->unreachable = calloc(state->unreachable_count,
		                             sizeof *report->unreachable);
		if (!report->unreachable)
			return -1;

		for (size_t i = 0; i < state->unreachable_count; i++) {
			uint32_t node = state->unreachable[i];

			if (node >= g->node_count)
				continue;

			UnreachableRow *row =
				&report->unreachable[report->unreachable_count];

			row->function = strdup(g->nodes[node].name);
			row->file     = strdup(g->nodes[node].file);
			if (!row->function || !row->file)
				return -1;
			row->line = g->nodes[node].line_start;
			report->unreachable_count++;
		}
	}

	/* --- unreachable globals ------------------------------------------ */

	if (state->dead_global_count > 0) {
		report->unreachable_globals =
			calloc(state->dead_global_count,
			       sizeof *report->unreachable_globals);
		if (!report->unreachable_globals)
			return -1;

		for (size_t i = 0; i < state->dead_global_count; i++) {
			report->unreachable_globals[i] =
				strdup(state->dead_globals[i]);
			if (!report->unreachable_globals[i])
				return -1;
			report->unreachable_global_count++;
		}
	}

	/* --- cross-scope access ------------------------------------------- */

	if (state->violation_count > 0) {
		report->cross_scope = calloc(state->violation_count,
		                             sizeof *report->cross_scope);
		if (!report->cross_scope)
			return -1;

		for (size_t i = 0; i < state->violation_count; i++) {
			const ScopeViolation *v = &state->violations[i];

			if (v->from >= g->node_count || v->to >= g->node_count ||
			    v->from_scope >= opts->scopes.count ||
			    v->to_scope >= opts->scopes.count)
				continue;

			CrossScopeRow *row =
				&report->cross_scope[report->cross_scope_count];

			row->from_scope =
				strdup(opts->scopes.items[v->from_scope].name);
			row->from_function = strdup(g->nodes[v->from].name);
			row->to_scope =
				strdup(opts->scopes.items[v->to_scope].name);
			row->to_function = strdup(g->nodes[v->to].name);
			row->object      = strdup(v->object ? v->object : "");
			if (!row->from_scope || !row->from_function ||
			    !row->to_scope || !row->to_function || !row->object)
				return -1;
			report->cross_scope_count++;
		}
	}

	/* Every collection ordered by an explicit key before a renderer sees
	 * it, as the rest of the model is. The globals and the unreachable
	 * functions arrive in an order the graph already fixed; the cross-scope
	 * rows arrive in edge order, which is deterministic but is not a key a
	 * reader could name (LLR-RPT-10). */
	if (report->cross_scope_count > 1)
		qsort(report->cross_scope, report->cross_scope_count,
		      sizeof *report->cross_scope, by_cross_scope);
	if (report->global_state_count > 1)
		qsort(report->global_state, report->global_state_count,
		      sizeof *report->global_state, by_object);
	if (report->unreachable_global_count > 1)
		qsort(report->unreachable_globals,
		      report->unreachable_global_count,
		      sizeof *report->unreachable_globals, by_string);

	return 0;
}

/* --------------------------------------------------- component coupling --
 *
 * Node and component identifiers are translated to paths and names here, for
 * the reason every other analysis result is: an index addresses a table that
 * exists only while the graph does, and the report outlives it.
 */

static int by_component(const void *a, const void *b)
{
	const CouplingRow *x = a;
	const CouplingRow *y = b;

	return strcmp(x->component, y->component);
}

/* Layering rows order by the boundary crossed, then by the kind, then by the
 * functions at either end — every key something a reader can name, so two runs
 * over one tree list them the same way (HLR-032). */
static int by_layering(const void *a, const void *b)
{
	const LayeringRow *x = a;
	const LayeringRow *y = b;
	int                c = strcmp(x->from_stratum, y->from_stratum);

	if (c != 0)
		return c;
	c = strcmp(x->to_stratum, y->to_stratum);
	if (c != 0)
		return c;
	if (x->kind != y->kind)
		return x->kind < y->kind ? -1 : 1;
	c = strcmp(x->from_function, y->from_function);
	if (c != 0)
		return c;
	return strcmp(x->to_function, y->to_function);
}

static int by_cycle_row(const void *a, const void *b)
{
	const CycleDependencyRow *x = a;
	const CycleDependencyRow *y = b;

	return strcmp(x->components, y->components);
}

/* Join component paths with `separator`, or NULL on allocation failure. */
static char *join_components(const Sdg *g, const size_t *items, size_t count,
                             const char *separator, bool close_loop)
{
	Joined out = { 0 };

	for (size_t i = 0; i < count; i++) {
		if (items[i] >= g->component_count)
			continue;
		if (join(&out, separator, g->component_paths[items[i]]) != 0) {
			free(out.text);
			return NULL;
		}
	}

	/* A loop is closed by naming its first component again, so the reader
	 * sees `a -> b -> c -> a` rather than having to infer the last edge —
	 * which is the edge they are most likely to cut. */
	if (close_loop && count > 0 && items[0] < g->component_count &&
	    join(&out, separator, g->component_paths[items[0]]) != 0) {
		free(out.text);
		return NULL;
	}

	return joined_take(&out);
}

int report_set_arch(Report *report, const ArchResults *arch, const Sdg *g,
                    const ElcOptions *opts)
{
	if (!arch || !g)
		return 0;

	report->strata_state         = arch->strata_state;
	report->bottleneck_threshold = opts->bottleneck_threshold;

	/* --- coupling, one row per component ------------------------------ */

	if (arch->component_count > 0) {
		report->coupling = calloc(arch->component_count,
		                          sizeof *report->coupling);
		if (!report->coupling)
			return -1;

		for (size_t i = 0; i < arch->component_count &&
		     i < g->component_count; i++) {
			const ComponentCoupling *c   = &arch->coupling[i];
			CouplingRow             *row =
				&report->coupling[report->coupling_count];
			char                     value[32];

			row->component = strdup(g->component_paths[i]);
			if (!row->component)
				return -1;
			row->ca         = c->ca;
			row->ce         = c->ce;
			row->bottleneck = c->bottleneck;

			/* Two decimal places, and the word where there is no
			 * number. Formatting once here is what keeps the four
			 * renderers from each deciding how to say "undefined"
			 * (HLR-082, LLR-INS-02). */
			if (c->instability_defined)
				snprintf(value, sizeof value, "%.2f",
				         c->instability);
			else
				snprintf(value, sizeof value, "undefined");

			row->instability = strdup(value);
			if (!row->instability)
				return -1;
			report->coupling_count++;
		}
	}

	/* --- dependency cycles -------------------------------------------- */

	if (arch->cycle_count > 0) {
		report->dep_cycles = calloc(arch->cycle_count,
		                            sizeof *report->dep_cycles);
		if (!report->dep_cycles)
			return -1;

		for (size_t i = 0; i < arch->cycle_count; i++) {
			const ComponentCycle *cycle = &arch->cycles[i];
			CycleDependencyRow   *row   =
				&report->dep_cycles[report->dep_cycle_count];

			row->components = join_components(g, cycle->members,
			                                  cycle->member_count,
			                                  ", ", false);
			row->path       = join_components(g, cycle->path,
			                                  cycle->path_count,
			                                  " -> ", true);
			if (!row->components || !row->path)
				return -1;
			report->dep_cycle_count++;
		}
	}

	/* --- layering ------------------------------------------------------ */

	if (arch->violation_count > 0) {
		report->layering = calloc(arch->violation_count,
		                          sizeof *report->layering);
		if (!report->layering)
			return -1;

		for (size_t i = 0; i < arch->violation_count; i++) {
			const LayerViolation *v = &arch->violations[i];

			if (v->from >= g->node_count || v->to >= g->node_count ||
			    v->from_stratum >= opts->strata.count ||
			    v->to_stratum >= opts->strata.count)
				continue;

			LayeringRow *row =
				&report->layering[report->layering_count];

			row->from_stratum =
				strdup(opts->strata.items[v->from_stratum].name);
			row->from_function = strdup(g->nodes[v->from].name);
			row->from_file     = strdup(g->nodes[v->from].file);
			row->to_stratum =
				strdup(opts->strata.items[v->to_stratum].name);
			row->to_function = strdup(g->nodes[v->to].name);
			row->to_file     = strdup(g->nodes[v->to].file);
			if (!row->from_stratum || !row->from_function ||
			    !row->from_file || !row->to_stratum ||
			    !row->to_function || !row->to_file)
				return -1;
			row->layers_crossed = (uint32_t)v->layers_crossed;
			row->kind           = v->kind;
			report->layering_count++;
		}
	}

	/* Every collection ordered by an explicit key before a renderer sees
	 * it, as the rest of the model is (LLR-RPT-10). */
	if (report->coupling_count > 1)
		qsort(report->coupling, report->coupling_count,
		      sizeof *report->coupling, by_component);
	if (report->dep_cycle_count > 1)
		qsort(report->dep_cycles, report->dep_cycle_count,
		      sizeof *report->dep_cycles, by_cycle_row);
	if (report->layering_count > 1)
		qsort(report->layering, report->layering_count,
		      sizeof *report->layering, by_layering);

	return 0;
}

const char *global_verdict_attribution(GlobalVerdict verdict)
{
	switch (verdict) {
	case GLOBAL_SCOPE_REDUCTION:
	case GLOBAL_HIDDEN_CHANNEL:
		/* Both verdicts come from one rule: an object should be
		 * defined at block scope where only one function names it,
		 * and an object shared between functions that never meet is
		 * the temporal coupling the same rule guards against. */
		return "MISRA C Rule 8.9";
	case GLOBAL_ORDINARY:
	default:
		return NULL;
	}
}

/* ------------------------------------------------------------- dead code --
 *
 * The intra-procedural findings, resolved from the per-file facts. A span
 * carries the index of the function containing it; the report carries names,
 * because an index into a table that no longer exists is not something a
 * reader can act on.
 */

static int dead_row_add(Report *report, size_t *capacity, const char *file,
                        const char *function, const DeadSpan *span)
{
	if (report->dead_count == *capacity) {
		size_t   next   = *capacity ? *capacity * 2 : 16;
		DeadRow *bigger = realloc(report->dead, next * sizeof *bigger);

		if (!bigger) {
			fputs("elc: out of memory listing dead code\n", stderr);
			return -1;
		}
		report->dead = bigger;
		*capacity    = next;
	}

	DeadRow *row = &report->dead[report->dead_count];

	memset(row, 0, sizeof *row);
	row->file     = strdup(file);
	row->function = strdup(function);
	if (!row->file || !row->function) {
		free(row->file);
		free(row->function);
		fputs("elc: out of memory listing dead code\n", stderr);
		return -1;
	}
	row->start_line = span->start_line;
	row->end_line   = span->end_line;
	row->cause      = span->cause;
	report->dead_count++;
	return 0;
}

/* Record a language whose module supplied no dead-code query, once. */
static int unanalysed_add(PathList *list, const char *language)
{
	for (size_t i = 0; i < list->count; i++)
		if (strcmp(list->paths[i], language) == 0)
			return 0;

	if (list->count == list->capacity) {
		size_t next   = list->capacity ? list->capacity * 2 : 4;
		char **bigger = realloc(list->paths, next * sizeof *bigger);

		if (!bigger) {
			fputs("elc: out of memory recording an unanalysed "
			      "language\n", stderr);
			return -1;
		}
		list->paths    = bigger;
		list->capacity = next;
	}

	list->paths[list->count] = strdup(language);
	if (!list->paths[list->count]) {
		fputs("elc: out of memory recording an unanalysed language\n",
		      stderr);
		return -1;
	}
	list->count++;
	return 0;
}

/* The narrowest reported function of `file` containing `line`, or NULL.
 *
 * Narrowest, not first, for the reason `innermost_enclosing` is: a nested
 * named function is reported in its own right, and a finding inside one
 * belongs to it rather than to the function around it (HLR-068).
 */
static const FunctionMetric *enclosing_function(const FileMetrics *file,
                                                uint32_t line)
{
	const FunctionMetric *best = NULL;

	for (size_t i = 0; i < file->function_count; i++) {
		const FunctionMetric *fn = &file->functions[i];

		if (line < fn->start_line || line > fn->end_line)
			continue;
		if (!best || (fn->end_line - fn->start_line) <
		             (best->end_line - best->start_line))
			best = fn;
	}

	return best;
}

static const FileFacts *facts_for_path(const FactList *facts, const char *path)
{
	for (size_t i = 0; facts && i < facts->count; i++)
		if (strcmp(facts->items[i]->path, path) == 0)
			return facts->items[i];
	return NULL;
}

static int by_dead_row(const void *a, const void *b)
{
	const DeadRow *x = a;
	const DeadRow *y = b;
	int            c = strcmp(x->file, y->file);

	if (c != 0)
		return c;
	if (x->start_line != y->start_line)
		return x->start_line < y->start_line ? -1 : 1;
	if (x->end_line != y->end_line)
		return x->end_line < y->end_line ? -1 : 1;
	return strcmp(x->function, y->function);
}

int report_set_dead(Report *report, const FactList *facts)
{
	size_t capacity = 0;

	for (size_t f = 0; f < report->file_count; f++) {
		const FileMetrics *file = report->files[f];
		const FileFacts   *ff   = facts_for_path(facts, file->path);

		if (!ff)
			continue;

		/* "Not looked for" and "none found" are different claims, and
		 * only one of them is safe to act on. A language with no
		 * dead-code query is named here so a clean table cannot be
		 * mistaken for a clean file (HLR-139, LLR-DED-05). */
		if (!ff->dead_analysed) {
			if (unanalysed_add(&report->dead_unanalysed,
			                   file->language ? file->language
			                                  : "") != 0)
				return -1;
			continue;
		}

		for (size_t d = 0; d < ff->dead_count; d++) {
			const DeadSpan       *span  = &ff->dead[d];
			const FunctionMetric *owner =
				enclosing_function(file, span->start_line);

			/* Resolved by containment rather than by the span's
			 * recorded index, and the distinction is not
			 * pedantry: the index is into the array the parse
			 * produced, and this array has since been sorted into
			 * *presentation* order. Reading the index here would
			 * name the right function only for as long as the two
			 * orders happen to agree. The rule is the same one
			 * the parse applied — the narrowest reported function
			 * containing the span (LLR-DED-04). */
			if (!owner)
				continue;

			if (dead_row_add(report, &capacity, file->path,
			                 owner->name, span) != 0)
				return -1;
		}
	}

	if (report->dead_count > 1)
		qsort(report->dead, report->dead_count, sizeof *report->dead,
		      by_dead_row);
	if (report->dead_unanalysed.count > 1)
		qsort(report->dead_unanalysed.paths,
		      report->dead_unanalysed.count,
		      sizeof *report->dead_unanalysed.paths, by_string);

	return 0;
}

void report_free(Report *report)
{
	if (!report)
		return;

	for (size_t i = 0; i < report->file_count; i++)
		filemetrics_free(report->files[i]);
	free(report->files);
	report->files      = NULL;
	report->file_count = 0;
	routelist_free(&report->routes);
	for (size_t i = 0; i < report->fan_out_count; i++) {
		free(report->fan_out[i].function);
		free(report->fan_out[i].file);
	}
	free(report->fan_out);
	report->fan_out       = NULL;
	report->fan_out_count = 0;
	for (size_t i = 0; i < report->cycle_count; i++) {
		for (size_t m = 0; m < report->cycles[i].count; m++)
			free(report->cycles[i].members[m]);
		free(report->cycles[i].members);
	}
	free(report->cycles);
	report->cycles      = NULL;
	report->cycle_count = 0;
	for (size_t i = 0; i < report->deepest_count; i++) {
		free(report->deepest[i].function);
		free(report->deepest[i].file);
	}
	free(report->deepest);
	report->deepest       = NULL;
	report->deepest_count = 0;
	free(report->languages.items);
	report->languages.items    = NULL;
	report->languages.count    = 0;
	report->languages.capacity = 0;
	free(report->over_threshold.items);
	report->over_threshold.items    = NULL;
	report->over_threshold.count    = 0;
	report->over_threshold.capacity = 0;
	for (size_t i = 0; i < report->global_state_count; i++) {
		free(report->global_state[i].object);
		free(report->global_state[i].writers);
		free(report->global_state[i].readers);
		free(report->global_state[i].participants);
	}
	free(report->global_state);
	report->global_state       = NULL;
	report->global_state_count = 0;
	for (size_t i = 0; i < report->unreachable_count; i++) {
		free(report->unreachable[i].function);
		free(report->unreachable[i].file);
	}
	free(report->unreachable);
	report->unreachable       = NULL;
	report->unreachable_count = 0;
	for (size_t i = 0; i < report->unreachable_global_count; i++)
		free(report->unreachable_globals[i]);
	free(report->unreachable_globals);
	report->unreachable_globals      = NULL;
	report->unreachable_global_count = 0;
	for (size_t i = 0; i < report->cross_scope_count; i++) {
		free(report->cross_scope[i].from_scope);
		free(report->cross_scope[i].from_function);
		free(report->cross_scope[i].to_scope);
		free(report->cross_scope[i].to_function);
		free(report->cross_scope[i].object);
	}
	free(report->cross_scope);
	report->cross_scope       = NULL;
	report->cross_scope_count = 0;
	for (size_t i = 0; i < report->dead_count; i++) {
		free(report->dead[i].file);
		free(report->dead[i].function);
	}
	free(report->dead);
	report->dead       = NULL;
	report->dead_count = 0;
	for (size_t i = 0; i < report->coupling_count; i++) {
		free(report->coupling[i].component);
		free(report->coupling[i].instability);
	}
	free(report->coupling);
	report->coupling       = NULL;
	report->coupling_count = 0;
	for (size_t i = 0; i < report->dep_cycle_count; i++) {
		free(report->dep_cycles[i].components);
		free(report->dep_cycles[i].path);
	}
	free(report->dep_cycles);
	report->dep_cycles      = NULL;
	report->dep_cycle_count = 0;
	for (size_t i = 0; i < report->layering_count; i++) {
		free(report->layering[i].from_stratum);
		free(report->layering[i].from_function);
		free(report->layering[i].from_file);
		free(report->layering[i].to_stratum);
		free(report->layering[i].to_function);
		free(report->layering[i].to_file);
	}
	free(report->layering);
	report->layering       = NULL;
	report->layering_count = 0;
	pathlist_free(&report->dead_unanalysed);
	pathlist_free(&report->skipped_files);
	memset(&report->summary, 0, sizeof report->summary);
}
