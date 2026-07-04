#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>

#include <endian.h>

#include <openssl/sha.h>
#include <openssl/asn1.h>

#include "pack.h"
#include "ware.h"

static char *progname;
static int human = 0;

static void
usage(void)
{
	fprintf(stderr, "usage: %s [-l] [-h] <packfile>\n", progname);
	exit(1);
}

static void
format_size(uint32_t bytes, char *buf, size_t bufsize)
{
	if (!human) {
		snprintf(buf, bufsize, "0x%08x", bytes);
		return;
	}
	if (bytes >= 1024U * 1024U) {
		snprintf(buf, bufsize, "%.2f MiB", bytes / (1024.0 * 1024.0));
	} else if (bytes >= 1024U) {
		snprintf(buf, bufsize, "%.2f KiB", bytes / 1024.0);
	} else {
		snprintf(buf, bufsize, "%u B", bytes);
	}
}

static int
parse_signature(int fd, size_t sig_offset, size_t sig_length)
{
	char buffer[8196];
	image_tlv_t tlv;
	uint8_t value[256];
	uint8_t sha[SHA256_DIGEST_LENGTH];
	SHA256_CTX sha_ctx;
	size_t offset;
	size_t remaining;
	ssize_t n;

	if (sig_length < sizeof(image_tlv_t))
		return 0;

	if (lseek(fd, sig_offset, SEEK_SET) != (off_t)sig_offset)
		return 0;

	n = read(fd, &tlv, sizeof(tlv));
	if (n != sizeof(tlv))
		return 0;

	if (tlv.type != IMAGE_TLV_INFO_MAGIC || tlv.length != sig_length)
		return 0;

	printf("%s: Vanmoof signature: Offset 0x%zx, Magic 0x%x, Length 0x%x\n",
		progname, sig_offset, tlv.type, tlv.length);

	SHA256_Init(&sha_ctx);
	if (lseek(fd, 0, SEEK_SET) != 0)
		return 1;
	remaining = sig_offset;
	while (remaining > 0) {
		size_t to_read = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
		n = read(fd, buffer, to_read);
		if (n <= 0)
			return 1;
		SHA256_Update(&sha_ctx, buffer, n);
		remaining -= n;
	}
	SHA256_Final(sha, &sha_ctx);

	offset = sizeof(image_tlv_t);
	while (offset < sig_length) {
		if (lseek(fd, sig_offset + offset, SEEK_SET) != (off_t)(sig_offset + offset))
			return 1;
		n = read(fd, &tlv, sizeof(tlv));
		if (n != sizeof(tlv))
			return 1;
		offset += sizeof(tlv);

		if (tlv.length > sizeof(value))
			return 1;
		n = read(fd, value, tlv.length);
		if (n != tlv.length)
			return 1;

		switch (tlv.type) {
			case IMAGE_TLV_SHA256:
				printf("%s: Vanmoof signature: SHA256 at 0x%zx, Length 0x%x: %s\n",
					progname, sig_offset + offset, tlv.length,
					(tlv.length == SHA256_DIGEST_LENGTH && memcmp(sha, value, tlv.length) == 0) ? "OK" : "FAIL");
				break;
			case IMAGE_TLV_KEYHASH:
				printf("%s: Vanmoof signature: KEYHASH at 0x%zx, Length 0x%x\n",
					progname, sig_offset + offset, tlv.length);
				break;
			case IMAGE_TLV_ECDSA_SIG: {
				BIO *bio = BIO_new_fd(fileno(stdout), BIO_NOCLOSE);
				printf("%s: Vanmoof signature: ECDSA_SIG at 0x%zx, Length 0x%x\n",
					progname, sig_offset + offset, tlv.length);
				fflush(stdout);
				ASN1_parse_dump(bio, value, tlv.length, 0, -1);
				BIO_free(bio);
				break;
			}
			default:
				printf("%s: Vanmoof signature: Type 0x%04x at 0x%zx, Length 0x%x\n",
					progname, tlv.type, sig_offset + offset, tlv.length);
				break;
		}
		offset += tlv.length;
	}
	return 1;
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
	size_t pack_start = 0;
	size_t offset;
	size_t total;
	ssize_t n, m;
	int i;
	int list_only = 0;
	int signature_parsed = 0;

	progname = strrchr(argv[0], '/');
	if (progname)
		progname++;
	else
		progname = argv[0];

	while (argc > 1 && argv[1][0] == '-') {
		if (strcmp(argv[1], "-l") == 0) {
			list_only = 1;
			argc--;
			argv++;
		} else if (strcmp(argv[1], "-h") == 0) {
			human = 1;
			argc--;
			argv++;
		} else if (strcmp(argv[1], "-d") == 0) {
			if (argc < 3)
				usage();
			outdir = argv[2];
			argc -= 2;
			argv += 2;
		} else if (strcmp(argv[1], "--") == 0) {
			argc--;
			argv++;
			break;
		} else {
			usage();
		}
	}

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

	if (outdir && !list_only) {
		if (mkdir(outdir, 0777) < 0 && errno != EEXIST) {
			fprintf(stderr, "%s: mkdir(%s): %s\n", progname, outdir, strerror(errno));
			exit(1);
		}
		if (chdir(outdir) < 0) {
			fprintf(stderr, "%s: chdir(%s): %s\n", progname, outdir, strerror(errno));
			exit(1);
		}
	}

retry:
	n = read(fd, &header, sizeof(header));
	if (n != sizeof(header)) {
		fprintf(stderr, "%s: read(%zu): %zd\n", progname, sizeof(header), n);
		exit(1);
	}

	if (memcmp(header.magic, PACK_MAGIC, sizeof(header.magic))) {
		uint32_t *magic = (uint32_t *)&header.magic;
		if (le32toh(*magic) == HEAD_MAGIC) {
			vanmoof_head_t head;
			n = lseek(fd, 0, SEEK_SET);
			if (n != 0) {
				fprintf(stderr, "%s: seek(%u): %zd\n", progname, offset, n);
				exit(1);
			}
			n = read(fd, &head, sizeof(head));
			if (n != sizeof(head)) {
				fprintf(stderr, "%s: read(%zu): %zd\n", progname, sizeof(head), n);
				exit(1);
			}
			pack_start = le32toh(head.offset);
			n = lseek(fd, pack_start, SEEK_SET);
			if (n != pack_start) {
				fprintf(stderr, "%s: seek(%u): %zd\n", progname, offset, n);
				exit(1);
			}
			{
				char len_buf[32];
				format_size(le32toh(head.length), len_buf, sizeof(len_buf));
				printf("%s: Vanmoof software: Version %d.%d.%d.%d, Offset 0x%x, Length %s\n",
					progname, (le32toh(head.version0) >> 0) & 0xff, (le32toh(head.version0) >> 8) & 0xff,
					(le32toh(head.version0) >> 16) & 0xff, le32toh(head.version1),
					le32toh(head.offset), len_buf);
			}
			if (pack_start + le32toh(head.length) < st.st_size) {
				size_t sig_offset = pack_start + le32toh(head.length);
				size_t sig_length = st.st_size - sig_offset;
				if (parse_signature(fd, sig_offset, sig_length)) {
					signature_parsed = 1;
				} else {
					printf("%s: Vanmoof signature?: Offset 0x%zx, Length 0x%zx\n", progname,
						sig_offset, sig_length);
				}
				if (lseek(fd, pack_start, SEEK_SET) != (off_t)pack_start) {
					fprintf(stderr, "%s: seek(%zu): failed\n", progname, pack_start);
					exit(1);
				}
			}
			goto retry;
		} else {
			fprintf(stderr, "%s: %s: not a PACK file\n", progname, packfile);
			exit(1);
		}
	}

	if (le32toh(header.offset) + le32toh(header.length) > st.st_size) {
		fprintf(stderr, "%s: WARNING: PACK offset 0x%08zx + length 0x%08x is beyond end of file 0x%08zx\n",
			progname, offset, le32toh(header.length), st.st_size);
		exit(1);
	}

	offset = le32toh(header.offset);
	for (i = 0; i < le32toh(header.length) / sizeof(entry); i++) {
		n = lseek(fd, offset + pack_start, SEEK_SET);
		if (n != offset + pack_start) {
			fprintf(stderr, "%s: seek(%u): %zd\n", progname, offset + pack_start, n);
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

		{
			char len_buf[32];
			format_size(le32toh(entry.length), len_buf, sizeof(len_buf));
			printf("file: %s, offset 0x%08x, length %s\n", entry.filename,
				le32toh(entry.offset), len_buf);
		}

		if (!list_only) {
			out = open(entry.filename, O_WRONLY|O_CREAT|O_TRUNC, 0666);
			if (out < 0) {
				fprintf(stderr, "%s: open(%s): %s\n", progname, packfile, strerror(errno));
				exit(1);
			}

			n = lseek(fd, le32toh(entry.offset) + pack_start, SEEK_SET);
			if (n != le32toh(entry.offset) + pack_start) {
				fprintf(stderr, "%s: seek(%u): %zd\n", progname, le32toh(entry.offset) + pack_start, n);
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
		}

		offset += sizeof(entry);
	}

	if (!signature_parsed) {
		size_t pack_end = pack_start + le32toh(header.offset) + le32toh(header.length);
		if (pack_end < (size_t)st.st_size) {
			size_t sig_offset = pack_end;
			size_t sig_length = st.st_size - sig_offset;
			if (!parse_signature(fd, sig_offset, sig_length)) {
				printf("%s: Vanmoof signature?: Offset 0x%zx, Length 0x%zx\n", progname,
					sig_offset, sig_length);
			}
		}
	}

	return 0;
}
