## Integrating wlroots with your build

wlroots ships with a pkg-config entry named wlroots, which should be compatible with most build systems. Compile and install wlroots normally, then link to `pkg-config --libs wlroots` and add `pkg-config --cflags wlroots` to your compiler flags (or whatever pretty way of doing this your particular build system encourages).

You also need to link to libwayland and wire up `wayland-scanner` to scan any protocols you want to use. wlroots provides, for example, and xdg-shell implementation, but it expects you to call `wayland-scanner` to generate the header before you include `wlr/types/wlr_xdg_shell.h`. Our naming convention for Wayland protocol headers is to replace any dashes `-` with underscores `_` in the protocol name, then replace `.xml` with `_server.h`. You can get these XML files from `wayland-protocols` (usually you want to reference the path specified by `pkg-config --variable=pkgdatadir wayland-protocols` rather than copying these into your source tree), and for other protocols from [`wlr-protocols`](https://gitlab.freedesktop.org/wlroots/wlr-protocols).

## Unstable features

We're still working on stabilizing the interface, and most interfaces are subject to change (though radical change is unlikely at this point). To this end, you need to pass `-DWLR_USE_UNSTABLE` to your compiler to get anything serious done.

## Documentation

wlroots is largely documented through comments in the headers. Read 'em. Also check out [tinywl](https://gitlab.freedesktop.org/wlroots/wlroots/-/tree/master/tinywl).

## Support

We hang out in [#sway-devel](https://web.libera.chat/gamja/?channels=#sway-devel), an IRC channel on [Libera Chat](https://libera.chat). Come in and ask questions.

## Additional resources

* [The Wayland Protocol (book)](https://wayland-book.com)
* [tinywl: a small Wayland compositor written with wlroots (annotated)](https://github.com/swaywm/wlroots/tree/master/tinywl)
* [Writing a Wayland Compositor, Part 1: Hello wlroots](https://drewdevault.com/2018/02/17/Writing-a-Wayland-compositor-1.html)
* [Writing a Wayland Compositor, Part 2: Rigging up the server](https://drewdevault.com/2018/02/22/Writing-a-wayland-compositor-part-2.html)
* [Writing a Wayland Compositor, Part 3: Rendering a window](https://drewdevault.com/2018/02/28/Writing-a-wayland-compositor-part-3.html)
* [Input handling in wlroots](https://drewdevault.com/2018/07/17/Input-handling-in-wlroots.html)
* [Writing a Wayland compositor with wlroots: shells](https://drewdevault.com/2018/07/29/Wayland-shells.html)
* [Introduction to damage tracking](https://emersion.fr/blog/2019/intro-to-damage-tracking/)

You are also encouraged to read the code of [other projects using wlroots](Projects-which-use-wlroots).