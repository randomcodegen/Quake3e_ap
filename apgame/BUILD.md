# Windows x64 build and install

Install vcpkg and its `jansson`, `libwebsockets`, and `glib` dependencies as
described in the repository root. The checked-in libwebsockets overlay enables
WebSocket extensions and zlib.

From PowerShell:

```powershell
.\apgame\tools\build.ps1 -VcpkgRoot C:\path\to\vcpkg
.\apgame\tools\install.ps1 -GameRoot C:\path\to\Quake3
```

The installer validates the retail `baseq3\pak0.pk3`, never writes inside
`baseq3`, and creates `q3ap-launch.cmd`. To restore files displaced by the
installer:

```powershell
.\apgame\tools\install.ps1 -GameRoot C:\path\to\Quake3 -Restore
```

The retail 1.32 point-release PK3 files must already be installed by the user.
They are not part of this project or bundle.
