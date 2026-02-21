/*
 * trimmer.c - A high-performance, incredibly safe, streaming, fuzzy-logic whitespace cleaner.
 * Copyright (C) 2026 Ruri / Mei
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>
#include <stdbool.h>
#include <limits.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>

#define alw_inl __attribute__((always_inline)) inline
#define CHUNK_SIZE 65536
#define THRESHOLD 10

/*
 * 512-bit integer for cosmic-scale file sizes.
 * Enough for 10^154 bytes.
 */
typedef struct { uint64_t w[8]; } bigoff_t;
#define BIGOFF_BITS 512

static inline bigoff_t off_to_bigoff(off_t x) {
  bigoff_t r = {{0}};
  r.w[0] = (uint64_t)x;
  return r;
}

static inline off_t bigoff_to_off(bigoff_t x) {
  return (off_t)x.w[0];
}

static inline bigoff_t bigoff_add(bigoff_t a, uint64_t b) {
  bigoff_t r = a;
  uint64_t carry = b;
  for (int i = 0; i < 8 && carry; i++) {
    uint64_t sum = r.w[i] + carry;
    carry = (sum < r.w[i]) ? 1 : 0;
    r.w[i] = sum;
  }
  return r;
}

static inline int bigoff_lt(bigoff_t a, bigoff_t b) {
  for (int i = 7; i >= 0; i--) {
    if (a.w[i] < b.w[i]) return 1;
    if (a.w[i] > b.w[i]) return 0;
  }
  return 0;
}

static inline bigoff_t bigoff_sub_u64(bigoff_t a, uint64_t b) {
  bigoff_t r = a;
  if (r.w[0] >= b) { r.w[0] -= b; return r; }
  r.w[0] -= b;
  for (int i = 1; i < 8; i++) {
    if (r.w[i] > 0) { r.w[i]--; break; }
    r.w[i] = UINT64_MAX;
  }
  return r;
}

typedef struct {
  bigoff_t val;
  pthread_spinlock_t lock;
} atomic_bigoff_t;

static inline void atomic_bigoff_init(atomic_bigoff_t *a, bigoff_t v) {
  a->val = v;
  pthread_spin_init(&a->lock, PTHREAD_PROCESS_PRIVATE);
}

static inline bigoff_t atomic_bigoff_fetch_add(atomic_bigoff_t *a, uint64_t v) {
  pthread_spin_lock(&a->lock);
  bigoff_t old = a->val;
  a->val = bigoff_add(a->val, v);
  pthread_spin_unlock(&a->lock);
  return old;
}

static inline void atomic_bigoff_destroy(atomic_bigoff_t *a) {
  pthread_spin_destroy(&a->lock);
}

static int no_bin = 0;

/* * Check extensions to avoid touching things that definitely shouldn't
 * be messed with (like compressed audio, ROMs, or compiled binaries).
 */
static alw_inl int get_ext_confidence(const char *n) {
  const char *d = strrchr(n, '.');
  if(!d) return 0;

  // HIGH CONFIDENCE: Source Code, Scripts, and Markup
  const char *code[] = {
    "c", "h", "cpp", "hpp", "cs", "java", "py", "rb", "go", "rs",
    "js", "ts", "php", "sh", "bat", "pl", "lua", "asm", "s", "ada",
    "ads", "adb", "bas", "pas", "sql", "html", "css", "md", "json",
    "xml", "yaml", "gd", "gml", "vmf", "vmt", "r", "scala", "raku",
    "nqp", "perl", "dart", "hs", "lisp", "ml", "clj", "p", "y", "l"
  };
  for(size_t i=0; i < sizeof(code)/sizeof(char*); i++)
    if(strcasecmp(d+1, code[i]) == 0) return 60;

  // MEDIUM CONFIDENCE: Data Tables, Subtitles, and Configs
  const char *data[] = {
    "csv", "tsv", "srt", "ass", "ssa", "ini", "cfg", "conf", "txt",
    "log", "reg", "info", "nfo", "m3u", "xspf", "jsonld", "markdown"
  };
  for(size_t i=0; i < sizeof(data)/sizeof(char*); i++)
    if(strcasecmp(d+1, data[i]) == 0) return 40;

  // NEGATIVE CONFIDENCE (BINARY PENALTY): Media, Game ROMs, and Executables
  const char *binary[] = {
    // Executables and Libraries
    "exe", "dll", "so", "bin", "com", "o", "a", "lib", "ko", "elf",
    "class", "jar", "war", "ear", "deb", "rpm", "msi", "apk", "ipa",
    "app", "dmg", "pkg", "xpi", "appx", "msix", "dol", "xbe", "xex",
    // Archives and Compressed
    "zip", "7z", "rar", "gz", "bz2", "xz", "lz", "lzma", "lzo",
    "tar", "tgz", "tbz2", "txz", "cab", "arj", "ace", "arc", "alz",
    "lzh", "zoo", "z", "zst", "br", "sitx", "sea", "cpt", "egg",
    // Disk Images and Virtual Disks
    "iso", "img", "vhd", "vhdx", "vmdk", "vdi", "dmg", "nrg", "mds",
    "mdx", "cue", "cdi", "cif", "daa", "gho", "ghs", "tib", "wim",
    "swm", "esd", "adf", "adz", "d64", "dms", "dsk", "qcow", "qcow2",
    // Audio
    "mp3", "wav", "flac", "aac", "ogg", "oga", "opus", "m4a", "wma",
    "aiff", "aif", "ape", "mpc", "wv", "tta", "tak", "ac3", "dts",
    "mid", "midi", "mod", "xm", "it", "s3m", "sid", "spc", "nsf",
    // Video
    "mp4", "mkv", "avi", "mov", "wmv", "flv", "webm", "m4v", "mpg",
    "mpeg", "m2v", "vob", "3gp", "3g2", "mxf", "rm", "rmvb", "asf",
    "ogv", "ts", "m2ts", "mts", "divx", "xvid", "bik", "bk2", "roq",
    "smk", "thp", "dvr-ms", "wtv", "swf",
    // Images
    "png", "jpg", "jpeg", "gif", "bmp", "tiff", "tif", "webp", "avif",
    "heic", "heif", "ico", "icns", "psd", "xcf", "raw", "cr2", "nef",
    "dng", "arw", "orf", "rw2", "pef", "sr2", "tga", "dds", "hdr",
    "exr", "jp2", "jxl", "pcx", "pbm", "pgm", "ppm", "svg", "svgz",
    // Fonts
    "ttf", "otf", "woff", "woff2", "eot", "fon", "fnt", "pfb", "pfm",
    // Game ROMs and Data
    "nes", "snes", "gba", "gbc", "gb", "nds", "3ds", "n64", "z64",
    "v64", "sfc", "smc", "md", "smd", "gg", "pce", "vb", "ws", "wsc",
    "gcm", "wbfs", "rvz", "nsp", "xci", "cia", "vpk", "pbp", "cso",
    "wad", "pak", "pk2", "pk3", "pk4", "bsp", "gcf", "vpk", "mpq",
    "pbo", "upk", "uasset", "umap", "u", "mdl", "vtf", "vmt", "bsp",
    // Databases
    "db", "sqlite", "sqlite3", "mdb", "accdb", "fdb", "gdb", "dbf",
    // Documents (binary)
    "pdf", "doc", "docx", "xls", "xlsx", "ppt", "pptx", "odt", "ods",
    "odp", "epub", "mobi", "azw", "azw3",
    // 3D Models
    "obj", "fbx", "dae", "3ds", "blend", "max", "mb", "ma", "c4d",
    "stl", "ply", "gltf", "glb", "usd", "usda", "usdc", "usdz",
    // Miscellaneous Binary
    "pyc", "pyo", "elc", "beam", "rlib", "pdb", "dmp", "core"
  };
  for(size_t i=0; i < sizeof(binary)/sizeof(char*); i++)
    if(strcasecmp(d+1, binary[i]) == 0) return -100;

  return 5;
}

/* * Deep Content Analysis:
 * We look at the actual bytes to calculate a score. If we see nulls or
 * non-text control characters, we tank the score to prevent corruption.
 */
static alw_inl int calculate_content_score(const unsigned char *buf, size_t len) {
  int score = 0;
  if (len == 0) return 0;
  for (size_t i = 0; i < len; i++) {
    unsigned char c = buf[i];
    if (isalnum(c) || isspace(c) || ispunct(c)) score += 1;
    else if (c == '\0') score -= 30;
    else if ((c < 32 && (c < 9 || c > 13))) score -= 15;
  }
  return (int)((score * 100) / (int)len);
}

typedef struct {
  int fd_in;
  int fd_out;
  atomic_bigoff_t offset;
  bigoff_t total_size;
  pthread_mutex_t write_mtx;
} FileJob;

/* * Worker Thread:
 * Streams chunks of the file, identifies line endings, and backtracks
 * to strip trailing spaces or tabs before writing to the output.
 */
static void* stream_worker(void* arg) {
  FileJob *job = (FileJob*)arg;
  unsigned char *buf = malloc(CHUNK_SIZE);
  if (!buf) return NULL;

  while (1) {
    bigoff_t start = atomic_bigoff_fetch_add(&job->offset, CHUNK_SIZE);
    if (!bigoff_lt(start, job->total_size)) break;

    bigoff_t remaining = bigoff_sub_u64(job->total_size, bigoff_to_off(start));
    size_t to_read = (remaining.w[0] < CHUNK_SIZE && remaining.w[1] == 0) ?
             (size_t)remaining.w[0] : CHUNK_SIZE;

    if (pread(job->fd_in, buf, to_read, bigoff_to_off(start)) != (ssize_t)to_read) break;

    bigoff_t end_pos = bigoff_sub_u64(job->total_size, 1);
    for (size_t i = 0; i < to_read; i++) {
      bigoff_t cur_pos = bigoff_add(start, i);
      int at_end = (cur_pos.w[0] == end_pos.w[0]) && (cur_pos.w[1] == end_pos.w[1]);
      if (buf[i] == '\n' || at_end) {
        ssize_t e = (buf[i] == '\n') ? (ssize_t)i - 1 : (ssize_t)i;
        while (e >= 0 && (buf[e] == ' ' || buf[e] == '\t')) {
          buf[e] = '\0';
          e--;
        }
      }
    }

    pthread_mutex_lock(&job->write_mtx);
    for(size_t i = 0; i < to_read; i++) {
      if (buf[i] != '\0' || (i < to_read - 1 && buf[i+1] == '\n')) {
         pwrite(job->fd_out, &buf[i], 1, bigoff_to_off(bigoff_add(start, i)));
      }
    }
    pthread_mutex_unlock(&job->write_mtx);
  }
  free(buf);
  return NULL;
}

static alw_inl void process_file(const char *path, struct stat *st) {
  int fd = open(path, O_RDONLY | O_NOFOLLOW);
  if (fd == -1) return;

  int confidence = 100;
  if (!no_bin) {
    unsigned char head[4096];
    ssize_t r = read(fd, head, sizeof(head));
    confidence = get_ext_confidence(path);
    if (r > 0) confidence += calculate_content_score(head, r);

    if (confidence < THRESHOLD) {
      close(fd);
      return;
    }
    lseek(fd, 0, SEEK_SET);
  }

  char tmp[PATH_MAX + 16];
  snprintf(tmp, sizeof(tmp), "%s.XXXXXX", path);
  int out_fd = mkstemp(tmp);
  if (out_fd == -1) { close(fd); return; }

  FileJob job = { .fd_in = fd, .fd_out = out_fd, .total_size = off_to_bigoff(st->st_size) };
  atomic_bigoff_init(&job.offset, off_to_bigoff(0));
  pthread_mutex_init(&job.write_mtx, NULL);

  pthread_t t1, t2;
  pthread_create(&t1, NULL, stream_worker, &job);
  pthread_create(&t2, NULL, stream_worker, &job);
  pthread_join(t1, NULL);
  pthread_join(t2, NULL);

  atomic_bigoff_destroy(&job.offset);
  pthread_mutex_destroy(&job.write_mtx);
  close(fd);
  close(out_fd);

  chmod(tmp, st->st_mode & 0777);
  rename(tmp, path);
}

void walk_directory(const char *dir_name) {
  DIR *d = opendir(dir_name);
  if (!d) return;
  struct dirent *e;
  while ((e = readdir(d)) != NULL) {
    if (e->d_name[0] == '.' && (e->d_name[1] == '\0' || e->d_name[1] == '.')) continue;

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", dir_name, e->d_name);

    struct stat st;
    if (lstat(path, &st) == -1) continue;

    if (S_ISDIR(st.st_mode)) {
      walk_directory(path);
    } else if (S_ISREG(st.st_mode)) {
      process_file(path, &st);
    }
  }
  closedir(d);
}

int main(int argc, char **argv) {
  const char *start_node = ".";

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--help") == 0) {
      printf("Trim is a super fast, super safe, fuzzy-logic whitespace cleaner.\n\n--no-bin -> tells the cleaner, assume trailing whitespace will never ever exist\n--version -> prints current version");
    } else if (strcmp(argv[i], "--version")) {
      printf("trim v0.1.0\nCopyright (C) 2026 Ruri / Mei\nUnder GNU General Public License V3 or later.");
    } else if (strcmp(argv[i], "--no-bin") == 0) {
      no_bin = 1;
    } else {
      start_node = argv[i];
    }
  }

  struct stat st;
  if (lstat(start_node, &st) == -1) {
    perror(start_node);
    return 1;
  }

  if (S_ISDIR(st.st_mode)) {
    walk_directory(start_node);
  } else if (S_ISREG(st.st_mode)) {
    process_file(start_node, &st);
  } else {
    fprintf(stderr, "%s: not a regular file or directory\n", start_node);
    return 1;
  }

  return 0;
}
