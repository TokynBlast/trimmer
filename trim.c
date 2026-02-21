/*
 * trimmer.c - A high-performance, streaming, fuzzy-logic whitespace cleaner.
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

#define alw_inl __attribute__((always_inline)) inline
#define CHUNK_SIZE 65536
#define THRESHOLD 10

/* * Check extensions to avoid touching things that definitely shouldn't
 * be messed with (like compressed audio, ROMs, or compiled binaries).
 */
static alw_inl int get_ext_confidence(const char *n) {
  const char *d = strrchr(n, '.');
  if(!d) return 0;

  // HIGH CONFIDENCE: Source Code, Scripts, and Markup
  const char *code[] = {
    ".c", ".h", ".cpp", ".hpp", ".cs", ".java", ".py", ".rb", ".go", ".rs",
    ".js", ".ts", ".php", ".sh", ".bat", ".pl", ".lua", ".asm", ".s", ".ada",
    ".ads", ".adb", ".bas", ".pas", ".sql", ".html", ".css", ".md", ".json",
    ".xml", ".yaml", ".gd", ".gml", ".vmf", ".vmt", ".r", ".scala", ".raku",
    ".nqp", ".perl", ".dart", ".hs", ".lisp", ".ml", ".clj", ".p", ".y", ".l"
  };
  for(size_t i=0; i < sizeof(code)/sizeof(char*); i++)
    if(strcasecmp(d, code[i]) == 0) return 60;

  // MEDIUM CONFIDENCE: Data Tables, Subtitles, and Configs
  const char *data[] = {
    ".csv", ".tsv", ".srt", ".ass", ".ssa", ".ini", ".cfg", ".conf", ".txt",
    ".log", ".reg", ".info", ".nfo", ".m3u", ".xspf", ".jsonld", ".markdown"
  };
  for(size_t i=0; i < sizeof(data)/sizeof(char*); i++)
    if(strcasecmp(d, data[i]) == 0) return 40;

  // NEGATIVE CONFIDENCE (BINARY PENALTY): Media, Game ROMs, and Executables
  const char *binary[] = {
    // Executables and Libraries
    ".exe", ".dll", ".so", ".bin", ".com", ".o", ".a", ".lib", ".ko", ".elf",
    ".class", ".jar", ".war", ".ear", ".deb", ".rpm", ".msi", ".apk", ".ipa",
    ".app", ".dmg", ".pkg", ".xpi", ".appx", ".msix", ".dol", ".xbe", ".xex",
    // Archives and Compressed
    ".zip", ".7z", ".rar", ".gz", ".bz2", ".xz", ".lz", ".lzma", ".lzo",
    ".tar", ".tgz", ".tbz2", ".txz", ".cab", ".arj", ".ace", ".arc", ".alz",
    ".lzh", ".zoo", ".z", ".zst", ".br", ".sitx", ".sea", ".cpt", ".egg",
    // Disk Images and Virtual Disks
    ".iso", ".img", ".vhd", ".vhdx", ".vmdk", ".vdi", ".dmg", ".nrg", ".mds",
    ".mdx", ".cue", ".cdi", ".cif", ".daa", ".gho", ".ghs", ".tib", ".wim",
    ".swm", ".esd", ".adf", ".adz", ".d64", ".dms", ".dsk", ".qcow", ".qcow2",
    // Audio
    ".mp3", ".wav", ".flac", ".aac", ".ogg", ".oga", ".opus", ".m4a", ".wma",
    ".aiff", ".aif", ".ape", ".mpc", ".wv", ".tta", ".tak", ".ac3", ".dts",
    ".mid", ".midi", ".mod", ".xm", ".it", ".s3m", ".sid", ".spc", ".nsf",
    // Video
    ".mp4", ".mkv", ".avi", ".mov", ".wmv", ".flv", ".webm", ".m4v", ".mpg",
    ".mpeg", ".m2v", ".vob", ".3gp", ".3g2", ".mxf", ".rm", ".rmvb", ".asf",
    ".ogv", ".ts", ".m2ts", ".mts", ".divx", ".xvid", ".bik", ".bk2", ".roq",
    ".smk", ".thp", ".dvr-ms", ".wtv", ".swf",
    // Images
    ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".tiff", ".tif", ".webp", ".avif",
    ".heic", ".heif", ".ico", ".icns", ".psd", ".xcf", ".raw", ".cr2", ".nef",
    ".dng", ".arw", ".orf", ".rw2", ".pef", ".sr2", ".tga", ".dds", ".hdr",
    ".exr", ".jp2", ".jxl", ".pcx", ".pbm", ".pgm", ".ppm", ".svg", ".svgz",
    // Fonts
    ".ttf", ".otf", ".woff", ".woff2", ".eot", ".fon", ".fnt", ".pfb", ".pfm",
    // Game ROMs and Data
    ".nes", ".snes", ".gba", ".gbc", ".gb", ".nds", ".3ds", ".n64", ".z64",
    ".v64", ".sfc", ".smc", ".md", ".smd", ".gg", ".pce", ".vb", ".ws", ".wsc",
    ".gcm", ".wbfs", ".rvz", ".nsp", ".xci", ".cia", ".vpk", ".pbp", ".cso",
    ".wad", ".pak", ".pk2", ".pk3", ".pk4", ".bsp", ".gcf", ".vpk", ".mpq",
    ".pbo", ".upk", ".uasset", ".umap", ".u", ".mdl", ".vtf", ".vmt", ".bsp",
    // Databases
    ".db", ".sqlite", ".sqlite3", ".mdb", ".accdb", ".fdb", ".gdb", ".dbf",
    // Documents (binary)
    ".pdf", ".doc", ".docx", ".xls", ".xlsx", ".ppt", ".pptx", ".odt", ".ods",
    ".odp", ".epub", ".mobi", ".azw", ".azw3",
    // 3D Models
    ".obj", ".fbx", ".dae", ".3ds", ".blend", ".max", ".mb", ".ma", ".c4d",
    ".stl", ".ply", ".gltf", ".glb", ".usd", ".usda", ".usdc", ".usdz",
    // Miscellaneous Binary
    ".pyc", ".pyo", ".elc", ".beam", ".rlib", ".pdb", ".dmp", ".core"
  };
  for(size_t i=0; i < sizeof(binary)/sizeof(char*); i++)
    if(strcasecmp(d, binary[i]) == 0) return -100;

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
  _Atomic off_t offset;
  off_t total_size;
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
    off_t start = atomic_fetch_add(&job->offset, CHUNK_SIZE);
    if (start >= job->total_size) break;

    size_t to_read = (job->total_size - start < CHUNK_SIZE) ?
             (size_t)(job->total_size - start) : CHUNK_SIZE;

    if (pread(job->fd_in, buf, to_read, start) != (ssize_t)to_read) break;

    for (size_t i = 0; i < to_read; i++) {
      // If we hit a newline or the absolute end of file, check for trailing whitespace
      if (buf[i] == '\n' || ((off_t)(start + i) == job->total_size - 1)) {
        ssize_t end = (buf[i] == '\n') ? (ssize_t)i - 1 : (ssize_t)i;
        while (end >= 0 && (buf[end] == ' ' || buf[end] == '\t')) {
          buf[end] = '\0'; // Mark for exclusion during write
          end--;
        }
      }
    }

    pthread_mutex_lock(&job->write_mtx);
    for(size_t i = 0; i < to_read; i++) {
      if (buf[i] != '\0' || (i < to_read - 1 && buf[i+1] == '\n')) {
         pwrite(job->fd_out, &buf[i], 1, start + i);
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

  unsigned char head[4096];
  ssize_t r = read(fd, head, sizeof(head));
  int confidence = get_ext_confidence(path);
  if (r > 0) confidence += calculate_content_score(head, r);

  if (confidence < THRESHOLD) {
    close(fd);
    return;
  }

  char tmp[PATH_MAX];
  snprintf(tmp, sizeof(tmp), "%s.XXXXXX", path);
  int out_fd = mkstemp(tmp);
  if (out_fd == -1) { close(fd); return; }

  FileJob job = { .fd_in = fd, .fd_out = out_fd, .total_size = st->st_size };
  atomic_init(&job.offset, 0);
  pthread_mutex_init(&job.write_mtx, NULL);

  pthread_t t1, t2;
  pthread_create(&t1, NULL, stream_worker, &job);
  pthread_create(&t2, NULL, stream_worker, &job);
  pthread_join(t1, NULL);
  pthread_join(t2, NULL);

  pthread_mutex_destroy(&job.write_mtx);
  close(fd);
  close(out_fd);

  chmod(tmp, st->st_mode & 0777);
  rename(tmp, path);
  printf("[Ruri/Mei] Cleaned: %s (Conf: %d)\n", path, confidence);
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
  const char *start_node = (argc > 1) ? argv[1] : ".";

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
