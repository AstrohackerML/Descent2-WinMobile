D2Engine: Descent II for Windows 10 Mobile
==========================================

D2Engine is a source port of the original DOS/Windows game "Descent II" for Windows 10 Mobile phones (UWP, ARM), made on and for the HP Elite x3. It is based on the [Android port](https://github.com/AstrohackerML/Descent2-Mobile). You will need the game files from a licensed copy of Descent II in order to play.

What works: the full game with touch controls, the on-screen keyboard for pilot and save names, sound effects, movies with sound and MIDI music. The same app also runs on a Windows 10/11 PC (x64) with the keyboard and the mouse.

Not yet: Redbook (CD) music, the gyroscope, multiplayer.

Licence
-------
GPLv3, with an exception for the original Parallax code, which may be used for non-commercial purposes only. Please see "COPYING" and "GPL-3".

D2Engine is an unofficial project, not affiliated with Interplay or Parallax Software. Descent is a trademark of Interplay Entertainment; the name is used here only to say which game the engine plays.

Installing
----------
Download the latest release from the [Releases](../../releases) page and unpack it. It contains:

- `Descent2_<version>_ARM.appx` – the game
- `Descent2.cer` – the certificate the package is signed with
- `Dependencies/` – `Microsoft.VCLibs.ARM.14.00.appx` (the C++ runtime) and `MidiVoices.appx` (the General MIDI sound set for the music)

### On the phone

1. Turn on developer mode: **Settings → Update & security → For developers → Developer mode**.
2. Copy the unpacked folder to the phone (over USB, to the phone storage or the SD card).
3. In **File Explorer**, open `Descent2.cer` and install the certificate.
4. Open `Microsoft.VCLibs.ARM.14.00.appx` and `MidiVoices.appx` from `Dependencies` and install them. If the phone says one is already installed, skip it.
5. Open `Descent2_<version>_ARM.appx` and install it.

### From a PC

With the phone connected over USB and developer mode on, `WinAppDeployCmd` from the Windows 10 SDK can install it (install the certificate on the phone first, as above):

    WinAppDeployCmd install -file Dependencies\MidiVoices.appx -ip 127.0.0.1
    WinAppDeployCmd install -file Descent2_<version>_ARM.appx -ip 127.0.0.1 -dependency Dependencies\Microsoft.VCLibs.ARM.14.00.appx

Required Game Data
------------------
The app does not contain any game data. Only the PC data files are supported. The following files are needed:

- alien1.pig
- alien2.pig
- descent2.ham
- descent2.hog
- descent2.s22 -or- descent2.s11
- fire.pig
- groupa.pig
- ice.pig
- water.pig

The movies (`intro-h.mvl` or `intro-l.mvl`, `other-h.mvl` or `other-l.mvl`, `robots-h.mvl` or `robots-l.mvl`) are optional.

Copy the files to a folder on the phone. On first launch the game asks for that folder (subfolders are searched too) and copies the data files into its own storage, lowercasing the names. If files are missing, it lists them and lets you pick another folder; files already copied are kept, so the data can come from more than one folder.

Controls
--------
The touch controls are those of the Android port. Taps outside the controls act as the mouse in menus and briefings (pick **Mouse** as the control type for a new pilot so taps also skip briefing pages). The hardware Back button acts as Escape.

Building
--------
The project is `Descent2-WinMobile.sln` with the UWP app in `winmobile/`. It needs:

- Visual Studio 2017 with the Universal Windows Platform workload and the **VC++ 2017 version 15.7 v14.14 toolset**: the last one with a 32-bit ARM compiler. The project pins it with `VCToolsVersion`.
- Windows 10 SDK 10.0.19041 with the **Microsoft General MIDI DLS for Universal Windows Apps** extension SDK.

Packages are signed with `winmobile/Descent2_TemporaryKey.pfx`, which is not part of the repository. Make your own self-signed one with the publisher `CN=AstrohackerML` (the one in `Package.appxmanifest`), or change the publisher to match yours.

    MSBuild Descent2-WinMobile.sln -p:Configuration=Release -p:Platform=ARM -p:UapAppxPackageBuildMode=SideloadOnly -p:AppxBundle=Never

The package and its dependencies end up in `AppPackages/Descent2/`.

### Store build

The Microsoft Store version is called **D2Engine** and has its own manifest, `winmobile/Package.Store.appxmanifest`, with the Partner Center identity. `StoreBuild=true` switches to it and to the Store signing key `winmobile/D2Engine_StoreKey.pfx` (not in the repository; its subject must be the Partner Center publisher), and builds in separate folders:

    MSBuild Descent2-WinMobile.sln -p:Configuration=Release -p:Platform=ARM -p:StoreBuild=true -p:UapAppxPackageBuildMode=StoreUpload -p:AppxBundle=Always "-p:AppxBundlePlatforms=ARM|x64" -p:AppxPackageDir=<repo>\AppPackages\Store\

This writes the `.appxupload` for Partner Center to `AppPackages/Store/`, with the phone (ARM) and PC (x64) packages. The store listing texts and submission notes are in `store/listing.md`.

### On a PC

The x64 build runs on Windows 10 and 11 PCs. There the game takes the keyboard and the mouse as the original did: keys by their position (scan codes), the mouse steers while flying (pick **Mouse** in **Options → Controls**) and its buttons fire. The touch controls show up only after the screen is touched, and a narrow window does not turn the picture.

### How it is put together

- The game renders with the OpenGL ES 1.x subset of the Android port. `winmobile/gles_d3d11.cpp` emulates it on Direct3D 11 and turns the frame to the phone's portrait panel itself.
- The Android sound code (`main/android/digi.c`, movie audio in `libmve/mveplay.c`) runs unchanged on an OpenSL ES subset written on XAudio2 (`winmobile/opensl_xaudio2.cpp`).
- Music: the game's HMP songs are converted to MIDI and played on the Windows General MIDI synthesizer (`winmobile/music.cpp`).
- `winmobile/App.cpp` is the app shell: window, touch, keyboard, data setup.

Issues and Limitations
----------------------
Please see the "Issues" section in GitHub.

Special Thanks
--------------
- Devin Tuchsen
