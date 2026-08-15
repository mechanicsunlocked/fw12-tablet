/* Display accelerometer -> screen orientation.
 *
 * The Framework 12 has TWO accelerometers: accel-display (the lid) and
 * accel-base (the keyboard half). Rotation must follow the lid. They are
 * distinguished only by their `label` attribute -- the iio:deviceN index and
 * the cros-ec-accel.N.auto platform path both move between boots, and
 * cros-ec-accel.11.auto was accel-base on one boot and accel-display on the
 * next (FINDINGS.md 2.1). So we resolve by label, always.
 *
 * The axis convention below was measured on this hardware against
 * iio-sensor-proxy, not derived (FINDINGS.md 2.2b). Panel mounting differs
 * between units; if rotation comes out mirrored, this table is what to flip.
 */
#ifndef FW12_ACCEL_H
#define FW12_ACCEL_H

/* Values are Hyprland monitor transforms, so they can be used directly. */
enum {
    ORI_NORMAL    = 0,
    ORI_LEFT_UP   = 1,
    ORI_BOTTOM_UP = 2,
    ORI_RIGHT_UP  = 3,
    ORI_FLAT      = -1, /* gravity along Z: no orientation information */
};

typedef struct {
    char dir[256]; /* resolved iio device dir, "" if not found */
} accel;

/* Resolve the accel-display device. Returns 0 on success, -1 if not found. */
int accel_open(accel *a);

/* Re-resolve after a read failure (device may have been renumbered). */
int accel_reopen(accel *a);

/* Read raw x/y/z. Returns 0 on success, -1 on failure. */
int accel_read(accel *a, int *x, int *y, int *z);

/* Classify a reading. Returns ORI_* ; ORI_FLAT means "hold previous". */
int accel_orientation(int x, int y, int z);

const char *accel_orientation_name(int ori);

#endif
