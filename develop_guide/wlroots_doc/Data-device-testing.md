Wayland's "data device" manages selections used for clipboard, drag-and-drop, and primary selection. This page contains tips to test this functionality.

## Test clients

* GTK3, Wayland-native
  * gedit: for simple plain-text
  * Epiphany: for copying rich text and images
  * LibreOffice Writer (lowriter): for pasting rich text and images
* X11
  * Firefox: for copying rich text and images
  * Gimp (GTK2): for pasting images
  * Chromium: for copying rich text and images
* CLI tools: useful to inspect/debug the selection contents
  * wl-clipboard: Wayland-native, `wl-paste -l` to list MIME types, `wl-paste -t <type>` to paste
  * xclip: X11 (must be run from an X11 terminal emulator like xterm), `xclip -selection clipboard -o -t TARGETS` ti list MIME types, `xclip -selection clipboard -o [-t <type>]` to paste

## References

* Wayland
  * https://emersion.fr/blog/2020/wayland-clipboard-drag-and-drop/
* X11
  * https://tronche.com/gui/x/icccm/sec-2.html
  * https://www.freedesktop.org/wiki/Specifications/XDND/
  * https://johnlindal.wixsite.com/xdnd
  * http://www.call-with-current-continuation.org/rants/icccm.txt