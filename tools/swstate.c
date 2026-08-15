/* Print SW_TABLET_MODE state without root, if the user is in group `input`.
 * Resolves the device by scanning for the SW_TABLET_MODE capability rather
 * than trusting an event number -- those move between boots (event5 -> event7
 * observed across two boots on this machine).
 *
 *   cc -O2 -o swstate swstate.c
 *   ./swstate            # one-shot: prints 1 (tablet) or 0 (laptop)
 *   ./swstate -w         # then streams every change, one line each
 */
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define NLONGS(x) (((x) / (sizeof(long) * 8)) + 1)
#define test_bit(b, a) (((a)[(b) / (sizeof(long) * 8)] >> ((b) % (sizeof(long) * 8))) & 1UL)

static int has_tablet_sw(int fd) {
  unsigned long ev[NLONGS(EV_MAX)] = {0}, sw[NLONGS(SW_MAX)] = {0};
  if (ioctl(fd, EVIOCGBIT(0, sizeof ev), ev) < 0 || !test_bit(EV_SW, ev)) return 0;
  if (ioctl(fd, EVIOCGBIT(EV_SW, sizeof sw), sw) < 0) return 0;
  return test_bit(SW_TABLET_MODE, sw);
}

static int open_tablet_dev(char *namebuf, size_t n) {
  DIR *d = opendir("/dev/input");
  if (!d) return -1;
  struct dirent *e;
  char path[300];
  int fd = -1;
  while ((e = readdir(d))) {
    if (strncmp(e->d_name, "event", 5)) continue;
    snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
    int f = open(path, O_RDONLY);
    if (f < 0) continue;
    if (has_tablet_sw(f)) {
      snprintf(namebuf, n, "%s", path);
      fd = f;
      break;
    }
    close(f);
  }
  closedir(d);
  return fd;
}

int main(int argc, char **argv) {
  char path[300] = "";
  int fd = open_tablet_dev(path, sizeof path);
  if (fd < 0) {
    fprintf(stderr, "swstate: no readable SW_TABLET_MODE device "
                    "(need group `input` or root)\n");
    return 1;
  }
  unsigned long st[NLONGS(SW_MAX)] = {0};
  if (ioctl(fd, EVIOCGSW(sizeof st), st) < 0) { perror("EVIOCGSW"); return 1; }
  printf("%d  (%s)\n", test_bit(SW_TABLET_MODE, st) ? 1 : 0, path);
  fflush(stdout);

  if (argc > 1 && !strcmp(argv[1], "-w")) {
    struct input_event ev;
    while (read(fd, &ev, sizeof ev) == (ssize_t)sizeof ev)
      if (ev.type == EV_SW && ev.code == SW_TABLET_MODE) {
        printf("%d\n", ev.value ? 1 : 0);
        fflush(stdout);
      }
  }
  return 0;
}
