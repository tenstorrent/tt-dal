# tt-kmd

The kernel driver's IOCTL definitions, vendored from [tt-kmd] with a
platform fence for non-Linux builds. The pinned upstream revision is recorded
in [VENDORED](VENDORED).

[tt-kmd]: https://github.com/tenstorrent/tt-kmd

## License

This header is dual-licensed under both `GPL-2.0-only WITH Linux-syscall-note`
and [Apache License 2.0](/LICENSE). You have permission to use this code under
the conditions of either license pursuant to the rights granted by the chosen
license. See [NOTICE](/NOTICE).

Tenstorrent authored this header and holds its copyright. It was originally
released to Linux under the GPL, which is why it carries a license notice
differing from the Apache-2.0 license of this project.

> [!IMPORTANT]
>
> Changes made to this header on the Linux side must not be synced here. They
> would arrive under the GPL alone, and the file would no longer be ours to
> re-release under Apache-2.0.
