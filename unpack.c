#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include "pack.h"

static char *progname;

static void
usage(void)
{
	fprintf(stderr, "usage: %s <packfile>\n", progname);
	exit(1);
}

int
main(int argc, char **argv)
{
	char buffer[8196];
	char *packfile;
	int fd, out;
	pack_header_t header;
	pack_entry_t entry;
	size_t offset;
	size_t total;
	ssize_t n, m;
	int i;

	progname = strrchr(argv[0], '/');
	if (progname)
		progname++;
	else
		progname = argv[0];

	if (argc < 2)
		usage();

	packfile = argv[1];

	fd = open(packfile, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "%s: open(%s): %s\n", progname, packfile, strerror(errno));
		exit(1);
	}

	n = read(fd, &header, sizeof(header));
	if (n != sizeof(header)) {
		fprintf(stderr, "%s: read(%zu): %zd\n", progname, sizeof(header), n);
		exit(1);
	}

	if (memcmp(header.magic, PACK_MAGIC, sizeof(header.magic))) {
		fprintf(stderr, "%s: %s: not a PACK file\n", progname, packfile);
		exit(1);
	}

	n = lseek(fd, header.offset, SEEK_SET);
	if (n != header.offset) {
		fprintf(stderr, "%s: seek(%u): %zd\n", progname, header.offset, n);
		exit(1);
	}

	offset = header.offset;
	for (i = 0; i < header.length / sizeof(entry); i++) {
		n = lseek(fd, offset, SEEK_SET);
		if (n != offset) {
			fprintf(stderr, "%s: seek(%u): %zd\n", progname, offset, n);
			exit(1);
		}

		n = read(fd, &entry, sizeof(entry));
		if (n != sizeof(entry)) {
			fprintf(stderr, "%s: read(%zu): %zd\n", progname, sizeof(header), n);
			exit(1);
		}

		printf("entry: %s offset %u, length %u\n", entry.filename, entry.offset, entry.length);

		out = open(entry.filename, O_WRONLY|O_CREAT|O_TRUNC, 0666);
		if (out < 0) {
			fprintf(stderr, "%s: open(%s): %s\n", progname, packfile, strerror(errno));
			exit(1);
		}

		n = lseek(fd, entry.offset, SEEK_SET);
		if (n != entry.offset) {
			fprintf(stderr, "%s: seek(%u): %zd\n", progname, entry.offset, n);
			exit(1);
		}

		total = 0;
		while (total < entry.length) {
			m = entry.length - total;
			if (m > sizeof(buffer))
				m = sizeof(buffer);
			n = read(fd, buffer, m);
			if (n != m) {
				fprintf(stderr, "%s: read(%zu): %zd\n", progname, m, n);
				exit(1);
			}
			n = write(out, buffer, m);
			if (n != m) {
				fprintf(stderr, "%s: write(%zu): %zd\n", progname, m, n);
				exit(1);
			}
			total += n;
		}

		close(out);

		offset += sizeof(entry);
	}

	return 0;
}
