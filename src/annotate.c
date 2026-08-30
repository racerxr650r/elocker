/* Placing the findings on the graph they describe (SDD §26).
 *
 * Lifted out of `format_graph.c` when the interactive report needed the same
 * answers. Nothing about *which* finding lands on *which* node is a property
 * of the language a drawing is written in, so it is decided once here and
 * both writers read the result — one opinion about a node's severity rather
 * than one per artefact (HLR-098, HLR-099).
 *
 * What each drawing does with an annotation is still its own: `format_graph.c`
 * turns a severity into a Graphviz fill and `report_html.c` into a stylesheet
 * class, and neither pigment belongs here.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "annotate.h"
#include "thresholds.h"   /* severity_rank: the ranking is judgement, and
                           * judgement belongs to the catalogue, not here */

/* Join one more finding onto a note. Grown rather than fixed because a hub
 * function can carry several, and a truncated tooltip that stops mid-finding
 * is worse than no tooltip at all. */
static int note_append(char **note, const char *text)
{
	size_t have  = *note ? strlen(*note) : 0;
	size_t joint = have ? 2 : 0;   /* the "; " between two findings */
	size_t add   = strlen(text);
	char  *grown = realloc(*note, have + joint + add + 1);

	if (!grown)
		return -1;
	if (joint)
		memcpy(grown + have, "; ", joint);
	memcpy(grown + have + joint, text, add + 1);
	*note = grown;
	return 0;
}

void annotations_free(Annotation *a, size_t count)
{
	if (!a)
		return;
	for (size_t i = 0; i < count; i++)
		free(a[i].note);
	free(a);
}

/* Does this finding name this node? Both halves are required: a file alone is
 * a component finding, and a line alone would match the same line in every
 * file. Together they are the definition site, which is what `thresholds.c`
 * records for a per-function finding. */
static bool finding_is_node(const FindingRow *f, const SdgNode *n)
{
	return f->line != 0 && f->where && f->where[0] &&
	       f->line == n->line_start && strcmp(f->where, n->file) == 0;
}

/* Record a finding against an annotation, keeping the highest severity seen.
 * The severities do not add up; a node carrying a critical and a warning is a
 * critical one (HLR-123). */
static int annotate(Annotation *a, const char *severity, const char *text)
{
	int rank = severity_rank(severity);

	if (rank > a->severity)
		a->severity = rank;
	return note_append(&a->note, text);
}

/* Every node the report names, by definition site.
 *
 * Matching on file and line rather than on name, because a name is not unique
 * — `elc`'s own sources define six functions called `grow` — and a drawing
 * that marked all six because one was unreachable would be worse than one that
 * marked none.
 */
static uint32_t node_at(const Sdg *g, const char *file, uint32_t line)
{
	if (!file)
		return UINT32_MAX;
	for (size_t i = 0; i < g->node_count; i++)
		if (g->nodes[i].line_start == line &&
		    strcmp(g->nodes[i].file, file) == 0)
			return (uint32_t)i;
	return UINT32_MAX;
}

/* The recursive cycles arrive as lists of *names*, which is all the report
 * model carries — a node identifier means nothing to a reader and does not
 * survive a record round trip. So this one match is by name, and inherits the
 * duplicate-`static` imprecision the manual already documents: two functions
 * sharing a name are both marked when one of them recurses. Visible in the
 * drawing, which is better than a silent wrong answer. */
static bool named_in_cycle(const CycleRow *cycle, const char *name)
{
	for (size_t i = 0; i < cycle->count; i++)
		if (strcmp(cycle->members[i], name) == 0)
			return true;
	return false;
}

/* A cycle's members, joined for the tooltip. Bounded, for the reason
 * `thresholds.c` bounds its own: a cycle of two hundred functions is a finding
 * about the cycle, not a place to print two hundred names. */
static void join_names(char *buf, size_t len, const CycleRow *cycle)
{
	size_t at = 0;

	buf[0] = '\0';
	for (size_t i = 0; i < cycle->count; i++) {
		int n = snprintf(buf + at, len - at, "%s%s", at ? ", " : "",
		                 cycle->members[i]);

		if (n < 0 || (size_t)n >= len - at)
			break;
		at += (size_t)n;
	}
}

/* Is this finding one the drawing already puts on every member of a set?
 *
 * The catalogue locates a cycle at a single subject, because a finding has one
 * and a set has no single location. HLR-105 asks for the *members*, so both
 * cycle kinds are drawn from the report's cycle rows instead, and the
 * catalogue's copy would then be a second note saying the same thing on one of
 * the members already carrying the first.
 *
 * Matched on the measurement's name rather than on its rendered text, because
 * both sides of that comparison come from the same catalogue row and cannot
 * drift apart; matching on the text would break the moment a detail was
 * reworded.
 */
static bool drawn_from_a_cycle_row(const FindingRow *f)
{
	const Threshold *recursion = thresholds_lookup(MEASURE_RECURSION);
	const Threshold *dependency = thresholds_lookup(MEASURE_COMPONENT_CYCLE);

	return (recursion && strcmp(f->measurement, recursion->name) == 0) ||
	       (dependency && strcmp(f->measurement, dependency->name) == 0);
}

/* One cycle's note: the catalogue's severity and name, and the members. The
 * severity is looked up rather than restated, so this module still names no
 * threshold of its own. */
static void cycle_note(char *buf, size_t len, const Threshold *t,
                       const char *members)
{
	snprintf(buf, len, "%s: %s (%s)", severity_name(t->fixed), t->name,
	         members);
}

/* Does this comma-separated list of component paths name this one? The group
 * a cycle row carries is rendered text, which is all the report model holds —
 * a component index means nothing to a reader and does not survive a record
 * round trip. */
static bool listed_in_group(const char *group, const char *path)
{
	size_t len = strlen(path);

	/* Whole-member, not substring: `/a/foo.c` is a substring of
	 * `/a/foo.cpp`, and marking the wrong file as a cycle member would be
	 * a false critical finding on a drawing someone circulates. The
	 * separator is ", ", so a member starts the list or follows a space,
	 * and ends the list or precedes a comma. */
	for (const char *p = group; (p = strstr(p, path)) != NULL; p += len)
		if ((p == group || p[-1] == ' ') &&
		    (p[len] == '\0' || p[len] == ','))
			return true;
	return false;
}

/* Place one finding on everything it describes.
 *
 * Returns 1 if it landed somewhere, 0 if it belongs to no node, no component
 * and no global object and so belongs to the graph as a whole, or -1 on
 * failure.
 */
static int place_finding(const Sdg *g, const FindingRow *row, const char *text,
                         Annotation *nodes, Annotation *comps)
{
	bool placed  = false;
	bool touched = false;

	for (size_t i = 0; i < g->node_count; i++) {
		if (!finding_is_node(row, &g->nodes[i]))
			continue;
		if (annotate(&nodes[i], row->severity, text) != 0)
			return -1;
		placed = true;
	}

	for (size_t c = 0; c < g->component_count && !placed; c++) {
		if (strcmp(row->subject, g->component_paths[c]) != 0)
			continue;
		if (annotate(&comps[c], row->severity, text) != 0)
			return -1;
		placed = true;
	}

	/* A finding about a global object names neither a definition site nor a
	 * component, so it is placed on the functions that touch the object —
	 * which is where a reader looking at the drawing would expect to find
	 * it, and the only place it can go on a graph whose nodes are functions
	 * (HLR-091, HLR-105). */
	for (size_t t = 0; !placed && t < g->touch_count; t++) {
		if (g->touches[t].node >= g->node_count ||
		    strcmp(row->subject, g->touches[t].object) != 0)
			continue;
		if (annotate(&nodes[g->touches[t].node], row->severity,
		             text) != 0)
			return -1;
		touched = true;
	}

	return placed || touched;
}

/* Every finding in the catalogue, on whatever it describes. `notes` receives
 * the ones that belong to the graph as a whole — the depth of the call tree is
 * the case that exists — which reach the file as a comment rather than being
 * dropped.
 */
static int place_findings(const Sdg *g, const Report *r, Annotation *nodes,
                          Annotation *comps, char **notes)
{
	char text[768];

	for (size_t f = 0; f < r->finding_count; f++) {
		const FindingRow *row = &r->findings[f];
		int               where;

		if (drawn_from_a_cycle_row(row))
			continue;

		snprintf(text, sizeof text, "%s: %s (%s)", row->severity,
		         row->measurement, row->detail);

		where = place_finding(g, row, text, nodes, comps);
		if (where < 0)
			return -1;
		if (where == 0 && note_append(notes, text) != 0)
			return -1;
	}

	return 0;
}

/* Both kinds of cycle, on every member.
 *
 * From the cycle rows rather than from the findings, because the catalogue
 * locates a cycle at one subject and HLR-105 asks for the members; the
 * severity is still the catalogue's, and only the membership is decided here.
 */
static int mark_cycles(const Sdg *g, const Report *r, Annotation *nodes,
                       Annotation *comps)
{
	char text[768];

	for (size_t i = 0; i < r->dep_cycle_count; i++) {
		const Threshold *t = thresholds_lookup(MEASURE_COMPONENT_CYCLE);

		if (!t)
			break;

		cycle_note(text, sizeof text, t, r->dep_cycles[i].path);
		for (size_t c = 0; c < g->component_count; c++) {
			if (!listed_in_group(r->dep_cycles[i].components,
			                     g->component_paths[c]))
				continue;
			if (annotate(&comps[c], severity_name(t->fixed),
			             text) != 0)
				return -1;
		}
	}

	for (size_t i = 0; i < r->cycle_count; i++) {
		const Threshold *t = thresholds_lookup(MEASURE_RECURSION);
		char             members[512];

		if (!t)
			break;

		join_names(members, sizeof members, &r->cycles[i]);
		cycle_note(text, sizeof text, t, members);
		for (size_t n = 0; n < g->node_count; n++) {
			if (!named_in_cycle(&r->cycles[i], g->nodes[n].name))
				continue;
			nodes[n].marks |= MARK_RECURSIVE;
			if (annotate(&nodes[n], severity_name(t->fixed),
			             text) != 0)
				return -1;
		}
	}

	return 0;
}

/* The structural marks, each from the collection that owns it. None of them
 * can fail: a mark is a bit on an annotation that already exists.
 */
static void mark_structure(const Sdg *g, const Report *r, Annotation *nodes)
{
	for (size_t i = 0; i < r->unreachable_count; i++) {
		uint32_t n = node_at(g, r->unreachable[i].file,
		                     r->unreachable[i].line);

		if (n != UINT32_MAX)
			nodes[n].marks |= MARK_UNREACHABLE;
	}

	for (size_t i = 0; i < r->deepest_count; i++) {
		uint32_t n = node_at(g, r->deepest[i].file, r->deepest[i].line);

		if (n != UINT32_MAX)
			nodes[n].marks |= MARK_DEEPEST;
	}

	/* The globals are marked from the *touch* set rather than from the
	 * edges, for the reason graph.h gives: an object named by exactly one
	 * function produces no edge at all, and that object is precisely the
	 * scope-reduction candidate (HLR-092). */
	for (size_t s = 0; s < r->global_state_count; s++) {
		const GlobalStateRow *row = &r->global_state[s];
		unsigned              mark;

		if (row->verdict == GLOBAL_HIDDEN_CHANNEL)
			mark = MARK_HIDDEN;
		else if (row->verdict == GLOBAL_SCOPE_REDUCTION)
			mark = MARK_SOLE_USER;
		else
			continue;

		for (size_t t = 0; t < g->touch_count; t++)
			if (g->touches[t].node < g->node_count &&
			    strcmp(g->touches[t].object, row->object) == 0)
				nodes[g->touches[t].node].marks |= mark;
	}
}

/* Everything the drawing knows, gathered in one pass so that emission is a
 * pure walk.
 *
 * The findings come first, so that the severity a node is filled with is the
 * catalogue's and the marks after it only add shape.
 */
static int collect(const Sdg *g, const Report *r, Annotation *nodes,
                   Annotation *comps, char **notes)
{
	if (place_findings(g, r, nodes, comps, notes) != 0)
		return -1;
	if (mark_cycles(g, r, nodes, comps) != 0)
		return -1;
	mark_structure(g, r, nodes);
	return 0;
}

/* Is this edge a step of the deepest chain? The chain is a list of definition
 * sites, so the step is a consecutive pair of them (HLR-088). */
bool annotation_on_chain(const uint32_t *chain, size_t count, uint32_t from,
                     uint32_t to)
{
	for (size_t i = 0; i + 1 < count; i++)
		if (chain[i] == from && chain[i + 1] == to)
			return true;
	return false;
}

/* Everything the writer needs that is derived rather than written: the node
 * and component annotations, the note block naming the conventions in use, and
 * the deepest chain as node identifiers.
 *
 * Returns 0 with all four published, or -1. The caller diagnoses, naming the
 * artefact it was writing; on failure its teardown releases whatever was
 * allocated, so each output is assigned as soon as it exists.
 */
int annotations_build(const Sdg *g, const Report *r, Annotation **nodes,
                      Annotation **comps, char **notes, uint32_t **chain)
{
	*nodes = calloc(g->node_count ? g->node_count : 1, sizeof **nodes);
	*comps = calloc(g->component_count ? g->component_count : 1,
	                sizeof **comps);
	*chain = calloc(r->deepest_count ? r->deepest_count : 1,
	                sizeof **chain);

	if (!*nodes || !*comps || !*chain ||
	    collect(g, r, *nodes, *comps, notes) != 0)
		return -1;

	for (size_t i = 0; i < r->deepest_count; i++)
		(*chain)[i] = node_at(g, r->deepest[i].file,
		                      r->deepest[i].line);

	return 0;
}
