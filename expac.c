#define _GNU_SOURCE
#include <alpm.h>
#include <ctype.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#define FORMAT_TOKENS "BCDEFGLNOPRSabdfiklmnoprsuv%"
#define ESCAPE_TOKENS "\"\\abefnrtv"

alpm_list_t *dblist = NULL, *targets = NULL;
pmdb_t *db_local;
int verbose = 0;
const char *format = NULL;
const char *timefmt = NULL;
const char *listdelim = NULL;
const char *delim = NULL;

typedef const char *(*extractfn)(void*);

static char *strtrim(char *str) {
  char *pch = str;

  if (!str || *str == '\0') {
    return(str);
  }

  while (isspace((unsigned char)*pch)) {
    pch++;
  }
  if (pch != str) {
    memmove(str, pch, (strlen(pch) + 1));
  }

  if (*str == '\0') {
    return(str);
  }

  pch = (str + (strlen(str) - 1));
  while (isspace((unsigned char)*pch)) {
    pch--;
  }
  *++pch = '\0';

  return(str);
}

static int alpm_init() {
  int ret = 0;
  FILE *fp;
  char line[PATH_MAX];
  char *ptr, *section = NULL;

  ret = alpm_initialize();
  if (ret != 0) {
    return(ret);
  }

  ret = alpm_option_set_root("/");
  if (ret != 0) {
    return(ret);
  }

  ret = alpm_option_set_dbpath("/var/lib/pacman");
  if (ret != 0) {
    return(ret);
  }

  db_local = alpm_db_register_local();
  if (!db_local) {
    return(1);
  }

  fp = fopen("/etc/pacman.conf", "r");
  if (!fp) {
    return(1);
  }

  while (fgets(line, PATH_MAX, fp)) {
    strtrim(line);

    if (strlen(line) == 0 || line[0] == '#') {
      continue;
    }
    if ((ptr = strchr(line, '#'))) {
      *ptr = '\0';
    }

    if (line[0] == '[' && line[strlen(line) - 1] == ']') {
      ptr = &line[1];
      if (section) {
        free(section);
      }

      section = strdup(ptr);
      section[strlen(section) - 1] = '\0';

      if (strcmp(section, "options") != 0) {
        if (!alpm_db_register_sync(section)) {
          ret = 1;
          goto finish;
        }
      }
    } else {
      char *key;

      key = ptr = line;
      strsep(&ptr, "=");
      strtrim(key);
      strtrim(ptr);
      if (strcmp(key, "RootDir") == 0) {
        alpm_option_set_root(ptr);
      } else if (strcmp(key, "DBPath") == 0) {
        alpm_option_set_dbpath(ptr);
      }
    }
  }

finish:
  free(section);
  fclose(fp);
  return(ret);
}

static void usage(void) {
  fprintf(stderr, "expac %s\n"
      "Usage: expac [options] <format> target...\n\n", VERSION);
  fprintf(stderr,
      " Options:\n"
      "  -Q, --local               search local DB (default)\n"
      "  -S, --sync                search sync DBs\n\n"
      "  -d, --delim <string>      separator used between packages (default: \"\\n\")\n"
      "  -l, --listdelim <string>  separator used between list elements (default: \"  \")\n"
      "  -t, --timefmt <fmt>       date format passed to strftime (default: \"%%c\")\n\n"
      "  -v, --verbose             be more verbose\n\n"
      "  -h, --help                display this help and exit\n\n");
}

static int parse_options(int argc, char *argv[]) {
  int opt, option_index = 0;

  static struct option opts[] = {
    {"delim",     required_argument,  0, 'd'},
    {"listdelim", required_argument,  0, 'l'},
    {"help",      no_argument,        0, 'h'},
    {"local",     no_argument,        0, 'Q'},
    {"sync",      no_argument,        0, 'S'},
    {"timefmt",   required_argument,  0, 't'},
    {"verbose",   no_argument,        0, 'v'},
    {0, 0, 0, 0}
  };

  while (-1 != (opt = getopt_long(argc, argv, "l:d:hf:QSt:v", opts, &option_index))) {
    switch (opt) {
      case 'S':
        if (dblist) {
          fprintf(stderr, "error: can only select one repo option (use -h for help)\n");
          return(1);
        }
        dblist = alpm_option_get_syncdbs();
        break;
      case 'Q':
        if (dblist) {
          fprintf(stderr, "error: can only select one repo option (use -h for help)\n");
          return(1);
        }
        dblist = alpm_list_add(dblist, db_local);
        break;
      case 'd':
        delim = optarg;
        break;
      case 'l':
        listdelim = optarg;
        break;
      case 'h':
        usage();
        return(1);
      case 't':
        timefmt = optarg;
        break;
      case 'v':
        verbose = 1;
        break;

      case '?':
        return(1);
      default:
        return(1);
    }
  }

  if (optind < argc) {
    format = argv[optind++];
  } else {
    fprintf(stderr, "error: missing format string (use -h for help)\n");
    return(1);
  }

  while (optind < argc) {
    targets = alpm_list_add(targets, argv[optind++]);
  }

  return(0);
}

static void print_escaped(const char *delim) {
  const char *f;

  for (f = delim; *f != '\0'; f++) {
    if (*f == '\\') {
      switch (*++f) {
        case '\\':
          putchar('\\');
          break;
        case '"':
          putchar('\"');
          break;
        case 'a':
          putchar('\a');
          break;
        case 'b':
          putchar('\b');
          break;
        case 'e': /* \e is nonstandard */
          putchar('\033');
          break;
        case 'n':
          putchar('\n');
          break;
        case 'r':
          putchar('\r');
          break;
        case 't':
          putchar('\t');
          break;
        case 'v':
          putchar('\v');
          break;
      }
    } else {
      putchar(*f);
    }
  }
}

static void print_list(alpm_list_t *list, extractfn fn, bool shortdeps) {
  alpm_list_t *i;

  if (!list) {
    if (verbose) {
      printf("None");
    }
    return;
  }

  i = list;
  while (1) {
    char *item;

    item = fn ? (char*)fn(alpm_list_getdata(i)) : (char*)alpm_list_getdata(i);

    if (shortdeps) {
      *(item + strcspn(item, "<>=")) = '\0';
    }

    printf("%s", item);

    if ((i = alpm_list_next(i))) {
      print_escaped(listdelim);
    } else {
      break;
    }
  }

  return;
}

static void print_time(time_t timestamp) {
  char buffer[64];

  if (!timestamp) {
    if (verbose) {
      printf("None");
    }
    return;
  }

  strftime(&buffer[0], 64, timefmt, localtime(&timestamp));
  printf("%s", buffer);
}

static int print_pkg(alpm_list_t *dblist, const char *p, const char *format) {
  alpm_list_t *i;
  pmpkg_t *pkg;
  const char *f;
  char *repo, *pkgname;

  pkgname = repo = (char*)p;
  if (strchr(pkgname, '/')) {
    strsep(&pkgname, "/");
  } else {
    repo = NULL;
  }

  for (i = dblist; i; i = alpm_list_next(i)) {
    pkg = alpm_db_get_pkg(alpm_list_getdata(i), pkgname);
    if (repo && strcmp(repo, alpm_db_get_name(alpm_list_getdata(i))) != 0) {
      continue;
    }
    if (pkg) {
      break;
    }
  }

  if (!pkg) {
    if (verbose) {
      fprintf(stderr, "error: package `%s' not found\n", pkgname);
    }
    return(1);
  }

  for (f = format; *f != '\0'; f++) {
    bool shortdeps = false;
    if (*f == '%') {
      switch (*++f) {
        /* simple attributes */
        case 'f': /* filename */
          printf("%s", alpm_pkg_get_filename(pkg));
          break;
        case 'n': /* package name */
          printf("%s", alpm_pkg_get_name(pkg));
          break;
        case 'v': /* version */
          printf("%s", alpm_pkg_get_version(pkg));
          break;
        case 'd': /* description */
          printf("%s", alpm_pkg_get_desc(pkg));
          break;
        case 'u': /* project url */
          printf("%s", alpm_pkg_get_url(pkg));
          break;
        case 'p': /* packager name */
          printf("%s", alpm_pkg_get_packager(pkg));
          break;
        case 's': /* md5sum */
          printf("%s", alpm_pkg_get_md5sum(pkg));
          break;
        case 'a': /* architecutre */
          printf("%s", alpm_pkg_get_arch(pkg));
          break;
        case 'i': /* has install scriptlet? */
          printf("%s", alpm_pkg_has_scriptlet(pkg) ? "yes" : "no");
          break;
        case 'r': /* repo */
          printf("%s", alpm_db_get_name(alpm_pkg_get_db(pkg)));
          break;

        /* times */
        case 'b': /* build date */
          print_time(alpm_pkg_get_builddate(pkg));
          break;
        case 'l': /* install date */
          print_time(alpm_pkg_get_installdate(pkg));
          break;

        /* sizes */
        case 'k': /* download size */
          printf("%.2f K", (float)alpm_pkg_get_size(pkg) / 1024.0);
          break;
        case 'm': /* install size */
          printf("%.2f K", (float)alpm_pkg_get_isize(pkg) / 1024.0);
          break;

        /* lists */
        case 'N': /* requiredby */
          print_list(alpm_pkg_compute_requiredby(pkg), NULL, shortdeps);
          break;
        case 'L': /* licenses */
          print_list(alpm_pkg_get_licenses(pkg), NULL, shortdeps);
          break;
        case 'G': /* groups */
          print_list(alpm_pkg_get_groups(pkg), NULL, shortdeps);
          break;
        case 'E': /* depends (shortdeps) */
          print_list(alpm_pkg_get_depends(pkg), (extractfn)alpm_dep_get_name, shortdeps);
          break;
        case 'D': /* depends */
          print_list(alpm_pkg_get_depends(pkg), (extractfn)alpm_dep_compute_string, shortdeps);
          break;
        case 'O': /* optdepends */
          print_list(alpm_pkg_get_optdepends(pkg), NULL, shortdeps);
          break;
        case 'C': /* conflicts */
          print_list(alpm_pkg_get_conflicts(pkg), NULL, shortdeps);
          break;
        case 'S': /* provides (shortdeps) */
          shortdeps = true;
        case 'P': /* provides */
          print_list(alpm_pkg_get_provides(pkg), NULL, shortdeps);
          break;
        case 'R': /* replaces */
          print_list(alpm_pkg_get_replaces(pkg), NULL, shortdeps);
          break;
        case 'F': /* files */
          print_list(alpm_pkg_get_files(pkg), NULL, shortdeps);
          break;
        case 'B': /* backup */
          print_list(alpm_pkg_get_backup(pkg), NULL, shortdeps);
          break;
        case '%':
          putchar('%');
          break;
      }
    } else if (*f == '\\') {
      char buf[3];
      buf[0] = *f;
      buf[1] = *++f;
      buf[2] = '\0';
      print_escaped(buf);
    } else {
      putchar(*f);
    }
  }

  print_escaped(delim);

  return(0);
}

int verify_format_string(const char *format) {
  const char *p;

  for (p = format; *p != '\0'; p++) {
    if (*p == '%' && !strchr(FORMAT_TOKENS, *++p)) {
      fprintf(stderr, "error: bad token in format string: %%%c\n", *p);
      return(1);
    } else if (*p == '\\' && !strchr(ESCAPE_TOKENS, *++p)) {
      fprintf(stderr, "error: bad token in format string: \\%c\n", *p);
      return(1);
    }
  }

  return(0);
}

int main(int argc, char *argv[]) {
  int ret = 0, freelist = 0;
  alpm_list_t *i;

  ret = alpm_init();
  if (ret != 0) {
    return(1);
  }

  ret = parse_options(argc, argv);
  if (ret != 0) {
    goto finish;
  }

  /* default vals */
  if (!dblist) {
    dblist = alpm_list_add(dblist, db_local);
    freelist = 1;
  }
  delim = delim ? delim : "\n";
  listdelim = listdelim ? listdelim : "  ";
  timefmt = timefmt ? timefmt : "%c";

  if (verify_format_string(format) != 0) {
    return(1);
  }

  for (i = targets; i; i = alpm_list_next(i)) {
    ret += print_pkg(dblist, alpm_list_getdata(i), format);
  }
  ret = !!ret; /* clamp to zero/one */

  if (freelist) {
    alpm_list_free(dblist);
  }

finish:
  alpm_list_free(targets);
  alpm_release();
  return(ret);
}
