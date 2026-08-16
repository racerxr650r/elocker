/* test/unit/format_graph.c — unit tests for src/format_graph.c.
 *
 * The naming and warranted rules are pure decisions over an ElcOptions and a
 * path, and are tested here directly. Whether the emitted GraphML says the
 * right thing about a real project is the `graph/` fixture group's job — that
 * is a question about content, and content is compared against a hand-written
 * expected file rather than against assertions.
 */

#include <criterion/criterion.h>
#include <stdlib.h>
#include <string.h>

#include "elc.h"
#include "format_graph.h"

/* ------------------------------------------------------------- warranted */

Test(format_graph, graphml_is_off_unless_asked_for)
{
	ElcOptions opts = { 0 };

	opts.output_path = "report.md";
	cr_assert_not(graph_graphml_warranted(&opts),
	              "the export is opt-in, unlike the .dot companion "
	              "(HLR-106, LLR-GML-02)");
}

Test(format_graph, graphml_needs_an_output_path)
{
	ElcOptions opts = { 0 };

	opts.graphml     = true;
	opts.output_path = NULL;

	cr_assert_not(graph_graphml_warranted(&opts),
	              "with the report on standard output there is no path to "
	              "derive the companion's name from, so no file is written "
	              "(HLR-104, HLR-106, LLR-GML-03)");
}

Test(format_graph, graphml_is_written_when_asked_for_and_named)
{
	ElcOptions opts = { 0 };

	opts.graphml     = true;
	opts.output_path = "report.md";

	cr_assert(graph_graphml_warranted(&opts));
}

/* ------------------------------------------------------------ the naming */

Test(format_graph, the_companion_replaces_the_extension)
{
	char *p = graph_companion_path("report.md", "graphml");

	cr_assert_not_null(p);
	cr_assert_str_eq(p, "report.graphml",
	                 "substituted, not appended: report.md.graphml would "
	                 "be a second extension rather than a sibling file");
	free(p);
}

Test(format_graph, a_path_without_an_extension_gains_one)
{
	char *p = graph_companion_path("report", "graphml");

	cr_assert_not_null(p);
	cr_assert_str_eq(p, "report.graphml");
	free(p);
}

Test(format_graph, a_dot_in_a_directory_is_not_an_extension)
{
	char *p = graph_companion_path("/a/b.d/report", "graphml");

	cr_assert_not_null(p);
	cr_assert_str_eq(p, "/a/b.d/report.graphml",
	                 "the extension search is scoped to the last path "
	                 "component, or a versioned directory name eats the "
	                 "file name");
	free(p);
}

Test(format_graph, a_directory_dot_with_an_extension_still_substitutes)
{
	char *p = graph_companion_path("/a/b.d/report.md", "graphml");

	cr_assert_not_null(p);
	cr_assert_str_eq(p, "/a/b.d/report.graphml");
	free(p);
}
