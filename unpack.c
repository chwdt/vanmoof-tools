#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>

#include <endian.h>

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
	struct stat st;
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

	if (fstat(fd, &st) < 0) {
		fprintf(stderr, "%s: fstat(%s): %s\n", progname, packfile, strerror(errno));
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

	if (le32toh(header.offset) + le32toh(header.length) > st.st_size) {
		fprintf(stderr, "%s: WARNING: PACK offset 0x%08zx + length 0x%08x is beyond end of file 0x%08zx\n",
			progname, offset, le32toh(header.length), st.st_size);
		exit(1);
	}

	offset = le32toh(header.offset);
	for (i = 0; i < le32toh(header.length) / sizeof(entry); i++) {
		n = lseek(fd, offset, SEEK_SET);
		if (n != offset) {
			fprintf(stderr, "%s: seek(%u): %zd\n", progname, offset, n);
			exit(1);
		}

		n = read(fd, &entry, sizeof(entry));
		if (n != sizeof(entry)) {
			fprintf(stderr, "%s: read(%zu): %zd\n", progname, sizeof(entry), n);
			exit(1);
		}

		if (le32toh(entry.offset) + le32toh(entry.length) > le32toh(header.offset)) {
			fprintf(stderr, "%s: file %s offset 0x%08zx + length 0x%08x is beyond start of PACK directoy 0x%08zx\n",
				progname, entry.filename, le32toh(entry.offset), le32toh(entry.length), le32toh(header.offset));
			exit(1);
		}

		printf("file: %s, offset 0x%08x, length 0x%08x\n", entry.filename,
			le32toh(entry.offset), le32toh(entry.length));

		out = open(entry.filename, O_WRONLY|O_CREAT|O_TRUNC, 0666);
		if (out < 0) {
			fprintf(stderr, "%s: open(%s): %s\n", progname, packfile, strerror(errno));
			exit(1);
		}

		n = lseek(fd, le32toh(entry.offset), SEEK_SET);
		if (n != le32toh(entry.offset)) {
			fprintf(stderr, "%s: seek(%u): %zd\n", progname, le32toh(entry.offset), n);
			exit(1);
		}

		total = 0;
		while (total < le32toh(entry.length)) {
			m = le32toh(entry.length) - total;
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
