The wlroots build system defaults are optimized for developer builds. Packagers may want to tweak it:

- Set `-Dbuildtype`/`-Doptimization` to your distribution's preferred settings (some distribution-specific Meson wrappers already do this automatically).
- Set `-Dwerror=false` to avoid failing the build on warnings. This ensures warnings introduced in newer compiler releases and additions to other libraries don't break your builds. Bug reports and patches to fix warnings are still welcome.
- Set `-Dauto_features=enabled`, then opt-out of the features you don't want. This ensures Meson will tell you when a dependency is missing, instead of silently disabling features.
- Avoid disabling assertions if possible (ie. don't set `-Db_ndebug=true`). The performance gain is minimal and it makes bugs much harder to track down.
- Set `--default-library=static` and link compositors statically against wlroots, if your distribution's policy allows it. wlroots releases often contain breaking changes and compositors aren't updated all at the same time.
