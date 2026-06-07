# Results Recycle Bin

This directory holds generated process artifacts that should not clutter the
main evidence folders or be included in functional commits.

Keep source evidence such as `.json`, `.rc`, `.stdout`, `.stderr`, and concise
logs in the run directory. Move disposable artifacts here with a manifest when
they are large, binary, or only useful for post-mortem recovery, for example:

- flash readback dumps
- downloaded binary logs
- stale `.pid` files
- temporary one-off process artifacts

Each recycle subdirectory should include a manifest with the original source
run, file size, and SHA256 so the artifact remains traceable.
