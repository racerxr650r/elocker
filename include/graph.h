/* graph.h — the System Dependence Graph.
 *
 * graph.c resolves the per-file facts the single parse produced into one
 * project-wide directed graph, and owns that graph for the rest of the run
 * (doc/SDD.md §8). Nothing here reopens a source file: the facts are all
 * there is, which is what HLR-076 asks for structurally rather than by
 * convention.
 *
 * The graph is **simple**. Repeated calls from one function to the same
 * callee collapse to one edge carrying a call-site count, so a function's
 * out-degree is the number of *distinct* subroutines it invokes (HLR-085).
 * A multigraph would report a function that calls one helper in a loop body
 * and again in its error path as having a fan-out of two.
 */
#ifndef ELC_GRAPH_H
#define ELC_GRAPH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "elc.h"
#include "report.h"

/* What an edge represents. The two kinds are carried separately and never
 * merged: coupling through shared state is a different fact about a design
 * from coupling through calls, and an analysis that could not tell them apart
 * would report one as the other (HLR-074). */
typedef enum {
	EDGE_CALL = 0,
	EDGE_GLOBAL
} SdgEdgeKind;

/* One node: a function, and where it came from.
 *
 * Every string is borrowed from the report model, which outlives the graph.
 * Copying them would duplicate every function name in the project to no end,
 * and the lifetime is not in question — main() releases the graph before the
 * report.
 */
typedef struct {
	const char *name;        /* borrowed from the report model      */
	const char *file;        /* the defining file, borrowed         */
	size_t      component;   /* index of that file; the component
	                          * a function belongs to (HLR-114)     */
	uint32_t    line_start;
	uint32_t    line_end;
	uint32_t    eloc;
	uint32_t    complexity;
	bool        address_taken; /* a reachability root (HLR-096)     */
} SdgNode;

typedef struct {
	uint32_t     from;
	uint32_t     to;
	SdgEdgeKind  kind;
	uint32_t     call_sites;  /* the collapsed count; 0 for a global edge */
	const char  *global;      /* the object's name for a global edge,
	                           * into the graph's own name table         */
} SdgEdge;

/* One call site that resolved to nothing — an external library, a system
 * call, or a call through a pointer. Retained rather than discarded so the
 * reader can judge the graph's completeness (HLR-077). */
typedef struct {
	const char *callee;   /* into the graph's own name table            */
	const char *file;     /* borrowed from the report model             */
	uint32_t    line;
} UnresolvedCall;

/* One function's access to one global object.
 *
 * Carried **beside** the global edges, not derived from them, and the reason
 * is the case the requirement is most interested in. A global edge joins a
 * writer to a reader, so an object touched by exactly one function produces no
 * edge at all — and that object is precisely the scope-reduction candidate of
 * HLR-092. An analysis reading only the edge table would find none of them,
 * and would find no object that is written but never read either.
 *
 * The set is what HLR-091 asks for in its own right: the functions that write
 * each object and the functions that read it.
 */
typedef struct {
	const char *object; /* into the graph's own name table  */
	uint32_t    node;   /* the accessing function           */
	bool        write;  /* true writes the object, false reads it */
} GlobalTouch;

/* An edge between two components, the file-level projection arch.c consumes
 * (HLR-114). Simple, like the function graph. */
typedef struct {
	size_t from;
	size_t to;
} ComponentEdge;

typedef struct {
	void          *graph;       /* igraph_t *; opaque here so that no
	                             * consumer of the SDG links the graph
	                             * library merely to read a node table  */
	void          *call_graph;  /* igraph_t * over the call edges alone.
	                             *
	                             * A second view rather than a filter at
	                             * each use, because the distinction is
	                             * not a detail: a global edge from a
	                             * writer to a reader is not a call. A
	                             * cycle through one is not recursion, and
	                             * a chain through one is not a call
	                             * chain — measuring either over the full
	                             * graph would report a program as
	                             * recursive because two of its functions
	                             * touch the same variable (HLR-089).
	                             *
	                             * Node identifiers are the same in both,
	                             * so a result from one indexes the node
	                             * table directly.                      */
	void          *component_graph; /* igraph_t * over the component
	                                 * projection (HLR-114).
	                                 *
	                                 * The third view, and the one the
	                                 * *architectural* questions are asked
	                                 * of. A cycle here means two files
	                                 * depend on each other; a cycle in the
	                                 * call view means two functions call
	                                 * each other, and the two are
	                                 * different facts about a design. Two
	                                 * mutually recursive functions in one
	                                 * file close a loop in the call view
	                                 * and none at all here — which is
	                                 * exactly what HLR-083 asks for, and
	                                 * is why this is a view rather than a
	                                 * filter applied at each use.
	                                 *
	                                 * Vertices are component indices, so a
	                                 * result indexes component_paths
	                                 * directly.                          */
	SdgNode       *nodes;
	size_t         node_count;
	SdgEdge       *edges;
	size_t         edge_count;
	size_t         edge_capacity;
	char         **global_names;    /* owned; the object names the edges
	                                 * point into. Owned rather than
	                                 * borrowed because the fact list is
	                                 * released as soon as the graph is
	                                 * built, and an edge outliving the
	                                 * string that named it is a
	                                 * use-after-free that renders as
	                                 * plausible garbage                */
	size_t         global_name_count;
	GlobalTouch   *touches;         /* sorted by object then node, and
	                                 * de-duplicated; owned (HLR-091)   */
	size_t         touch_count;
	size_t         touch_capacity;
	char         **component_paths; /* borrowed from the report model   */
	size_t         component_count;
	ComponentEdge *component_edges;
	size_t         component_edge_count;
	size_t         component_edge_capacity;
	UnresolvedCall *unresolved_sites;
	size_t          unresolved;      /* HLR-077                        */
	size_t          unresolved_capacity;
	char          **unresolved_names; /* owned; the strings the sites
	                                   * above point into              */
	size_t          unresolved_name_count;
} Sdg;

/* Construct the SDG from the accumulated facts and the assembled report.
 *
 * The report rather than the raw file list, because the graph's nodes carry
 * the per-function metrics GraphML exports and because the report's file
 * order *is* the sorted order the stable node identifiers depend on
 * (LLR-SDG-09). Facts are matched to files by path.
 *
 * Returns 0 on success, non-zero only on allocation failure. An unresolvable
 * call site is not a failure (HLR-077).
 */
int graph_build(const FactList *facts, const Report *report, Sdg *out);

/* The number of call sites with no resolvable target (HLR-077). */
size_t graph_unresolved_count(const Sdg *g);

/* Release the graph, node table, edge table and projection. Safe on NULL. */
void graph_free(Sdg *g);

#endif /* ELC_GRAPH_H */
