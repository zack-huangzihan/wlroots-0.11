The DRM subsystem has a module parameter affecting the amount of debug output it prints.
For several kinds of DRM errors, this is the only way we can find out why something failed, especially if it's `Invalid Argument`. If this appears as a DRM error in the wlroots log, please include this debug log in any issues.

```sh
echo 0x19F | sudo tee /sys/module/drm/parameters/debug # Enable verbose DRM logging
sudo dmesg -C # Clear kernel logs
dmesg -w >dmesg.log & # Continuously write DRM logs to a file, in the background
sway -d >sway.log 2>&1 # Reproduce the bug, then exit sway
fg # Kill dmesg with Ctrl+C
echo 0 | sudo tee /sys/module/drm/parameters/debug # Disable DRM logging
```

Replace sway with whatever program/compositor you're testing. Please note that the DRM debugging log is **EXTREMELY** verbose. Reproduce your issue as quickly as you can, so we don't have to sift through potentially hundreds of thousands of log messages.

Please attach the whole log file to your bug reports. The interesting part of the log will _often_ be right before a line which looks like:

```
[ 2079.295837] [drm:drm_atomic_check_only [drm]] atomic driver check for 000000001dac41bb failed: -28
```

### DRM logging bitmask

The number provided as the `debug` parameter is a bitmask of logging categories to enable. The command `modinfo -p drm` can print the possible values. As of Linux 5.13, the values are:

```
Enable debug output, where each bit enables a debug category.
		Bit 0 (0x01)  will enable CORE messages (drm core code)
		Bit 1 (0x02)  will enable DRIVER messages (drm controller code)
		Bit 2 (0x04)  will enable KMS messages (modesetting code)
		Bit 3 (0x08)  will enable PRIME messages (prime code)
		Bit 4 (0x10)  will enable ATOMIC messages (atomic code)
		Bit 5 (0x20)  will enable VBL messages (vblank code)
		Bit 7 (0x80)  will enable LEASE messages (leasing code)
		Bit 8 (0x100) will enable DP messages (displayport code)
```

### Force-probing connectors

If a connector's status is "unknown" or stuck in a bad state, you can try force-probing it:

```
echo detect | sudo tee /sys/class/drm/card0-DP-1/status
```