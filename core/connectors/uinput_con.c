/*
 Copyright (c) 2026 GIMX contributors
 License: GPLv3
 */

#include <connectors/uinput_con.h>

#ifndef WIN32

#include <controller.h>
#include <gimx.h>
#include <mainloop.h>
#include <gimxcontroller/include/controller.h>
#include <gimxcontroller/include/x360.h>
#include <gimxcontroller/include/ds4.h>
#include <gimxinput/include/ginput.h>
#include <gimxlog/include/glog.h>
#include <gimxpoll/include/gpoll.h>
#include <gimxtime/include/gtime.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define UINPUT_PATH "/dev/uinput"
#define UINPUT_PATH_ALT "/dev/input/uinput"

#define UINPUT_MAX_FF_EFFECTS 16

typedef enum
{
  M_KEY,          // 0 = released, anything else = pressed
  M_ABS_STICK,    // centered axis
  M_ABS_TRIGGER,  // [0, max] axis
} e_map_kind;

typedef struct
{
  int axis;
  e_map_kind kind;
  int code;
} s_map;

typedef struct
{
  e_controller_type ctype;
  const char * name;
  unsigned short vendor;
  unsigned short product;
  unsigned short version;
  int stick_min;
  int stick_max;
  int stick_offset;  // added to the GIMX axis value to get the evdev value
  int trigger_max;
  struct
  {
    int up, down, left, right;
  } dpad;
  const s_map * maps;
  unsigned int nb_maps;
} s_profile;

/*
 * Microsoft X-Box 360 pad, evdev layout of the Linux xpad driver.
 * The ids make SDL, Steam, Wine/Proton and most games use their xinput mappings.
 */
static const s_map x360_maps[] =
{
  { x360a_lstick_x, M_ABS_STICK,   ABS_X },
  { x360a_lstick_y, M_ABS_STICK,   ABS_Y },
  { x360a_rstick_x, M_ABS_STICK,   ABS_RX },
  { x360a_rstick_y, M_ABS_STICK,   ABS_RY },
  { x360a_LT,       M_ABS_TRIGGER, ABS_Z },
  { x360a_RT,       M_ABS_TRIGGER, ABS_RZ },
  { x360a_A,        M_KEY,         BTN_A },
  { x360a_B,        M_KEY,         BTN_B },
  { x360a_X,        M_KEY,         BTN_X },
  { x360a_Y,        M_KEY,         BTN_Y },
  { x360a_LB,       M_KEY,         BTN_TL },
  { x360a_RB,       M_KEY,         BTN_TR },
  { x360a_back,     M_KEY,         BTN_SELECT },
  { x360a_start,    M_KEY,         BTN_START },
  { x360a_guide,    M_KEY,         BTN_MODE },
  { x360a_LS,       M_KEY,         BTN_THUMBL },
  { x360a_RS,       M_KEY,         BTN_THUMBR },
};

/*
 * Sony DualShock 4, evdev layout of the Linux hid-playstation driver.
 * The touchpad and the motion sensors are not emulated.
 */
static const s_map ds4_maps[] =
{
  { ds4a_lstick_x,  M_ABS_STICK,   ABS_X },
  { ds4a_lstick_y,  M_ABS_STICK,   ABS_Y },
  { ds4a_rstick_x,  M_ABS_STICK,   ABS_RX },
  { ds4a_rstick_y,  M_ABS_STICK,   ABS_RY },
  { ds4a_l2,        M_ABS_TRIGGER, ABS_Z },
  { ds4a_r2,        M_ABS_TRIGGER, ABS_RZ },
  { ds4a_cross,     M_KEY,         BTN_SOUTH },
  { ds4a_circle,    M_KEY,         BTN_EAST },
  { ds4a_triangle,  M_KEY,         BTN_NORTH },
  { ds4a_square,    M_KEY,         BTN_WEST },
  { ds4a_l1,        M_KEY,         BTN_TL },
  { ds4a_r1,        M_KEY,         BTN_TR },
  { ds4a_l2,        M_KEY,         BTN_TL2 },
  { ds4a_r2,        M_KEY,         BTN_TR2 },
  { ds4a_share,     M_KEY,         BTN_SELECT },
  { ds4a_options,   M_KEY,         BTN_START },
  { ds4a_ps,        M_KEY,         BTN_MODE },
  { ds4a_l3,        M_KEY,         BTN_THUMBL },
  { ds4a_r3,        M_KEY,         BTN_THUMBR },
};

static const s_profile profiles[] =
{
  {
    .ctype = C_TYPE_360_PAD,
    .name = "Microsoft X-Box 360 pad (GIMX virtual)",
    .vendor = X360_VENDOR,
    .product = X360_PRODUCT,
    .version = 0x0114,
    .stick_min = SHRT_MIN,
    .stick_max = SHRT_MAX,
    .stick_offset = 0,
    .trigger_max = MAX_AXIS_VALUE_8BITS,
    .dpad = { x360a_up, x360a_down, x360a_left, x360a_right },
    .maps = x360_maps,
    .nb_maps = sizeof(x360_maps) / sizeof(*x360_maps),
  },
  {
    .ctype = C_TYPE_DS4,
    .name = "Sony Interactive Entertainment Wireless Controller (GIMX virtual)",
    .vendor = DS4_VENDOR,
    .product = DS4_PRODUCT,
    .version = 0x8111,
    .stick_min = 0,
    .stick_max = MAX_AXIS_VALUE_8BITS,
    .stick_offset = CENTER_AXIS_VALUE_8BITS,
    .trigger_max = MAX_AXIS_VALUE_8BITS,
    .dpad = { ds4a_up, ds4a_down, ds4a_left, ds4a_right },
    .maps = ds4_maps,
    .nb_maps = sizeof(ds4_maps) / sizeof(*ds4_maps),
  },
};

typedef struct
{
  int used;
  int playing;
  unsigned short weak;
  unsigned short strong;
  unsigned int length_ms; // 0 = infinite
  gtime start;
} s_ff_effect;

static struct
{
  int fd;
  const s_profile * profile;
  int last[AXIS_MAX];
  int hat_x;
  int hat_y;
  int first;
  s_ff_effect effects[UINPUT_MAX_FF_EFFECTS];
  unsigned short rumble_weak;
  unsigned short rumble_strong;
} devices[MAX_CONTROLLERS];

void uinput_con_init_static(void) __attribute__((constructor));
void uinput_con_init_static(void)
{
  unsigned int i;
  for (i = 0; i < MAX_CONTROLLERS; ++i)
  {
    devices[i].fd = -1;
  }
}

static const s_profile * get_profile(e_controller_type ctype)
{
  unsigned int i;
  for (i = 0; i < sizeof(profiles) / sizeof(*profiles); ++i)
  {
    if (profiles[i].ctype == ctype)
    {
      return profiles + i;
    }
  }
  return NULL;
}

int uinput_con_is_supported(e_controller_type ctype)
{
  return get_profile(ctype) != NULL;
}

static int emit(int fd, unsigned short type, unsigned short code, int value)
{
  struct input_event ie = { .type = type, .code = code, .value = value };
  if (write(fd, &ie, sizeof(ie)) != sizeof(ie))
  {
    gerror("uinput write failed: %s\n", strerror(errno));
    return -1;
  }
  return 0;
}

static int set_abs(int fd, unsigned short code, int min, int max, int flat)
{
  struct uinput_abs_setup abs_setup =
  {
    .code = code,
    .absinfo = { .minimum = min, .maximum = max, .flat = flat }
  };
  if (ioctl(fd, UI_SET_ABSBIT, code) < 0 || ioctl(fd, UI_ABS_SETUP, &abs_setup) < 0)
  {
    return -1;
  }
  return 0;
}

/*
 * Rumble: the game uploads/plays FF_RUMBLE effects on the virtual device,
 * and they are forwarded to the physical joystick assigned to this controller.
 */
static void update_rumble(int adapter)
{
  gtime now = gtime_gettime();
  unsigned int weak = 0, strong = 0;
  unsigned int i;
  for (i = 0; i < UINPUT_MAX_FF_EFFECTS; ++i)
  {
    s_ff_effect * effect = devices[adapter].effects + i;
    if (!effect->used || !effect->playing)
    {
      continue;
    }
    if (effect->length_ms && now - effect->start >= (gtime) effect->length_ms * 1000000ULL)
    {
      effect->playing = 0;
      continue;
    }
    weak += effect->weak;
    strong += effect->strong;
  }
  if (weak > USHRT_MAX) weak = USHRT_MAX;
  if (strong > USHRT_MAX) strong = USHRT_MAX;

  if (weak == devices[adapter].rumble_weak && strong == devices[adapter].rumble_strong)
  {
    return;
  }
  devices[adapter].rumble_weak = weak;
  devices[adapter].rumble_strong = strong;

  int joystick = adapter_get_device(E_DEVICE_TYPE_JOYSTICK, adapter);
  if (joystick >= 0 && (ginput_joystick_get_haptic(joystick) & GE_HAPTIC_RUMBLE))
  {
    GE_Event event =
    {
      .jrumble =
      {
        .type = GE_JOYRUMBLE,
        .which = joystick,
        .weak = weak,
        .strong = strong
      }
    };
    ginput_queue_push(&event);
  }
}

static int read_callback(void * user)
{
  int adapter = (intptr_t) user;
  int fd = devices[adapter].fd;
  struct input_event ie;

  while (read(fd, &ie, sizeof(ie)) == sizeof(ie))
  {
    if (ie.type == EV_UINPUT && ie.code == UI_FF_UPLOAD)
    {
      struct uinput_ff_upload upload = { .request_id = ie.value };
      if (ioctl(fd, UI_BEGIN_FF_UPLOAD, &upload) < 0)
      {
        continue;
      }
      upload.retval = 0;
      if (upload.effect.id >= 0 && upload.effect.id < UINPUT_MAX_FF_EFFECTS && upload.effect.type == FF_RUMBLE)
      {
        s_ff_effect * effect = devices[adapter].effects + upload.effect.id;
        effect->used = 1;
        effect->weak = upload.effect.u.rumble.weak_magnitude;
        effect->strong = upload.effect.u.rumble.strong_magnitude;
        effect->length_ms = upload.effect.replay.length;
      }
      else
      {
        upload.retval = -EINVAL;
      }
      ioctl(fd, UI_END_FF_UPLOAD, &upload);
    }
    else if (ie.type == EV_UINPUT && ie.code == UI_FF_ERASE)
    {
      struct uinput_ff_erase erase = { .request_id = ie.value };
      if (ioctl(fd, UI_BEGIN_FF_ERASE, &erase) < 0)
      {
        continue;
      }
      if (erase.effect_id < UINPUT_MAX_FF_EFFECTS)
      {
        memset(devices[adapter].effects + erase.effect_id, 0x00, sizeof(s_ff_effect));
      }
      erase.retval = 0;
      ioctl(fd, UI_END_FF_ERASE, &erase);
    }
    else if (ie.type == EV_FF && ie.code < UINPUT_MAX_FF_EFFECTS)
    {
      s_ff_effect * effect = devices[adapter].effects + ie.code;
      effect->playing = ie.value > 0;
      effect->start = gtime_gettime();
    }
  }

  update_rumble(adapter);

  return 0;
}

static int close_callback(void * user)
{
  int adapter = (intptr_t) user;
  gerror("virtual controller #%d was closed\n", adapter + 1);
  set_done();
  return -1;
}

int uinput_con_open(int adapter, e_controller_type ctype)
{
  if (adapter < 0 || adapter >= MAX_CONTROLLERS)
  {
    return -1;
  }

  const s_profile * profile = get_profile(ctype);
  if (profile == NULL)
  {
    gerror(_("controller type %s can't be emulated in software, use 360pad or DS4.\n"), controller_get_name(ctype));
    return -1;
  }

  int fd = open(UINPUT_PATH, O_RDWR | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0)
  {
    fd = open(UINPUT_PATH_ALT, O_RDWR | O_NONBLOCK | O_CLOEXEC);
  }
  if (fd < 0)
  {
    gerror(_("can't open %s: %s\n"), UINPUT_PATH, strerror(errno));
    gerror(_("make sure the uinput module is loaded (modprobe uinput) and that you have write access to %s.\n"), UINPUT_PATH);
    return -1;
  }

  int ret = 0;
  unsigned int i;

  ret |= ioctl(fd, UI_SET_EVBIT, EV_KEY);
  ret |= ioctl(fd, UI_SET_EVBIT, EV_ABS);
  ret |= ioctl(fd, UI_SET_EVBIT, EV_FF);
  ret |= ioctl(fd, UI_SET_FFBIT, FF_RUMBLE);

  for (i = 0; i < profile->nb_maps && ret >= 0; ++i)
  {
    const s_map * map = profile->maps + i;
    switch (map->kind)
    {
    case M_KEY:
      ret |= ioctl(fd, UI_SET_KEYBIT, map->code);
      break;
    case M_ABS_STICK:
      ret |= set_abs(fd, map->code, profile->stick_min, profile->stick_max, 0);
      break;
    case M_ABS_TRIGGER:
      ret |= set_abs(fd, map->code, 0, profile->trigger_max, 0);
      break;
    }
  }
  ret |= set_abs(fd, ABS_HAT0X, -1, 1, 0);
  ret |= set_abs(fd, ABS_HAT0Y, -1, 1, 0);

  struct uinput_setup setup =
  {
    .id =
    {
      .bustype = BUS_USB,
      .vendor = profile->vendor,
      .product = profile->product,
      .version = profile->version,
    },
    .ff_effects_max = UINPUT_MAX_FF_EFFECTS,
  };
  snprintf(setup.name, sizeof(setup.name), "%s", profile->name);

  if (ret < 0 || ioctl(fd, UI_DEV_SETUP, &setup) < 0 || ioctl(fd, UI_DEV_CREATE) < 0)
  {
    gerror(_("failed to create the virtual controller: %s\n"), strerror(errno));
    close(fd);
    return -1;
  }

  memset(devices + adapter, 0x00, sizeof(*devices));
  devices[adapter].fd = fd;
  devices[adapter].profile = profile;
  devices[adapter].first = 1;

  GPOLL_CALLBACKS callbacks =
  {
    .fp_read = read_callback,
    .fp_write = NULL,
    .fp_close = close_callback,
  };
  if (gpoll_register_fd(fd, (void *)(intptr_t) adapter, &callbacks) < 0)
  {
    gwarn("failed to register the virtual controller for rumble events\n");
  }

  ginfo(_("Virtual controller created: %s.\n"), profile->name);

  return 0;
}

static int stick_value(const s_profile * profile, int value)
{
  return clamp(profile->stick_min, value + profile->stick_offset, profile->stick_max);
}

int uinput_con_send(int adapter, const int axis[AXIS_MAX])
{
  if (adapter < 0 || adapter >= MAX_CONTROLLERS || devices[adapter].fd < 0)
  {
    return -1;
  }

  int fd = devices[adapter].fd;
  const s_profile * profile = devices[adapter].profile;
  int * last = devices[adapter].last;
  int first = devices[adapter].first;
  int changed = 0;
  unsigned int i;

  for (i = 0; i < profile->nb_maps; ++i)
  {
    const s_map * map = profile->maps + i;
    int value = axis[map->axis];
    unsigned short type = EV_ABS;
    switch (map->kind)
    {
    case M_KEY:
      type = EV_KEY;
      value = value ? 1 : 0;
      break;
    case M_ABS_STICK:
      value = stick_value(profile, value);
      break;
    case M_ABS_TRIGGER:
      value = clamp(0, value, profile->trigger_max);
      break;
    }
    /*
     * The same GIMX axis can be mapped to an analog and a digital code (DS4 L2/R2),
     * so compare against the converted value stored for each mapping.
     */
    if (first || last[i] != value)
    {
      if (emit(fd, type, map->code, value) < 0)
      {
        return -1;
      }
      last[i] = value;
      changed = 1;
    }
  }

  int hat_x = (axis[profile->dpad.right] ? 1 : 0) - (axis[profile->dpad.left] ? 1 : 0);
  int hat_y = (axis[profile->dpad.down] ? 1 : 0) - (axis[profile->dpad.up] ? 1 : 0);
  if (first || hat_x != devices[adapter].hat_x)
  {
    emit(fd, EV_ABS, ABS_HAT0X, hat_x);
    devices[adapter].hat_x = hat_x;
    changed = 1;
  }
  if (first || hat_y != devices[adapter].hat_y)
  {
    emit(fd, EV_ABS, ABS_HAT0Y, hat_y);
    devices[adapter].hat_y = hat_y;
    changed = 1;
  }

  devices[adapter].first = 0;

  if (changed && emit(fd, EV_SYN, SYN_REPORT, 0) < 0)
  {
    return -1;
  }

  // expire timed rumble effects
  update_rumble(adapter);

  return 0;
}

void uinput_con_close(int adapter)
{
  if (adapter < 0 || adapter >= MAX_CONTROLLERS || devices[adapter].fd < 0)
  {
    return;
  }
  gpoll_remove_fd(devices[adapter].fd);
  ioctl(devices[adapter].fd, UI_DEV_DESTROY);
  close(devices[adapter].fd);
  devices[adapter].fd = -1;
}

#else

#include <gimxlog/include/glog.h>

int uinput_con_is_supported(e_controller_type ctype __attribute__((unused)))
{
  return 0;
}

int uinput_con_open(int adapter __attribute__((unused)), e_controller_type ctype __attribute__((unused)))
{
  gerror("the virtual controller is only available on Linux.\n");
  return -1;
}

int uinput_con_send(int adapter __attribute__((unused)), const int axis[AXIS_MAX] __attribute__((unused)))
{
  return -1;
}

void uinput_con_close(int adapter __attribute__((unused)))
{
}

#endif
