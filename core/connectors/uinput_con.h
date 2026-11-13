/*
 Copyright (c) 2026 GIMX contributors
 License: GPLv3
 */

#ifndef UINPUT_CON_H_
#define UINPUT_CON_H_

#include <gimxcontroller/include/defs.h>

/*
 * Software-only "adapter": instead of sending reports over a serial link to an
 * ATmega32U4-based USB adapter, GIMX creates a virtual game controller on the
 * local machine through the Linux uinput subsystem.
 *
 * This is only useful when the target of the controller is the computer running
 * GIMX itself (PC games, emulators, Steam, Remote Play / streaming clients...).
 * A PC has no USB device-mode port, so it cannot impersonate a controller towards
 * a console: the hardware adapter is still needed for that.
 */

#define UINPUT_CON_PORT "virtual"

int uinput_con_is_supported(e_controller_type ctype);
int uinput_con_open(int adapter, e_controller_type ctype);
int uinput_con_send(int adapter, const int axis[AXIS_MAX]);
void uinput_con_close(int adapter);

#endif /* UINPUT_CON_H_ */
