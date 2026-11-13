GIMX
====

[![Build Status](https://travis-ci.com/matlo/GIMX.svg?branch=master)](https://travis-ci.com/matlo/GIMX)

GIMX is a free software that allows to use a computer as a hub for your gaming devices. It works on Windows® and GNU/Linux platforms. It is compatible with Playstation® and Xbox® gaming consoles. The connection between the computer and the gaming console is performed using a USB adapter – [get one on the GIMX shop!](https://blog.gimx.fr/product/gimx-adapter/) – or a Bluetooth® dongle (PS3/PS4 only). The capabilities depend on the platform, the connection method, and the gaming platform.

Links:
* [Documentation](https://wiki.gimx.fr)  
* [Source code](https://gimx.fr/source)  
* [Issue tracker](https://gimx.fr/buglist)  
* Licence: [GPLv3](https://www.gnu.org/copyleft/gpl.html)  
* [Donations](https://blog.gimx.fr/give/gimx-donations-current/)

Software-only mode (no adapter, Linux)
--------------------------------------

When the controller is meant for the computer GIMX runs on (PC games, emulators, Steam,
Remote Play / streaming clients), the USB adapter is not needed: GIMX can create a virtual
controller on the local machine through the Linux `uinput` subsystem.

```
gimx --config "My config.xml" --type 360pad --port virtual   # Xbox 360 pad (default type)
gimx --config "My config.xml" --type DS4 --port virtual      # DualShock 4
```

In gimx-launcher, select the output **Virtual controller (no adapter)** and pick the type.

* `360pad` exposes the evdev layout of the `xpad` driver with the 045e:028e ids, so SDL, Steam,
  Wine/Proton and most games treat it as a regular Xbox 360 controller.
* `DS4` exposes the layout of the `hid-playstation` driver with the 054c:05c4 ids. The touchpad
  and motion sensors are not emulated.
* Rumble requested by the game on the virtual controller is forwarded to the physical joystick
  bound to it in the config, if that joystick supports rumble.

GIMX needs write access to `/dev/uinput`. Load the module and allow the `input` group to use it:

```
sudo modprobe uinput
echo uinput | sudo tee /etc/modules-load.d/uinput.conf
echo 'KERNEL=="uinput", MODE="0660", GROUP="input", OPTIONS+="static_node=uinput"' | sudo tee /etc/udev/rules.d/60-gimx-uinput.rules
sudo udevadm control --reload-rules && sudo udevadm trigger
sudo usermod -aG input $USER   # then log out and back in
```

This mode cannot replace the adapter for a console: a PC has no USB device-mode port, so it
cannot present itself to a PlayStation or Xbox as a controller.
