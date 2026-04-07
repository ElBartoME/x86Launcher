# x86Launcher

## What is it?

**x86Launcher** is graphical game browser and launcher for the IBM/PC compatibles running MS/PC/DR-DOS or equivalent. Originally written by megatron-uk. I just extended the code.

It runs on real DOS hardware **or** from within Dosbox, either emulated or real physical system; it is __not__ a Windows/Mac/Linux application like [launchbox](https://www.launchbox-app.com/) or [RetroArch](https://github.com/libretro/RetroArch).

**x86Launcher** is a 32-bit protected-mode application built with DJGPP and will work on any hardware compatible with the following:

   * Intel 386 or higher x86 processor (required for 32-bit protected mode / DPMI)
   * 640K of conventional memory, plus a small amount of extended memory for the DPMI host
   * A DPMI host (provided automatically by DOSBox; on real hardware use CWSDPMI or similar)
   * VGA card with 256KB memory and VESA 2.0 support with a Linear Frame Buffer (must support 640x400 @ 256 colours). Cards without a VESA 2.0 BIOS may work with UniVBE.

**x86Launcher** is the DOS equivalent of other projects:

   * [PC98Launcher](https://github.com/megatron-uk/pc98launcher) - A game browser and launcher for the NEC PC9801 series of computers
   * [X68KLauncher](https://github.com/megatron-uk/x68klauncher) - A game browser and launcher for the Sharp X68000 series of computers

## What does it do?

   * It creates a browseable list of all the game directories on your drive(s)
   * It can load and display additional metadata per game (i.e. developer, publisher, genre, release date etc)
   * If metadata is available, it can also do:
      * Search/Filter games by publisher or developer
      * Search/Filter by genre (e.g. FPS, Fighting, Adventure, Strategy)
      * Search/Filter by series (e.g. Ultima 1-8, the DOOM series, Advanced Dungeons and Dragons RPGs)
      * Search/Filter by tech/hardware requirements (e.g. CPU class, sound card, video standard)
      * Load and display screenshots or artwork per game (i.e. box art, screenshots, etc)
      * Play back **FLI/FLC animations** as in-app video previews for a game
      * Play back **WAV audio** via Sound Blaster while browsing game artwork
   * It can export an audit file of all the found games
   * It remembers the last selected game between sessions
   * It can launch any game for which a start file is either found automatically or defined in metadata, supporting **multiple named launch options** per game (e.g. "Start Game", "Setup", "Intro")
   * For games without a metadata file it will **automatically scan and present a list of all launchable executables** in the game directory

## Sample Use

The image below takes you to a sample movie of the launcher application in action via **YouTube**:

TBD

*Click the video above to see the launcher in action*

## What does it look like?

Starting up and scanning for games:

TBD

Main user interface:

TBD

Browsing game artwork:

TBD

TBD

----

## How do I run it?

You will need a system that meets these requirements:

   * An IBM/PC or compatible with a **386 or higher** x86 processor (32-bit protected mode is required)
   * 640K of conventional memory, plus a small amount of extended memory for the DPMI host
   * A DPMI host — DOSBox provides one automatically; on real hardware use CWSDPMI or a similar provider
   * A hard drive (technically it can run from floppy, but it wouldn't be practical!)
   * A VGA graphics card with **VESA 2.0** support and a Linear Frame Buffer, capable of 640x400 at 256 colours. This requires a minimum of 256KB of video memory. Cards without a native VESA 2.0 BIOS may work with UniVBE.
   * A Sound Blaster or compatible card is optional but required for WAV audio playback

You then need to copy the zipped binaries (see below) to a location on your hard drive and unzip them to a directory of your choice.

The directory should have the following after unzipping:

   * launcher.exe
   * launcher.ini
   * l.bat
   * assets\font8x16.bmp
   * assets\font8x8.bmp
   * assets\logo.bmp
   * assets\light\\*.bmp

You don't need to set anything in config.sys or autoexec.bat.

Edit launcher.ini with a normal text editor and set the `gamedirs` variable to all of the locations of games on your hard drive(s).

The following settings are available for launcher.ini:

   * `gamedirs=C:\Path1,C:\Path2,D:\Path3` — List the directories which contain your game subdirectories
   * `verbose=0|1` — Enable text mode logging for troubleshooting purposes
   * `savedirs=0|1` — Save the scraped list of games to a text file at start
   * `preload_names=0|1` — For each found game, attempt to load the metadata file to get its real name. This will slow initial scraping down.
   * `keyboard_test=0|1` — Before starting the UI, prompt the user to do a quick input test
   * `timers=0|1` — Enable internal performance timing of function calls (for debugging)

The following settings are available under a `[display]` section in launcher.ini:

   * `hsync_shift=N` — Shift the display horizontally by N pixels; useful on real hardware where the image is off-centre

The following settings are available under a `[sound]` section in launcher.ini:

   * `volume=0..256` — Set the Sound Blaster output volume (default: 256, i.e. full volume)

If you have your games under folders such as `C:\Games\Arkanoid` and `C:\Games\Dark` for example, then you only need to add the path `C:\Games`. You may add up to 16 comma-separated game paths, and these can be for different drives if you wish.

<u>Just run `l.bat` to start the application.</u>


#### Binaries

The latest pre-compiled binaries as well as pre-made metadata and screenshots can be found on my website under the [Tech Stuff](TBD) wiki:

   * [www.target-earth.net - Tech Stuff wiki](TBD)

----

## How to build

The code for x86Launcher is C built using **DJGPP** (the DOS port of GCC), targeting 32-bit protected mode via DPMI. The build system is **CMake** with the **Ninja** generator. Open Watcom is no longer used.

### Setting up Visual Studio Code on Windows

The repository already includes a `.vscode` folder with the necessary configuration. The easiest way to get a working build environment is via the **dos-dev** VS Code extension by badlogic, which installs all required tools automatically.

#### 1. Install prerequisites

You only need two things installed before starting:

- [Visual Studio Code](https://code.visualstudio.com/)
- [CMake](https://cmake.org/download/) — ensure it is added to your system `PATH` during installation

#### 2. Install the dos-dev extension

Install the **dos-dev** extension (`badlogicgames.dos-dev`) from the VS Code marketplace. The first time you use it, it will automatically download and install everything else needed into `%USERPROFILE%\.dos`:

- DJGPP (the DJGPP cross-compiler)
- Ninja (the build tool)
- DOSBox-X (for running and debugging the result)
- A CMake toolchain file for DJGPP (`toolchain-djgpp.cmake`)

This is a one-time setup that works across all your DOS projects.

#### 3. Open the project and build

Open the x86Launcher folder in VS Code. When prompted, select the **djgpp** kit. CMake will configure the project automatically using the included `CMakeLists.txt`.

To build, use the CMake Tools status bar at the bottom of VS Code or run the build task. The compiled output will appear in the `build/` directory as `main.exe`. Copy this to your DOS environment alongside `l.bat` and the `assets\` folder, renaming it to `launcher.exe`.

#### 4. Running and debugging

The dos-dev extension also provides launch configurations for running and debugging directly inside DOSBox-X from within VS Code, including source-level debugging via GDB. The `.vscode` folder in the repository already has these configured.

The compiled `launcher.exe` is a DPMI executable and must run under DOS or DOSBox — it cannot be executed directly on Windows.


----

## Adding metadata & images


### Metadata files

You can add additional metadata to your game by placing a text file in each game directory named `launch.dat`.

The contents of this file is as follows:

```
[default]
name=
developer=
publisher=
midi_mpu=
midi_serial=
year=
genre=
images=
video=
audio=
series=
start=
```

The fields `name`, `developer`, `publisher`, `genre` and `series` are all simple text, maximum of 32 characters. `name` can be up to 44 characters.

The fields `midi_mpu` and `midi_serial` are integers, where __1__ means supported and any other value means unsupported.

The `images` field is a comma-separated list of the artwork available. The path is relative to the game directory, so `image01.bmp` will be expanded to include the drive and path that the game is located under (`C:\Games\gamename\image01.bmp`, for example). Up to 16 images may be listed.

Images will be shown in the order they are listed, so place the image you want shown by default as the first item in the list.

The `series` field is a text name of the larger game series in which the game is based, useful for those games in which there are more than one title (Doom and Doom II, for example). You can use the __filter__ option within the application to find all games within the same series, as long as they are tagged with the correct metadata.

The `genre` field notes the type of gameplay within the game (RPG, Action, Sports, Puzzle, etc). As with `series` you can use the application __filter__ facility to restrict the display of games to just one genre if desired.

#### Video previews (FLI/FLC)

The `video` field names a single FLI or FLC animation file (e.g. `intro.fli`) stored in the game directory. When the artwork pane is focused and a video file is available, x86Launcher will play it back directly in the artwork window at up to 208 colours, leaving the UI palette intact. Playback can be interrupted with any key. Both the original FLI format (320×200) and the more flexible FLC format (any resolution) are supported.

#### Audio previews (WAV)

The `audio` field names a single uncompressed WAV file (e.g. `theme.wav`) stored in the game directory. If a Sound Blaster compatible card is detected, the file will be streamed via 8-bit DMA while you browse screenshots or watch a video preview. Volume is controlled by the `volume` setting in `launcher.ini`.

#### Multiple launch options

The `start` field now accepts a comma-separated list of up to 8 executables, each with an optional display label in square brackets:

```
start=DUKE3D.EXE[Start Game],SETUP.EXE[Configure Sound],INTRO.EXE[Watch Intro]
```

When the game is launched, a popup menu presents each labelled option for the user to choose from. The legacy single `start=` and `alt_start=` format is still fully supported.

#### Executable auto-detection

If a game directory contains no `launch.dat` file, x86Launcher will automatically scan the directory (and one level of subdirectories) for `.EXE`, `.COM` and `.BAT` files and present them in a scrollable picker popup, so you can still launch the game without writing a metadata file first.

An example is shown below:

```
[default]
name=Duke Nukem 3D
developer=3D Realms
publisher=GT Interactive
midi_mpu=1
midi_serial=1
year=1996
genre=FPS
images=cover.bmp,screen1.bmp,screen2.bmp,box.bmp
video=intro.fli
audio=theme.wav
series=Duke Nukem
start=DUKE3D.EXE[Start Game],SETUP.EXE[Configure Sound]
```

You could then browse to "Duke Nukem 3D" in the game list, search for it by games published by "GT Interactive", developed by "3D Realms", or all titles in the series "Duke Nukem".

#### In-app metadata editor

If a game already has a `launch.dat` file, you can edit the core metadata fields (name, developer, publisher, genre, year, series) directly from inside the launcher without leaving DOS. Press the designated key while a game is selected to open the editor pane; changes are saved back to `launch.dat` automatically.

----

#### Supplementary Metadata

The metadata file can have several other sections added to it, which are entirely optional:

`[sound]`, `[video]` and `[cpu]`.

##### Sound Metadata

The sound metadata block can have zero, one or more of the following entries:

```
[sound]
beeper=
adlib=
soundblaster=
mt32=
gm=
tandy=
ultrasound=
disney=
covox=
```

All entries are set to either 0 or 1 to indicate support for that particular audio device.

##### Video Metadata

The video metadata block can have zero, one or more of the following entries:

```
[video]
text=
cga=
ega=
vga=
tandy=
hercules=
svga=
```

All entries are set to either 0 or 1 to indicate support for that particular video display device.

##### CPU Metadata

The CPU metadata block can have zero, one or more of the following entries:

```
[cpu]
xms=
ems=
dpmi=
8086=
286=
386=
486=
586=
```

All entries are set to either 0 or 1.

For **EMS** and **XMS** a setting of 1 indicates that the title requires DOS memory of that particular type.

For **DPMI** a setting of 1 indicates that the title is a 32bit protected mode game *requiring* a 386 or above in order to run.

For the various x86 CPU types, a setting of 1 indicates that the title will *run adequately* on a processor of that type.

You can use the **Filter by Tech** option in the application to filter the game list by any of the hardware flags above — for example, to show only games that support Adlib sound, or only titles that run on an 8086.

----

### Converting images to useable BMP files

The BMP files used by the application need to be 8bpp but of a limited palette so that the user interface does not suffer from colour issues (approximately 48 colours are reserved by the user interface and fonts).

You can use any image processing application you want, but it must output images of the following specifications:

   * BMP
   * Uncompressed
   * 8bpp, indexed/paletted colour
   * Maximum of 208 colours
   * No larger than 320x200 (but they may be smaller in either dimension, if desirable, e.g. for vertical box art)

If you have the [ImageMagick](https://www.imagemagick.org/) tools available on your system, you can batch convert files using the following syntax:

```
convert INPUT.JPG -resize 320x200 -depth 8 -colors 208 -alpha OFF -compress none BMP3:OUTPUT.BMP
```

**Note:** *If you do not reduce the number of active colours in use in your screenshots and box art, the images will still show, but they may display incorrectly.*

#### Converting video previews to FLI/FLC

FLI/FLC files must be 8bpp and use no more than 208 colours (the remaining palette entries are reserved for the UI). Tools such as [Aseprite](https://www.aseprite.org/) or [FFmpeg](https://ffmpeg.org/) with an appropriate encoder can produce FLI/FLC output. Keep animations at or below 320×200 for best results on period hardware.

#### Automating Image Conversion

A script to automate the image conversion is included in the `scripts/` directory.

----

### Preparing video previews (FLI)

Two scripts in the `scripts/` directory handle FLI creation. Both require [Python](https://www.python.org/) and the [Pillow](https://pillow.readthedocs.io/) library (`pip install pillow`).

#### make_preview.py — full pipeline from a video file

This is the recommended starting point. It takes any video file that ffmpeg can read, extracts a clip, automatically removes black borders, quantizes to 208 colours, and produces a ready-to-use FLI file in one step. It requires [ffmpeg](https://ffmpeg.org/) to be available on your `PATH`.

```
python make_preview.py <input_video> <output.fli> <start_time> <duration> [fps]
```

```
python make_preview.py gameplay.mp4 preview.fli 00:02:30 00:00:12
python make_preview.py gameplay.mp4 preview.fli 00:02:30 00:00:12 15
```

The default frame rate is 15fps. The script automatically detects and crops black borders, builds a globally consistent 208-colour palette across all frames, and remaps any pixel that would fall into palette entry 0 (reserved by the UI) to entry 1, which is forced to the same colour so the result is visually identical.

#### make_fli.py — convert a folder of PNG frames

A lower-level tool for cases where you have already extracted frames yourself (e.g. via ffmpeg or another tool). Frames must be named `frame0001.png`, `frame0002.png`, etc., which is the default ffmpeg naming convention.

```
python make_fli.py <frames_folder> <output.fli> [fps]
```

```
python make_fli.py frames/ preview.fli 15
```

`make_preview.py` calls this script internally, so you only need to use it directly if you want to work with pre-existing frames.

----

### Preparing audio themes (WAV)

Two scripts in the `scripts/` directory handle WAV creation. Both require Python. The pipeline scripts also require ffmpeg on your `PATH`.

The Sound Blaster playback in x86Launcher expects **8-bit unsigned PCM, 22050Hz, mono** WAV files. The scripts produce exactly this format.

#### make_theme.py — full pipeline from any audio file

This is the recommended starting point. It takes any audio file that ffmpeg can read, extracts a clip at the correct sample rate and bit depth, and applies a fade in and fade out.

```
python make_theme.py <input_audio> <output.wav> <start_time> <duration> [fade_in_ms] [fade_out_ms]
```

```
python make_theme.py theme.mp3 theme.wav 00:00:10 00:00:25
python make_theme.py theme.mp3 theme.wav 00:00:10 00:00:25 500 2000
python make_theme.py soundtrack.ogg theme.wav 00:01:30 00:00:20 0 1500
```

The default fade in is 500ms and the default fade out is 1500ms. Times can be given in `HH:MM:SS` format or as plain seconds.

#### fade_wav.py — add fades to an existing WAV file

A lower-level tool that adds a fade in and fade out to an already-converted WAV file. Supports both 8-bit unsigned and 16-bit signed PCM, mono or stereo.

```
python fade_wav.py <input.wav> <output.wav> [fade_in_ms] [fade_out_ms]
```

```
python fade_wav.py theme_raw.wav theme.wav 500 1500
```

`make_theme.py` calls this script internally, so you only need to use it directly if you have a WAV file you want to add fades to without going through the full ffmpeg pipeline.