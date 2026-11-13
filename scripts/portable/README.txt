GIMX portable bundle for Linux
==============================

Runs from this folder, nothing is installed system-wide except the permissions below.
Built and tested for Fedora, should work on most recent x86_64 distributions.

First run
---------

1. Run the setup script once, as your normal user (it asks for your password when needed):

     ./setup.sh

   It installs missing runtime packages, allows virtual controllers (/dev/uinput), adds you to
   the "input" and "dialout" groups and adds GIMX to your applications menu.

2. Log out and back in, so that the group changes apply.

3. Start GIMX from the applications menu, or run:

     ./bin/gimx-launcher

Programs
--------

  bin/gimx-launcher    the main window
  bin/gimx             the command line program
  bin/gimx-config      the configuration editor
  bin/gimx-fpsconfig   the FPS configuration editor
  bin/gimx-loader      flashes the firmware of a GIMX hardware adapter

Always start them through bin/, the wrappers there point them to the bundled libraries.

Virtual controller (no hardware adapter)
----------------------------------------

To play PC games with GIMX, no adapter is needed: in the launcher, pick the output
"Virtual controller (no adapter)" and a type (360pad or DS4). From the command line:

  ./bin/gimx --config "My config.xml" --type 360pad --port virtual

Notes
-----

- Configurations are stored in ~/.gimx, as with an installed GIMX.
- GIMX is installed setuid root by a regular "make install"; the portable bundle is not.
  Everything works without it, except:
    . Bluetooth outputs (PS3/PS4 over Bluetooth), which need root,
    . real-time scheduling: GIMX prints "failed to set process priority" and runs at normal priority.
- To move the bundle to another folder, run ./setup.sh again to update the menu entry.
- To undo the setup: remove /etc/udev/rules.d/60-gimx-uinput.rules,
  /etc/modules-load.d/gimx-uinput.conf and ~/.local/share/applications/gimx-launcher.desktop,
  and remove yourself from the groups with: sudo gpasswd -d $USER input
