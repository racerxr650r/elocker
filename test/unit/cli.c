/* test/unit/cli.c — unit tests for src/cli.c.
 *
 * One Criterion binary per src/ module, linked against that module. Tests
 * register automatically, so a test cannot be written and silently never run
 * (doc/STP.md §2.2).
 *
 * Suite names match the module they exercise, so the suite, the module, and
 * the LLR prefix line up when a failure is read.
 */

#include <criterion/criterion.h>
#include <criterion/redirect.h>
#include <stdlib.h>

#include "cli.h"
#include "elc.h"

/* --------------------------------------------------------------- --wrap ---
 *
 * Phase 0 proves the link-time interception the whole unit level rests on
 * (STP §2.2), before anything depends on it. A wrapped allocator is the
 * mechanism by which the checked-growth contracts of later phases —
 * LLR-ANL-34, LLR-RPT-16 — become testable at all: allocation failure cannot
 * otherwise be provoked from a test.
 */
extern void *__real_malloc(size_t);

/* `volatile` is load-bearing, not decoration. The compiler treats malloc as a
 * builtin that cannot read the caller's globals, so in
 *
 *     armed = 1;  p = malloc(n);  armed = 0;
 *
 * it considers the first store dead — overwritten by the second before
 * anything could observe it — and removes it. The wrapper then runs with the
 * flag still clear and the interception silently does not happen. Every
 * arm/disarm flag guarding a wrapped function must be volatile for the same
 * reason (doc/STP.md §2.2). */
static volatile int malloc_should_fail;

void *__wrap_malloc(size_t n)
{
	if (malloc_should_fail)
		return NULL;
	return __real_malloc(n);
}

Test(cli, wrap_passes_through_when_not_armed)
{
	malloc_should_fail = 0;
	void *p = malloc(32);
	cr_assert_not_null(p, "unarmed wrap must delegate to the real malloc");
	free(p);
}

Test(cli, wrap_intercepts_when_armed)
{
	malloc_should_fail = 1;
	void *p = malloc(32);
	malloc_should_fail = 0;
	cr_assert_null(p, "armed wrap must intercept and fail the allocation");
}

/* ------------------------------------------------------------- cli_parse ---
 *
 * getopt_long keeps global state across calls, so each test resets optind
 * via cli_parse() itself; Criterion's per-test process isolation means no
 * test can leak that state into another regardless.
 */

Test(cli, help_short_option_reports_help)
{
	char *argv[] = { "elc", "-h", NULL };
	ElcOptions o;
	cr_assert_eq(cli_parse(2, argv, &o), CLI_HELP);
}

Test(cli, help_long_option_reports_help)
{
	char *argv[] = { "elc", "--help", NULL };
	ElcOptions o;
	cr_assert_eq(cli_parse(2, argv, &o), CLI_HELP);
}

Test(cli, unrecognised_option_is_a_usage_error)
{
	char *argv[] = { "elc", "--bogus", NULL };
	ElcOptions o;
	cr_assert_eq(cli_parse(2, argv, &o), CLI_ERROR,
	             "an unrecognised option must be rejected (HLR-063)");
}

Test(cli, missing_target_is_a_usage_error)
{
	char *argv[] = { "elc", NULL };
	ElcOptions o;
	cr_assert_eq(cli_parse(1, argv, &o), CLI_ERROR,
	             "an invocation with no target must be rejected (HLR-063)");
}

Test(cli, single_target_is_collected)
{
	char *argv[] = { "elc", "a.c", NULL };
	ElcOptions o;
	cr_assert_eq(cli_parse(2, argv, &o), CLI_OK);
	cr_assert_eq(o.target_count, 1);
	cr_assert_str_eq(o.targets[0], "a.c");
	cli_options_free(&o);
}

Test(cli, several_targets_are_collected_in_order)
{
	char *argv[] = { "elc", "a.c", "src/", "b.c", NULL };
	ElcOptions o;
	cr_assert_eq(cli_parse(4, argv, &o), CLI_OK,
	             "files and directories may be intermixed (HLR-071)");
	cr_assert_eq(o.target_count, 3);
	cr_assert_str_eq(o.targets[0], "a.c");
	cr_assert_str_eq(o.targets[1], "src/");
	cr_assert_str_eq(o.targets[2], "b.c");
	cli_options_free(&o);
}

Test(cli, help_takes_precedence_over_a_target)
{
	char *argv[] = { "elc", "--help", "a.c", NULL };
	ElcOptions o;
	cr_assert_eq(cli_parse(3, argv, &o), CLI_HELP,
	             "help is reported without validating other arguments");
}

Test(cli, mode_defaults_to_analyse)
{
	char *argv[] = { "elc", "a.c", NULL };
	ElcOptions o;
	cr_assert_eq(cli_parse(2, argv, &o), CLI_OK);
	cr_assert_eq(o.mode, MODE_ANALYSE);
	cli_options_free(&o);
}

Test(cli, the_output_destination_defaults_to_standard_output)
{
	char *argv[] = { "elc", "a.c", NULL };
	ElcOptions o;
	cr_assert_eq(cli_parse(2, argv, &o), CLI_OK);
	cr_assert_null(o.output_path,
	               "a null output path records standard output as the "
	               "destination (HLR-030)");
	cli_options_free(&o);
}

Test(cli, an_output_path_is_collected)
{
	char *argv[] = { "elc", "--output", "report.txt", "a.c", NULL };
	ElcOptions o;
	cr_assert_eq(cli_parse(4, argv, &o), CLI_OK);
	cr_assert_str_eq(o.output_path, "report.txt");
	cr_assert_eq(o.target_count, 1,
	             "the option's argument is not mistaken for a target");
	cli_options_free(&o);
}

Test(cli, the_short_output_option_behaves_as_the_long_one)
{
	char *argv[] = { "elc", "-o", "report.txt", "a.c", NULL };
	ElcOptions o;
	cr_assert_eq(cli_parse(4, argv, &o), CLI_OK);
	cr_assert_str_eq(o.output_path, "report.txt");
	cli_options_free(&o);
}

Test(cli, an_output_option_without_its_argument_is_a_usage_error)
{
	char *argv[] = { "elc", "-o", NULL };
	ElcOptions o;
	cr_assert_eq(cli_parse(2, argv, &o), CLI_ERROR,
	             "an option requiring an argument must be given one "
	             "(HLR-063)");
}

Test(cli, the_complexity_threshold_defaults_to_fifteen)
{
	char *argv[] = { "elc", "a.c", NULL };
	ElcOptions o;
	cr_assert_eq(cli_parse(2, argv, &o), CLI_OK);
	cr_assert_eq(o.complexity_threshold, ELC_DEFAULT_COMPLEXITY_THRESHOLD);
	cr_assert_eq(o.complexity_threshold, 15,
	             "the documented default is 15 (HLR-022)");
	cli_options_free(&o);
}

Test(cli, a_complexity_threshold_is_collected)
{
	char *argv[] = { "elc", "--complexity-threshold", "7", "a.c", NULL };
	ElcOptions o;
	cr_assert_eq(cli_parse(4, argv, &o), CLI_OK);
	cr_assert_eq(o.complexity_threshold, 7);
	cr_assert_eq(o.target_count, 1);
	cli_options_free(&o);
}

Test(cli, the_short_threshold_option_behaves_as_the_long_one)
{
	char *argv[] = { "elc", "-c", "7", "a.c", NULL };
	ElcOptions o;
	cr_assert_eq(cli_parse(4, argv, &o), CLI_OK);
	cr_assert_eq(o.complexity_threshold, 7);
	cli_options_free(&o);
}

Test(cli, a_threshold_of_zero_is_accepted)
{
	/* Zero lists every function, which is a legitimate thing to ask for
	 * and must not be confused with "unset". */
	char *argv[] = { "elc", "-c", "0", "a.c", NULL };
	ElcOptions o;
	cr_assert_eq(cli_parse(4, argv, &o), CLI_OK);
	cr_assert_eq(o.complexity_threshold, 0);
	cli_options_free(&o);
}

Test(cli, a_malformed_threshold_is_a_usage_error)
{
	/* strtoul would accept every one of these: a sign, a trailing tail,
	 * leading whitespace, and an empty string all parse to something. */
	const char *bad[] = { "abc", "7x", "-1", " 7", "", "0x10" };

	for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
		char      *argv[] = { "elc", "-c", (char *)bad[i], "a.c", NULL };
		ElcOptions o;

		cr_assert_eq(cli_parse(4, argv, &o), CLI_ERROR,
		             "'%s' is not a threshold", bad[i]);
	}
}

Test(cli, options_free_is_safe_on_null)
{
	cli_options_free(NULL);
	cr_assert(1, "releasing a null options structure must not fault");
}
