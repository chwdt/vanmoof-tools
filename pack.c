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
	fprintf(stderr, "usage: %s <packfile> <warefile> [<warefile> ...]\n", progname);
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
	pack_entry_t *entries;
	size_t offset;
	size_t total;
	ssize_t n, m;
	int i;

	progname = strrchr(argv[0], '/');
	if (progname)
		progname++;
	else
		progname = argv[0];

	if (argc < 3)
		usage();

	packfile = argv[1];

	entries = malloc((argc - 2) * sizeof(pack_entry_t));
	if (entries == NULL) {
		fprintf(stderr, "%s: malloc(%zu): Out of memory\n", progname, (argc - 2) * sizeof(pack_entry_t));
		exit(1);
	}
	memset(entries, 0, (argc - 2) * sizeof(pack_entry_t));

	out = open(packfile, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (out < 0) {
		fprintf(stderr, "%s: open(%s): %s\n", progname, packfile, strerror(errno));
		exit(1);
	}

	memcpy(header.magic, PACK_MAGIC, sizeof(header.magic));
	header.length = htole32((argc - 2) * sizeof(pack_entry_t));

	n = write(out, &header, sizeof(header));
	if (n != sizeof(header)) {
		fprintf(stderr, "%s: write(%zu): %zd\n", progname, sizeof(header), n);
		exit(1);
	}

	offset = sizeof(header);
	for (i = 0; i < argc - 2; i++) {
		fd = open(argv[i + 2], O_RDONLY);
		if (fd < 0) {
			fprintf(stderr, "%s: open(%s): %s\n", progname, argv[i + 2], strerror(errno));
			exit(1);
		}

		if (fstat(fd, &st) < 0) {
			fprintf(stderr, "%s: fstat(%s): %s\n", progname, argv[i + 2], strerror(errno));
			exit(1);
		}

		char *basename = strrchr(argv[i + 2], '/');
		if (basename)
			basename++;
		else
			basename = argv[i + 2];
		strncpy(entries[i].filename, basename, sizeof(entries[i].filename));
		entries[i].offset = htole32(offset);
		entries[i].length = htole32(st.st_size);

		printf("file: %s, offset 0x%08x, length 0x%08x\n", entries[i].filename,
			le32toh(entries[i].offset), le32toh(entries[i].length));

		total = 0;
		while (total < st.st_size) {
			m = st.st_size - total;
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

		close(fd);

		offset += st.st_size;

		switch (offset & 3) {
			case 1:
				write(out, "\0", 1);
				offset++;
			case 2:
				write(out, "\0", 1);
				offset++;
			case 3:
				write(out, "\0", 1);
				offset++;
		}
	}

	header.offset = htole32(offset);

	n = write(out, entries, le32toh(header.length));
	if (n != le32toh(header.length)) {
		fprintf(stderr, "%s: write(%zu): %zd\n", progname, le32toh(header.length), n);
		exit(1);
	}

	n = lseek(out, 0, SEEK_SET);
        if (n != 0) {
		fprintf(stderr, "%s: seek(%u): %zd\n", progname, 0, n);
		exit(1);
	}

	n = write(out, &header, sizeof(header));
	if (n != sizeof(header)) {
		fprintf(stderr, "%s: write(%zu): %zd\n", progname, sizeof(header), n);
		exit(1);
	}

	return 0;
}
