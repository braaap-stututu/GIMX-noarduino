/*
 Copyright (c) 2026 GIMX contributors
 License: GPLv3
 */

#ifdef WIN32

#ifdef HAVE_VIGEM

/*
 * Windows backend of the software-only adapter, based on the ViGEmBus driver
 * (https://github.com/nefarius/ViGEmBus) and its client library (shared/ViGEmClient).
 *
 * The reports are built by gimxcontroller exactly as for the hardware adapter,
 * then handed over to the virtual bus. For the DS4, the full USB report is sent,
 * so the touchpad and the motion sensors are emulated too.
 */

#include <connectors/virtual_con.h>

#include <controller.h>
#include <gimx.h>
#include <gimxcontroller/include/controller.h>
#include <gimxinput/include/ginput.h>
#include <gimxlog/include/glog.h>

#include <windows.h>
#include <ViGEm/Client.h>

#include <string.h>

typedef struct
{
  PVIGEM_TARGET target;
  e_controller_type ctype;
  s_report_packet report[MAX_REPORTS];
  int use_ds4_ex;
  /*
   * Rumble requests are received in a ViGEmClient thread: (large << 8) | small, or -1 if no pending update.
   */
  volatile LONG rumble;
} s_virtual_device;

static PVIGEM_CLIENT client = NULL;
static unsigned int nb_targets = 0;
static s_virtual_device devices[MAX_CONTROLLERS];

void virtual_con_vigem_init_static(void) __attribute__((constructor));
void virtual_con_vigem_init_static(void)
{
  unsigned int i;
  for (i = 0; i < MAX_CONTROLLERS; ++i)
  {
    devices[i].rumble = -1;
  }
}

int virtual_con_is_supported(e_controller_type ctype)
{
  return ctype == C_TYPE_360_PAD || ctype == C_TYPE_DS4;
}

static const char * vigem_error_str(VIGEM_ERROR error)
{
  switch (error)
  {
  case VIGEM_ERROR_BUS_NOT_FOUND:
    return "ViGEmBus driver not found, install it from https://github.com/nefarius/ViGEmBus/releases";
  case VIGEM_ERROR_NO_FREE_SLOT:
    return "no free slot on the virtual bus";
  case VIGEM_ERROR_BUS_VERSION_MISMATCH:
    return "ViGEmBus driver version mismatch, install the latest release";
  case VIGEM_ERROR_BUS_ACCESS_FAILED:
    return "can't access the ViGEmBus driver";
  default:
    return "ViGEm error";
  }
}

static VOID CALLBACK x360_notification(PVIGEM_CLIENT c __attribute__((unused)), PVIGEM_TARGET t __attribute__((unused)),
    UCHAR large, UCHAR small, UCHAR led __attribute__((unused)), LPVOID user)
{
  s_virtual_device * device = (s_virtual_device *) user;
  InterlockedExchange(&device->rumble, (large << 8) | small);
}

static VOID CALLBACK ds4_notification(PVIGEM_CLIENT c __attribute__((unused)), PVIGEM_TARGET t __attribute__((unused)),
    UCHAR large, UCHAR small, DS4_LIGHTBAR_COLOR color __attribute__((unused)), LPVOID user)
{
  s_virtual_device * device = (s_virtual_device *) user;
  InterlockedExchange(&device->rumble, (large << 8) | small);
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

int virtual_con_open(int adapter, e_controller_type ctype)
{
  if (adapter < 0 || adapter >= MAX_CONTROLLERS)
  {
    return -1;
  }

  if (!virtual_con_is_supported(ctype))
  {
    gerror(_("controller type %s can't be emulated in software, use 360pad or DS4.\n"), controller_get_name(ctype));
    return -1;
  }

  if (client == NULL)
  {
    client = vigem_alloc();
    if (client == NULL)
    {
      gerror("vigem_alloc failed\n");
      return -1;
    }
    VIGEM_ERROR error = vigem_connect(client);
    if (!VIGEM_SUCCESS(error))
    {
      gerror(_("failed to connect to the ViGEmBus driver: %s (0x%08lx)\n"), vigem_error_str(error), (unsigned long) error);
      vigem_free(client);
      client = NULL;
      return -1;
    }
  }

  s_virtual_device * device = devices + adapter;

  memset(device, 0x00, sizeof(*device));
  device->rumble = -1;
  device->ctype = ctype;
  device->use_ds4_ex = 1;
  controller_init_report(ctype, &device->report[0].value);

  device->target = (ctype == C_TYPE_360_PAD) ? vigem_target_x360_alloc() : vigem_target_ds4_alloc();
  if (device->target == NULL)
  {
    gerror("vigem_target_alloc failed\n");
    return -1;
  }

  VIGEM_ERROR error = vigem_target_add(client, device->target);
  if (!VIGEM_SUCCESS(error))
  {
    gerror(_("failed to plug the virtual controller: %s (0x%08lx)\n"), vigem_error_str(error), (unsigned long) error);
    vigem_target_free(device->target);
    device->target = NULL;
    return -1;
  }

  ++nb_targets;

  if (ctype == C_TYPE_360_PAD)
  {
    error = vigem_target_x360_register_notification(client, device->target, x360_notification, device);
  }
  else
  {
    error = vigem_target_ds4_register_notification(client, device->target, ds4_notification, device);
  }
  if (!VIGEM_SUCCESS(error))
  {
    gwarn("failed to register for rumble notifications (0x%08lx)\n", (unsigned long) error);
  }

  ginfo(_("Virtual controller created: %s (ViGEmBus).\n"), controller_get_name(ctype));

  return 0;
}

static void forward_rumble(int adapter)
{
  LONG rumble = InterlockedExchange(&devices[adapter].rumble, -1);
  if (rumble < 0)
  {
    return;
  }
  int joystick = adapter_get_device(E_DEVICE_TYPE_JOYSTICK, adapter);
  if (joystick >= 0 && (ginput_joystick_get_haptic(joystick) & GE_HAPTIC_RUMBLE))
  {
    GE_Event event =
    {
      .jrumble =
      {
        .type = GE_JOYRUMBLE,
        .which = joystick,
        .weak = (rumble & 0xFF) << 8,
        .strong = ((rumble >> 8) & 0xFF) << 8
      }
    };
    ginput_queue_push(&event);
  }
}

int virtual_con_send(int adapter, const int axis[AXIS_MAX])
{
  if (adapter < 0 || adapter >= MAX_CONTROLLERS || devices[adapter].target == NULL)
  {
    return -1;
  }

  s_virtual_device * device = devices + adapter;
  VIGEM_ERROR error;

  int axes[AXIS_MAX];
  memcpy(axes, axis, sizeof(axes));
  unsigned int index = controller_build_report(device->ctype, axes, device->report);
  s_report * value = &device->report[index].value;

  if (device->ctype == C_TYPE_360_PAD)
  {
    // s_report_x360 has the layout of the xinput report, with sticks already oriented for xinput (y up)
    XUSB_REPORT report =
    {
      .wButtons = value->x360.buttons,
      .bLeftTrigger = value->x360.ltrigger,
      .bRightTrigger = value->x360.rtrigger,
      .sThumbLX = value->x360.xaxis,
      .sThumbLY = value->x360.yaxis,
      .sThumbRX = value->x360.zaxis,
      .sThumbRY = value->x360.taxis,
    };
    error = vigem_target_x360_update(client, device->target, report);
  }
  else
  {
    // DS4_REPORT_EX is the USB input report without the report id
    DS4_REPORT_EX report;
    memcpy(report.ReportBuffer, ((unsigned char *) &value->ds4) + 1, sizeof(report.ReportBuffer));
    error = VIGEM_ERROR_NOT_SUPPORTED;
    if (device->use_ds4_ex)
    {
      error = vigem_target_ds4_update_ex(client, device->target, report);
      if (error == VIGEM_ERROR_NOT_SUPPORTED)
      {
        gwarn("the ViGEmBus driver doesn't support extended DS4 reports, touchpad and motion are disabled (update the driver)\n");
        device->use_ds4_ex = 0;
      }
    }
    if (!device->use_ds4_ex)
    {
      // DS4_REPORT is the beginning of the extended report
      DS4_REPORT basic;
      memcpy(&basic, report.ReportBuffer, sizeof(basic));
      error = vigem_target_ds4_update(client, device->target, basic);
    }
  }

  forward_rumble(adapter);

  if (!VIGEM_SUCCESS(error))
  {
    gerror("failed to update the virtual controller (0x%08lx)\n", (unsigned long) error);
    return -1;
  }

  return 0;
}

void virtual_con_close(int adapter)
{
  if (adapter < 0 || adapter >= MAX_CONTROLLERS || devices[adapter].target == NULL)
  {
    return;
  }

  s_virtual_device * device = devices + adapter;

  if (device->ctype == C_TYPE_360_PAD)
  {
    vigem_target_x360_unregister_notification(device->target);
  }
  else
  {
    vigem_target_ds4_unregister_notification(device->target);
  }
  vigem_target_remove(client, device->target);
  vigem_target_free(device->target);
  device->target = NULL;

  if (--nb_targets == 0)
  {
    vigem_disconnect(client);
    vigem_free(client);
    client = NULL;
  }
}

#pragma GCC diagnostic pop

#else /* HAVE_VIGEM */

/*
 * Built without the ViGEmBus client library (shared/ViGEmClient).
 */

#include <connectors/virtual_con.h>
#include <gimx.h>
#include <gimxlog/include/glog.h>

int virtual_con_is_supported(e_controller_type ctype)
{
  return ctype == C_TYPE_360_PAD || ctype == C_TYPE_DS4;
}

int virtual_con_open(int adapter __attribute__((unused)), e_controller_type ctype __attribute__((unused)))
{
  gerror(_("this build of GIMX has no virtual controller support (it needs to be built with shared/ViGEmClient).\n"));
  return -1;
}

int virtual_con_send(int adapter __attribute__((unused)), const int axis[AXIS_MAX] __attribute__((unused)))
{
  return -1;
}

void virtual_con_close(int adapter __attribute__((unused)))
{
}

#endif /* HAVE_VIGEM */

#endif /* WIN32 */
