/* analyze.c — per-file measurement.
 *
 * The single parse lives here from Phase 2. Phase 1 builds the part of it
 * that needs no grammar: map the file read-only and count its physical lines
 * (doc/SDD.md §7.3.2 steps 2, 3, and 10; LLR-ANL-02, LLR-ANL-04, LLR-ANL-06).
 *
 * The file is opened O_RDONLY and mapped PROT_READ. No module in src/ ever
 * opens a source file for writing, which is what makes HLR-043 a property of
 * the design rather than a rule someone has to remember.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "analyze.h"
#include "elc.h"

void filemetrics_free(FileMetrics *metrics)
{
	if (!metrics)
		return;
	free(metrics->path);
	free(metrics);
}

/* Count the lines in a mapping.
 *
 * A trailing fragment with no final newline is a line a reader sees, so it
 * counts. The mapping is not NUL-terminated, so every scan is bounded by the
 * length from fstat(2) rather than by a terminator.
 */
static uint32_t count_lines(const char *data, size_t len)
{
	size_t lines = 0;

	for (const char *p = data, *end = data + len; p < end; ) {
		const char *nl = memchr(p, '\n', (size_t)(end - p));

		if (!nl) {
			lines++;         /* final line, unterminated */
			break;
		}
		lines++;
		p = nl + 1;
	}

	return lines > UINT32_MAX ? UINT32_MAX : (uint32_t)lines;
}

int analyze_file(const char *path, FileMetrics **out)
{
	FileMetrics *metrics = NULL;
	struct stat  st;
	int          fd     = -1;
	int          status = -1;

	*out = NULL;

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "elc: %s: %s\n", path, strerror(errno));
		goto cleanup;
	}

	if (fstat(fd, &st) != 0) {
		fprintf(stderr, "elc: %s: %s\n", path, strerror(errno));
		goto cleanup;
	}

	metrics = calloc(1, sizeof *metrics);
	if (!metrics) {
		fprintf(stderr, "elc: out of memory measuring %s\n", path);
		goto cleanup;
	}

	metrics->path = strdup(path);
	if (!metrics->path) {
		fprintf(stderr, "elc: out of memory measuring %s\n", path);
		goto cleanup;
	}

	if (st.st_size > 0) {
		size_t len = (size_t)st.st_size;
		void  *map = mmap(NULL, len, PROT_READ, MAP_PRIVATE, fd, 0);

		/* A zero-length file is short-circuited above rather than
		 * mapped: mmap of an empty file fails with EINVAL, and an empty
		 * file is not an error (LLR-ANL-04). */
		if (map == MAP_FAILED) {
			fprintf(stderr, "elc: %s: %s\n", path, strerror(errno));
			goto cleanup;
		}

		metrics->physical_lines = count_lines(map, len);
		munmap(map, len);
	}

	*out    = metrics;
	metrics = NULL;
	status  = 0;

cleanup:
	filemetrics_free(metrics);
	if (fd >= 0)
		close(fd);
	return status;
}
