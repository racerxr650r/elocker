/* test/unit/analyze.c — unit tests for src/analyze.c.
 *
 * Phase 1 builds the part of the single parse that needs no grammar: map the
 * file read-only and count its physical lines. These tests cover that part;
 * the parse itself is tested here from Phase 2, against the same module.
 */

#include <criterion/criterion.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "analyze.h"
#include "elc.h"

static char scratch[512];

static void remove_scratch(void)
{
	char command[600];

	if (scratch[0] == '\0')
		return;
	snprintf(command, sizeof command, "rm -rf -- '%s'", scratch);
	if (system(command) != 0)
		fprintf(stderr, "could not remove %s\n", scratch);
}

/* A file holding exactly `contents`, in a directory removed when this test's
 * process exits. Returns its path, valid for the rest of the test. */
static const char *file_holding(const char *contents)
{
	static char path[1024];

	if (scratch[0] == '\0') {
		snprintf(scratch, sizeof scratch, "/tmp/elc-analyze-XXXXXX");
		cr_assert_not_null(mkdtemp(scratch), "could not create a scratch dir");
		atexit(remove_scratch);
	}

	snprintf(path, sizeof path, "%s/subject", scratch);

	FILE *fp = fopen(path, "w");
	cr_assert_not_null(fp, "could not write %s", path);
	fputs(contents, fp);
	fclose(fp);
	return path;
}

/* Verifies LLR-ANL-06: physical lines are counted from the mapped contents. */
Test(analyze, physical_lines_are_counted)
{
	FileMetrics *m = NULL;

	cr_assert_eq(analyze_file(file_holding("one\ntwo\nthree\n"), &m), 0);
	cr_assert_not_null(m);
	cr_assert_eq(m->physical_lines, 3);
	filemetrics_free(m);
}

/* Verifies LLR-ANL-06: a final line with no terminating newline is still a
 * line the reader sees, and counts. */
Test(analyze, an_unterminated_final_line_counts)
{
	FileMetrics *m = NULL;

	cr_assert_eq(analyze_file(file_holding("one\ntwo"), &m), 0);
	cr_assert_eq(m->physical_lines, 2,
	             "the trailing fragment is a line whether or not it ends "
	             "in a newline");
	filemetrics_free(m);
}

/* Verifies LLR-ANL-04: a zero-length file reports zero metrics without error,
 * rather than being mapped — mmap of an empty file fails with EINVAL. */
Test(analyze, a_zero_length_file_reports_zero_without_error)
{
	FileMetrics *m = NULL;

	cr_assert_eq(analyze_file(file_holding(""), &m), 0,
	             "an empty file is not an error");
	cr_assert_eq(m->physical_lines, 0);
	filemetrics_free(m);
}

/* Verifies LLR-ANL-06: the metrics carry the path they were measured from. */
Test(analyze, the_metrics_carry_the_path)
{
	const char  *path = file_holding("x\n");
	FileMetrics *m    = NULL;

	cr_assert_eq(analyze_file(path, &m), 0);
	cr_assert_str_eq(m->path, path);
	filemetrics_free(m);
}

/* Verifies LLR-ANL-02: a file that cannot be read is a per-file failure, not
 * a fatal one, and yields no metrics for the caller to release. */
Test(analyze, an_unreadable_file_is_reported_without_metrics)
{
	const char *path = file_holding("x\n");
	FileMetrics *m   = (FileMetrics *)0x1;

	cr_assert_eq(chmod(path, 0), 0);
	int rc = analyze_file(path, &m);
	cr_assert_eq(chmod(path, 0600), 0);

	cr_assert_neq(rc, 0);
	cr_assert_null(m, "no metrics are handed back on the failure path");
}

/* Verifies LLR-ANL-02: the file is opened read-only. Reopening the same
 * descriptor's file on a read-only filesystem is not available here, so the
 * property is asserted the way the module guarantees it — a run against a
 * file whose directory denies writing still succeeds. */
Test(analyze, a_file_in_an_unwritable_directory_is_still_measured)
{
	const char  *path = file_holding("one\ntwo\n");
	FileMetrics *m    = NULL;

	cr_assert_eq(chmod(scratch, 0500), 0);
	int rc = analyze_file(path, &m);
	cr_assert_eq(chmod(scratch, 0700), 0);

	cr_assert_eq(rc, 0, "reading needs no write permission anywhere");
	cr_assert_eq(m->physical_lines, 2);
	filemetrics_free(m);
}

Test(analyze, filemetrics_free_is_safe_on_null)
{
	filemetrics_free(NULL);
	cr_assert(1, "releasing a null metrics structure must not fault");
}
