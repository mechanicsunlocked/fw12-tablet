#include "accel.h"
#include "log.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>

/* Raw counts per g, from scale=0.000598550 m/s^2 per count:
 * 9.80665 / 0.00059855 ~= 16384. */
#define ONE_G 16384

/* An axis must carry at least this much of 1 g to be considered dominant.
 * Below it the device is close enough to flat that the reading is noise, and
 * atan2-style classification would jitter. 40% of 1 g. */
#define DEAD_ZONE ((ONE_G * 2) / 5)

static int read_int_file(const char *path, int *out)
{
    FILE *f = fopen(path, "re");
    int v;

    if (!f)
        return -1;
    if (fscanf(f, "%d", &v) != 1) {
        fclose(f);
        return -1;
    }
    fclose(f);
    *out = v;
    return 0;
}

/* Find the iio device whose `label` is exactly "accel-display". */
static int resolve(accel *a)
{
    DIR *d;
    struct dirent *e;

    a->dir[0] = '\0';

    d = opendir("/sys/bus/iio/devices");
    if (!d) {
        log_errno("opendir(/sys/bus/iio/devices)");
        return -1;
    }

    while ((e = readdir(d))) {
        char path[512];
        char label[64];
        FILE *f;

        if (strncmp(e->d_name, "iio:device", 10) != 0)
            continue;

        snprintf(path, sizeof path, "/sys/bus/iio/devices/%.200s/label",
                 e->d_name);
        f = fopen(path, "re");
        if (!f)
            continue;
        if (!fgets(label, sizeof label, f)) {
            fclose(f);
            continue;
        }
        fclose(f);
        label[strcspn(label, "\n")] = '\0';

        if (strcmp(label, "accel-display") == 0) {
            snprintf(a->dir, sizeof a->dir, "/sys/bus/iio/devices/%.200s",
                     e->d_name);
            break;
        }
    }
    closedir(d);

    if (!a->dir[0]) {
        log_warn("no iio device labelled accel-display; rotation unavailable");
        return -1;
    }
    return 0;
}

int accel_open(accel *a)
{
    if (resolve(a) < 0)
        return -1;
    log_info("display accelerometer: %s", a->dir);
    return 0;
}

int accel_reopen(accel *a)
{
    log_warn("accelerometer read failed; re-resolving by label");
    return resolve(a);
}

int accel_read(accel *a, int *x, int *y, int *z)
{
    char path[512];

    if (!a->dir[0])
        return -1;

    snprintf(path, sizeof path, "%s/in_accel_x_raw", a->dir);
    if (read_int_file(path, x) < 0)
        return -1;
    snprintf(path, sizeof path, "%s/in_accel_y_raw", a->dir);
    if (read_int_file(path, y) < 0)
        return -1;
    snprintf(path, sizeof path, "%s/in_accel_z_raw", a->dir);
    if (read_int_file(path, z) < 0)
        return -1;
    return 0;
}

int accel_orientation(int x, int y, int z)
{
    int ax = x < 0 ? -x : x;
    int ay = y < 0 ? -y : y;
    int az = z < 0 ? -z : z;

    /* Screen roughly horizontal: gravity is on Z and X/Y say nothing. */
    if (az > ax && az > ay)
        return ORI_FLAT;

    if (ay > ax) {
        if (ay < DEAD_ZONE)
            return ORI_FLAT;
        return y > 0 ? ORI_NORMAL : ORI_BOTTOM_UP;
    }

    if (ax < DEAD_ZONE)
        return ORI_FLAT;
    return x > 0 ? ORI_RIGHT_UP : ORI_LEFT_UP;
}

const char *accel_orientation_name(int ori)
{
    switch (ori) {
    case ORI_NORMAL:    return "normal";
    case ORI_LEFT_UP:   return "left-up";
    case ORI_BOTTOM_UP: return "bottom-up";
    case ORI_RIGHT_UP:  return "right-up";
    default:            return "flat";
    }
}
