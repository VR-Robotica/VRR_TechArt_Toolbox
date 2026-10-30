# Making the Fab upload zips

Maintainer notes; this folder is not part of the Fab package.

`fab_zip.py` builds the Fab upload zip for one engine version from the last commit (commit first: uncommitted
changes are left out). Run it from the plugin folder with Python 3:

```
python Scripts/fab_zip.py
python Scripts/fab_zip.py 5.7
python Scripts/fab_zip.py 5.7 5.8
python Scripts/fab_zip.py 5.7 D:/Builds/TechArtToolbox_5.7.zip
```

With no version it makes a zip for every version in `VERSIONS` at the top of the script (5.7, 5.8; 5.6 lacks Control Rig and static mesh APIs the plugin uses); add a
version there once the plugin builds and loads on it. Each zip is written to the desktop as
`TechArtToolbox_<version>.zip` unless a path is given (one version only), and holds one
`TechArtToolbox/` folder with the uplugin, README, LICENSE, Config, Extras, Resources and Source (no Binaries, Intermediate or
`Scripts/`), the uplugin's `EngineVersion` set to `<version>.0`. The script stops with an error if a path uses
backslashes, build output got in, or a source file is missing its copyright line. Submit one zip per engine version.
