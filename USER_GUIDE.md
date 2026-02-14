# ntrak User Guide

Welcome to **ntrak**, a tracker for creating authentic SNES (Super Nintendo Entertainment System) music. This guide is written for beginners and focuses on practical, step-by-step use.

## Table of Contents

1. [Before You Start](#before-you-start)
2. [What is ntrak?](#what-is-ntrak)
3. [Getting Started](#getting-started)
4. [Understanding the Interface](#understanding-the-interface)
5. [Creating Your First Song](#creating-your-first-song)
6. [Working with Patterns](#working-with-patterns)
7. [Managing Instruments and Samples](#managing-instruments-and-samples)
8. [Building Song Structure](#building-song-structure)
9. [Effects and Commands](#effects-and-commands)
10. [Playback and Testing](#playback-and-testing)
11. [Memory Management](#memory-management)
12. [Advanced Features](#advanced-features)
13. [Tips and Best Practices](#tips-and-best-practices)

---

## Before You Start

You do not need deep SNES or tracker knowledge to use this guide.

- **Scope**: ntrak is built for the **N-SPC format family** (and N-SPC-derived engines), not for every possible SNES music engine.
- **Hex values**: Many fields use hexadecimal (base-16) numbers such as `00`, `7F`, and `FF`.
- **Shortcut format**: `Ctrl+S` means hold `Ctrl` and press `S`.
- **Menu format**: Paths like `File -> Import SPC...` mean open the first menu, then pick the listed item.
- **Learning order**: If you are new, read this guide in order through **Creating Your First Song**.

---

## What is ntrak?

**ntrak** is a music tracker for composing songs that play back like real SNES music. It targets the **N-SPC command/data model** used by many Nintendo games and some games from other studios, so what you hear in the editor is close to hardware behavior for that engine family.

### What ntrak Gives You

- **Engine Compatibility**: Create music for different SNES game engines (Super Metroid, The Legend of Zelda: A Link to the Past, Super Mario World 2: Yoshi's Island, Super Mario World)
- **Full SNES Audio Support**: Access all 8 audio channels with effects like vibrato, echo, and tremolo
- **Real-time Playback**: Hear your music exactly as it would sound on actual SNES hardware
- **Memory Visualization**: See how your music uses the SNES's limited 64KB audio memory
- **Modern Interface**: A dockable panel layout that still follows tracker-style workflows

### Who is ntrak For?

- Game developers creating SNES-style games
- Chiptune musicians and composers
- ROM hackers working on SNES game mods
- Anyone interested in retro music production

---

## Getting Started

### First Launch

When you first launch ntrak, you'll see several panels arranged on the screen:

- **Pattern Editor**: The main workspace where you compose music
- **Control Panel**: Playback controls and engine selection
- **Assets Panel**: Manage your instruments and samples
- **Project Panel**: Overview of your project structure
- **Quick Guide**: Built-in reference for effects and shortcuts

Most editing actions are disabled until you load a project (`File -> Open Project...`) or import an SPC (`File -> Import SPC...`).
You can also import an NSPC patch with `File -> Import NSPC...` (select NSPC first, then the base SPC it patches).

### Basic Workflow

The typical workflow in ntrak follows these steps:

1. **Load a Project or Import Base Data**: Start from `Open Project`, `Import SPC`, or `Import NSPC` (patch + base SPC)
2. **Choose or Create a Song**: Reuse, duplicate, or create a song entry
3. **Reuse or Edit Samples/Instruments**: Modify imported assets or add new ones
4. **Compose Patterns**: Write musical phrases in the pattern editor
5. **Build Sequences**: Arrange patterns into a complete song
6. **Add Effects**: Apply effects like vibrato, echo, and volume changes
7. **Test and Refine**: Play back your music and make adjustments
8. **Export**: Build your song as an SPC file for use in games

---

## Understanding the Interface

### Panel Overview

ntrak uses a flexible panel system. You can dock, undock, resize, and arrange panels to suit your workflow. Here's what each panel does:

#### Pattern Editor
Your main composing workspace. Shows 8 channels (tracks) in a grid where each row represents a moment in time. You can enter notes, instruments, volumes, and effects here.

#### Control Panel
Contains playback buttons (Play, Stop) and lets you select which SNES game engine you're targeting. Different engines have different capabilities and memory layouts.

#### Assets Panel
Manages your sound library. Has two tabs:
- **Instruments Tab**: Create and edit instruments (how samples are played)
- **Samples Tab**: Import and manage audio samples (the raw sounds)

#### Sequence Editor
Defines the song structure—which patterns play in what order, how many times they loop, and where the song ends.

#### Project Panel
Shows an overview of your entire project, including song count, instrument count, and sample count. Switch between multiple songs in a project here.

#### Build Panel
Configure optimization settings that control how your music is compressed to fit in the SNES's limited memory.

#### ARAM Usage Panel
Visual representation of how your music uses the SNES's 64KB audio RAM. Helps you identify if you're running out of space.

#### SPC Player Panel
Utility panel for loading and playing external SPC files (SNES music files) for reference or testing.

#### SPC Info Panel
Shows live technical playback data. Most users can ignore this panel until they need debugging details.

#### Quick Guide Panel
Built-in help with keyboard shortcuts and effects reference.

---

## Creating Your First Song

Let's walk through creating or editing a simple song.

### Step 1: Load an Engine Context (Required)

Before editing, load data so ntrak has an engine/project context:

1. Use **File -> Open Project...** to open an existing project file
2. Or use **File -> Import SPC...** to import a game SPC into an editable project
3. After loading, use the **Project Panel** to select the song you want to edit

You can edit existing songs, remove songs, duplicate songs, or create new user songs from that starting point.

### Step 2: Reuse or Import a Sample

1. Open the **Assets Panel** and select the **Samples** tab
2. Check imported samples first and reuse them when possible
3. Click **Import WAV** or **Import BRR** to load an audio file if you need a new sample
   - WAV files will be automatically converted to BRR (SNES format)
   - Use short, looped samples for best results
4. Set a name for your sample
5. Configure the **Loop Point** if your sample should repeat
6. Enable **Loop** if you want the sample to sustain

**Tip**: Start with simple waveforms like sine, square, or triangle waves if you don't have samples ready.

### Step 3: Create or Edit an Instrument

1. Switch to the **Instruments** tab in the Assets Panel
2. Select an existing instrument to edit, or click **New** to create a new one
3. Give it a name (e.g., "Lead Synth")
4. Select which sample to use from the dropdown
5. Configure the ADSR envelope:
   - **Attack**: How quickly the sound starts (0-15)
   - **Decay**: How quickly it drops to sustain level (0-7)
   - **Sustain**: The held volume level (0-7)
   - **Release**: How quickly it fades when you stop playing (0-31)

**Tip**: For a piano-like sound, use fast Attack (15), medium Decay (4), low Sustain (2), and medium Release (10).

### Step 4: Create a Pattern

1. In the **Project Panel**, ensure you have a song selected
2. The **Pattern Editor** shows an empty pattern grid
3. Each row is one engine tick; playback timing is engine-driven
4. Each of the 8 columns is a separate audio channel

### Step 5: Enter Notes

1. Click on a cell in the pattern editor
2. Press keys on your keyboard to enter notes:
   - The **ZSXDCVGBHNJM** keys act like piano keys (white and black keys)
   - Use **[** and **]** to change octaves
3. The pattern automatically advances based on your Edit Step setting
4. Press **Period (.)** to enter a rest (silence)
5. Press **Delete** to clear a note

**Tip**: The bottom of the screen shows which key corresponds to which note.

### Step 6: Set the Instrument

1. Click on the instrument column (the "Ins" column)
2. Type the instrument ID in hex (`00` is the first instrument, `01` is the second, and so on)
3. For larger edits, use bulk tools:
   - **Alt+I** to set instrument on selected notes
   - **Alt+R** to remap instrument references across a whole song

### Step 7: Build Your Sequence

1. Open the **Sequence Editor** panel
2. Click **Add Row** to add a new sequence entry
3. Select "Play Pattern" as the operation
4. Enter the pattern ID you've been editing (usually 00 or 01)
5. Add an "End Sequence" entry at the bottom to mark the end of your song

### Step 8: Play Your Song

1. Press **F5** or click **Play** in the Control Panel
2. Your pattern will play back in real-time
3. The pattern editor will highlight the currently playing row
4. Press **F8** or **Stop** to stop playback

---

## Working with Patterns

### Understanding the Pattern Grid

Each row in the pattern editor is one engine tick. Each channel has 5 columns:

1. **Note**: The musical note to play (C-4, D#5, etc.) or a rest
2. **Ins**: Instrument number (00-FF)
3. **Vol**: Volume for this note (00-FF)
4. **QV**: Quantization/Velocity byte (packed timing + velocity values)
5. **FX**: Effects/commands (multiple effects can be placed on one row/channel)

`Ticks/Row` only changes how many ticks are grouped into one visible row in the editor (and related cursor/navigation stepping). It does not change playback timing; all ticks still execute.

Note display markers in the **Note** column:
- `~~~`: Start row of a tie span
- `^^^`: Continued tie rows
- `===`: Start row of a rest span
- `---`: Continued rest rows

### Channel Header Mute/Solo (Playback Audition)

The channel headers in the pattern table can be used to quickly audition parts while listening:

- **Click a channel header**: Toggle mute for that channel
- **Shift+Click a channel header**: Solo that channel
- **Shift+Click the same soloed header again**: Return to all channels enabled

Header state indicators:
- **`[M]`** means the channel is muted
- **`[S]`** means the channel is soloed

These header controls affect playback monitoring only. They do **not** write pattern data or add `FC` (Mute Channel) effects.

### Navigation Shortcuts

- **Arrow Keys**: Move between cells
- **Tab / Shift+Tab**: Jump to next/previous channel
- **Page Up/Down**: Jump 16 rows up/down
- **Home / End**: Jump to first/last row

### Editing Shortcuts

- **Ctrl+C / Ctrl+V**: Copy and paste
- **Ctrl+X**: Cut
- **Ctrl+Z / Ctrl+Y**: Undo/Redo
- **Delete**: Clear selection
- **Ctrl+A**: Select entire pattern
- **Ctrl+Shift+A**: Select current channel only

### Selection and Range Operations

1. Click a cell to select it
2. Hold **Shift** and click another cell to select a range
3. Hold **Shift** and use arrow keys to extend selection
4. With a selection active, you can:
   - **Ctrl+C**: Copy to clipboard
   - **Ctrl+X**: Cut to clipboard
   - **Ctrl+V**: Paste at cursor
   - **Ctrl+Up/Down**: Transpose notes up/down by semitone
   - **Ctrl+Shift+Up/Down**: Transpose notes up/down by octave

### Interpolation

Select a range of cells and press **Ctrl+I** to interpolate between the first and last values. This is useful for:
- Creating smooth volume fades
- Gradual instrument/value transitions
- Smoothing QV changes over a phrase

### Bulk Operations

- **Alt+I**: Set the same instrument on all selected notes
- **Alt+V**: Set the same volume on all selected notes
- **Alt+R**: Open song instrument remap (global or per channel)
- **Ctrl+Shift+,/.**: Cycle through instruments (previous/next)

---

## Managing Instruments and Samples

### Sample Basics

Samples are the raw audio data—the actual waveforms. On SNES, samples are stored in BRR format (Bit Rate Reduction), a lossy compression format that reduces file size.

**Sample Properties**:
- **Name**: Identifying label
- **Loop**: Whether the sample repeats
- **Loop Point**: Which byte to jump back to when looping
- **Sample Rate**: How fast to play the sample (affects pitch)

**Tips for Good Samples**:
- Keep samples short (under 2KB when possible) to save memory
- Use looped samples for sustained sounds (strings, pads)
- Use one-shot samples for percussion (drums, hits)
- Higher sample rates sound better but use more memory

### Instrument Design

Instruments control how samples are played back. Think of them as presets.

**Key Parameters**:

**ADSR Envelope**:
The envelope controls volume over time when a note is played:
- **Attack (0-15)**: How quickly sound goes from 0 to full volume
  - 0 = instant, 15 = very slow fade-in
- **Decay (0-7)**: How quickly sound drops from peak to sustain level
- **Sustain (0-7)**: The held volume level while note is pressed
  - 0 = silent, 7 = full volume
- **Release (0-31)**: How quickly sound fades after note is released

**Gain**:
Alternative volume control mode. Allows fixed volume or more complex envelope shapes.

**Pitch Multiplier**:
Shifts the base pitch of the sample up or down:
- **Base Multiplier**: Coarse pitch adjustment (typically 1.0)
- **Fractional Multiplier**: Fine-tuning

### Importing Samples

**From WAV File**:
1. Click **Import WAV** in the Samples tab
2. Select a WAV file (mono preferred, will be resampled)
3. The sample is automatically converted to BRR format
4. Adjust loop points if needed

**From BRR File**:
1. Click **Import BRR** if you have a pre-converted BRR file
2. Select the .brr file
3. Set loop points manually

### Previewing Sounds

- **Instrument Preview**: Click keys on the keyboard widget in Instruments tab
- **Sample Preview**: Click **Preview** button next to each sample
- **Pattern Preview**: Notes play as you enter them (if tracker preview is enabled)

---

## Building Song Structure

### Understanding Sequences

A **Sequence** is the song arrangement—it defines which patterns play in what order. Think of it like a playlist for your patterns.

### Sequence Operations

The Sequence Editor supports several operation types:

**Play Pattern**:
Plays a specific pattern on all channels. Most common operation.

**Jump Times**:
Conditional loop—plays the following section N times, then continues. Use this for verse/chorus repetition:
```
Play Pattern 00    ← Intro
Jump Times 3       ← Repeat next section 3 times
Play Pattern 01    ← Verse
Play Pattern 02    ← Chorus
                   ← (loops back 3 times)
Play Pattern 03    ← Bridge
End Sequence
```

**Always Jump**:
Unconditional jump to a specific address. Creates infinite loops:
```
Play Pattern 00
Play Pattern 01
Always Jump 00     ← Jump back to start
```

**Fast Forward On/Off**:
Speeds up playback tempo (for fast-paced game music sections).

**End Sequence**:
Marks the end of the song. Required for songs that don't loop forever.

### Building a Complete Song

Typical song structure:
1. Intro patterns
2. Main verse/chorus with Jump Times for repetition
3. Bridge or breakdown section
4. Final chorus or outro
5. End Sequence (or Always Jump back to beginning for looping)

**Example Sequence**:
```
Row 00: Play Pattern 00    (Intro - 16 bars)
Row 01: Jump Times 2       (Play verse+chorus twice)
Row 02: Play Pattern 01    (Verse)
Row 03: Play Pattern 02    (Chorus)
Row 04: Play Pattern 03    (Bridge)
Row 05: Play Pattern 02    (Final chorus)
Row 06: Play Pattern 04    (Outro)
Row 07: End Sequence
```

---

## Effects and Commands

Effects (also called commands or Vcmds) add movement and expression to your song.

### How to Enter Effects (Easy Method)

You do **not** need to type raw hex in the FX Editor.

1. Click in the FX column of a cell
2. Press **Ctrl+E** to open the FX Editor dialog
3. Select an effect by name
4. Adjust parameters with labeled controls (sliders) and tooltips
5. Click **Apply** (or **Apply & Close**) to commit changes

Subroutine call commands are also editable in this dialog. To remove a subroutine call from a row, delete it from the FX Editor list and apply.

### Essential Effects

The FX Editor shows effect names directly. Hex IDs are included below only for reference.

#### Volume Control

**ED - Volume (Vol)**
- Parameter: XX (00-FF)
- Sets the channel volume
- Example: `ED 80` sets volume to 50%

**EE - Volume Fade (VFd)**
- Parameters: XX YY (time, target)
- Gradually fades volume over time
- Example: `EE 20 00` fades to silence over 32 ticks

**E5 - Global Volume (GVl)**
- Parameter: XX (00-FF)
- Sets master volume for entire song
- Example: `E5 C0` sets master to 75%

#### Panning

**E1 - Panning (Pan)**
- Parameter: XX (00=left, 80=center, FF=right)
- Sets stereo position
- Example: `E1 00` pans hard left

**E2 - Pan Fade (PFa)**
- Parameters: XX YY (time, target)
- Smooth panning motion
- Example: `E2 30 FF` pans to hard right over 48 ticks

#### Pitch Effects

**E3 - Vibrato On (VOn)**
- Parameters: XX YY ZZ (delay, rate, depth)
- Adds pitch wobble (vibrato)
- Example: `E3 00 08 0A` starts vibrato immediately

**E4 - Vibrato Off (VOf)**
- No parameters
- Stops vibrato

**F1 - Pitch Envelope To (PEt)**
- Parameters: XX YY ZZ (delay, length, semitone)
- Applies a timed pitch envelope toward a target value
- Example: `F1 00 20 0C` slides up 1 octave over 32 ticks

**F9 - Pitch Slide To Note (PSt)**
- Parameters: XX YY ZZ (delay, length, target note)
- Portamento effect
- Example: `F9 00 10 30` slides to note C-4

#### Modulation

**EB - Tremolo On (TOn)**
- Parameters: XX YY ZZ (delay, rate, depth)
- Volume wobble effect
- Example: `EB 00 06 05` gentle tremolo

**EC - Tremolo Off (TOf)**
- No parameters
- Stops tremolo

#### Echo (Reverb/Delay)

**F5 - Echo On (EOn)**
- Parameters: XX YY ZZ (channels, left, right)
- Enables echo effect
- Example: `F5 FF 40 40` echo on all channels

**F6 - Echo Off (EOf)**
- No parameters
- Disables echo

**F7 - Echo Params (EPr)**
- Parameters: XX YY ZZ (delay, feedback, FIR)
- Configures echo characteristics

#### Tempo

**E7 - Tempo (Tmp)**
- Parameter: XX
- Sets song playback tempo
- Example: `E7 80` sets moderate tempo

**E8 - Tempo Fade (TmF)**
- Parameters: XX YY (time, target)
- Gradually changes tempo
- Example: `E8 40 A0` speeds up over time

#### Transpose

**E9 - Global Transpose (GTr)**
- Parameter: XX (signed semitones)
- Shifts all notes up or down
- Example: `E9 0C` transposes everything up 12 semitones (1 octave)

**EA - Per Voice Transpose (PTr)**
- Parameter: XX (signed semitones)
- Transposes only current channel
- Example: `EA FE` transposes down 2 semitones

#### Special

**Instrument Changes**
- Use the **Ins** column for per-row instrument changes
- Use **Alt+I** for bulk set on selected cells
- Use **Alt+R** for song-wide instrument remapping

**FA - Percussion Base Instrument (PIn)**
- Parameter: XX
- Sets base instrument for percussion mode

**FC - Mute Channel (MCh)**
- No parameters
- Silences the channel

### Advanced Effect Usage

**Combining Effects**:
You can stack multiple effects on the same row/channel from the FX Editor.

**Effect Timing**:
Effects execute on the row they appear, not when the note plays. Place effects carefully relative to note timing.

**Inheritance**:
Some properties (instrument, volume) carry forward to subsequent notes until changed.

---

## Playback and Testing

### Playback Controls

**Global Shortcuts**:
- **F5**: Play song from beginning
- **F6**: Play from current pattern
- **F8**: Stop playback
- **Space**: Toggle play/pause

**Control Panel Buttons**:
- **Play**: Starts playback
- **Stop**: Stops playback
- **Reset Before Play**: Optional—clears audio state before playing

### Per-Channel Monitoring from Pattern Headers

While playback is running, you can mute/solo channels directly from the pattern editor header row:

- **Click header**: Mute/unmute channel
- **Shift+Click header**: Solo/unsolo channel

If you set mute/solo states before pressing Play, they are applied when playback starts.

### Playback Modes

**Play Song (F5)**:
Starts from the beginning of the sequence. Use this to hear the complete arrangement.

**Play Pattern (F6)**:
Starts from the currently selected pattern, ignoring the sequence. Useful for testing individual patterns during composition.

### Visual Feedback

During playback:
- **Pattern Editor**: Current row is highlighted
- **Sequence Editor**: Current sequence entry is highlighted
- **SPC Info Panel**: Shows real-time voice activity

### Testing Tips

1. **Test Early and Often**: Play back your pattern every few rows to catch mistakes
2. **Listen on Different Outputs**: Check both headphones and speakers
3. **Check Balance**: Make sure melody, bass, and percussion are all audible
4. **Watch Memory Usage**: Keep an eye on the ARAM Usage panel

---

## Memory Management

### Understanding ARAM

The SNES audio processor has only **64KB of RAM** (ARAM - Audio RAM) for everything:
- Song sequence data
- Pattern data
- Instruments
- Samples (the largest consumers of memory)
- Echo buffer (if using echo effects)

### ARAM Usage Panel

The ARAM Usage panel shows a color-coded bar representing memory allocation:
- **Blue**: Reserved regions
- **Cyan**: Song index table
- **Teal**: Instrument table
- **Green**: Sample directory
- **Orange**: Sample data (usually the biggest)
- **Light Green**: Sequence data
- **Light Blue**: Pattern table
- **Light Yellow**: Track data
- **Red**: Subroutine data
- **Dark**: Free space

### Saving Memory

**Sample Optimization**:
- Use lower sample rates when possible
- Keep samples short
- Reuse samples for multiple instruments (adjust ADSR instead)
- Use looped samples to avoid storing tail/decay

**Pattern Optimization**:
- Enable "Optimize subroutines on build" in Build Panel
- This automatically detects repeated patterns and converts them to subroutines

**Avoid Waste**:
- Delete unused samples and instruments
- Avoid very long or very high sample-rate files when memory is tight
- Simplify complex patterns—simpler is often better on limited hardware

### Build Panel Settings

The Build Panel controls how ntrak optimizes your song:

**Presets**:
- **Relaxed**: Fast compile, less optimization (good for testing)
- **Balanced**: Moderate optimization (default)
- **Aggressive**: Maximum compression (use for final build)

**Advanced Options**:
- **Max Optimize Iterations**: How many times the optimizer runs
- **Flatten Subroutines**: Converts subroutines to inline code (increases size but simplifies)
- **Optimize Subroutines on Build**: Automatically creates subroutines from repeated patterns (decreases size)
- **Compact ARAM layout on build**: Packs relocatable song data into tighter ARAM ranges. This usually reduces
  holes and lowers NSPC upload segment count.
- **Lock engine content edits**: When enabled (default), engine-owned songs/assets are read-only in the editor.
  This protects base engine content from accidental edits or deletion.

---

## Advanced Features

### Subroutines

Subroutines are reusable pattern fragments. If you have a drum pattern that repeats many times, the optimizer can extract it as a subroutine and reference it, saving memory.

**Manual Subroutine Control**:
- Build Panel → "Flatten subroutines on load" to expand subroutines into patterns
- Build Panel → "Optimize subroutines on build" to automatically create them

### Multi-Song Projects

The Project Panel lets you manage multiple songs in one project:
- Click the song dropdown to switch between songs
- Click **New** to create a new song
- Click **Duplicate** to copy the current song
- Click **Remove** to delete a song

All songs share the same sample and instrument library.

### Engine Content Lock (Safety)

The Build Panel includes **Lock engine content edits** (enabled by default).

When enabled, content marked as engine-owned is protected:
- **Project Panel**: engine songs cannot be renamed, re-authored, or removed
- **Sequence Editor / Pattern Editor**: engine songs are read-only for editing operations
- **Assets Panel**: engine instruments/samples cannot be edited or removed

Disable this toggle only when you intentionally want to modify engine-provided content.

### Engine Selection

Different SNES games use different audio engines with unique features:

**Super Metroid**:
- Full feature set
- Robust echo effects

**A Link to the Past**:
- Similar to Super Metroid
- Command-compatible with Super Metroid (different memory layout/pointers)

**Super Mario World 2: Yoshi's Island**:
- Command-compatible with Super Metroid/ALttP (different memory layout/pointers)

**Super Mario World**:
- Prototype-style N-SPC variant
- Requires command remapping compared to Super Metroid/ALttP/SMW2

Select the target engine in the Control Panel to match command maps and memory layout for that game.

### Advanced: Custom Engine Profiles

If you need support for another game that uses a compatible N-SPC-derived engine, you can add overrides in `engine_overrides.json`.

- Bundled base config: `config/engine_configs.json`
- Linux override file: `$XDG_CONFIG_HOME/ntrak/engine_overrides.json` (or `~/.config/ntrak/engine_overrides.json`)
- Override format: JSON array of engine objects keyed by `id` (preferred) or `name`
- Matching ids/names patch bundled engines; unknown ids/names are added as custom engines.
- Add/update engine entries with required pointers, reserved regions, and command-map fields.

Important: incorrect values can break import, playback, or build output. Keep backups before editing.

### Importing IT Modules (.it)

Use **File -> Import IT...** to open the IT Import Workbench and convert an Impulse Tracker song into the currently selected song slot.

**Requirements**:
- A project must already be loaded
- A valid target song must be selected in the Project panel

**Workbench highlights**:
- **Song Overview**: module name, imported pattern/track/instrument/sample counts
- **ARAM estimates**: estimated sample bytes vs free ARAM (current and after selected deletions)
- **Resampling**:
  - Global ratio (`0.10` to `2.00`)
  - Per-sample ratio overrides (`0.10` to `2.00`)
  - **High Quality Resampling** toggle
  - **Treble Compensation (Gaussian)** toggle
- **Delete Target Instruments/Samples**: optional cleanup before import
- **Warnings Preview**: issues and conversions shown before commit
- **Sample format support**:
  - IT compressed samples (IT2.14 / IT2.15) are decoded during import
  - Stereo IT samples are downmixed to mono for NSPC compatibility

**Extension behavior**:
- Import attempts to enable matching engine extensions (Legato, Arpeggio, No Pattern KOFF) when available.
- If an extension is unavailable in the selected engine config, related IT behavior is downgraded and a warning is shown.

### Porting Songs to Another Engine

Use **File -> Port Song to Engine...** to move a source song from your current project into a target SPC engine.

Porting workflow:
1. Pick the source song in your current project
2. Select a target SPC file (this determines the target engine and asset set)
3. Configure instrument mappings (`Copy` or `Map To`)
4. For copied instruments, choose sample action (`New`, `Reuse`, or `Replace`)
5. Optionally mark target instruments for deletion before porting
6. Choose target placement (`Append as new song` or `Overwrite existing`)
7. Click **Port Song**

### NSPC Patch Import/Export

**Import NSPC** (`File -> Import NSPC...`):
1. Select a `.nspc` upload/patch file
2. Select the base `.spc` it should be applied to
3. ntrak applies the upload, then loads the patched result as your working project

**Export User Data (.nspc)** (`File -> Export User Data (.nspc)`):
- Exports user-provided content as an NSPC upload stream
- Useful for patch workflows and ROM hack pipelines

### Importing SPC Files

You can import existing SPC files to analyze or modify:
1. Use **SPC Player Panel** to load an SPC file
2. Or use **File → Import SPC...** in the main menu
3. ntrak will attempt to parse the song structure

Note: Importing SPC files is reverse-engineering and may not always produce perfect results.

### Exporting

To export your song for use in a game:
1. Ensure a base SPC is loaded (via `Import SPC...` or a project linked to one)
2. Select the song you want to export in the Project panel
3. Use **File -> Export SPC...**
4. ntrak compiles and writes an auto-play SPC for the selected song

---

## Tips and Best Practices

### Composition Tips

1. **Start Simple**: Begin with a basic melody on one channel, then add accompaniment
2. **Use Channels Wisely**: Reserve channel 1 for melody, channels 2-3 for harmony, channels 4-5 for bass/percussion
3. **Don't Overuse Effects**: Effects add CPU overhead on real hardware—use them strategically
4. **Think in Loops**: SNES music typically loops forever—design your sequences accordingly
5. **Study Existing Music**: Import SPC files from games you like to see how professionals structured their songs

### Performance Tips

1. **Keep Patterns Short**: Break long songs into many short patterns rather than one huge pattern
2. **Reuse Patterns**: Use the sequence editor to repeat patterns instead of duplicating them
3. **Use Rests**: Silence (rests) uses less CPU than sustained notes
4. **Limit Polyphony**: Don't play too many notes simultaneously (aim for 4-6 active voices)

### Workflow Tips

1. **Save Often**: Use **Ctrl+S** frequently
2. **Use Multiple Songs for Variations**: Try different arrangements in separate songs within the same project
3. **Name Everything**: Give descriptive names to instruments, samples, and patterns
4. **Test on Target Engine**: Always test your final build with the correct engine selected
5. **Check Memory Early**: Don't wait until the end to check ARAM usage—you might have to cut content

### Troubleshooting

**No Sound During Playback**:
- Check that instruments have valid samples assigned
- Verify sample rate is reasonable (32000 Hz is standard)
- Ensure volume is not 00
- Check that the control panel shows "Playing"

**Clicks or Glitches**:
- Adjust sample loop points
- Smooth volume transitions with volume fade effects
- Use attack/release envelopes to avoid abrupt starts/stops

**Memory Overflow**:
- Delete unused samples
- Lower sample rates
- Simplify patterns
- Enable subroutine optimization

**Wrong Pitch**:
- Check octave setting ([ and ] keys)
- Verify instrument pitch multiplier is 1.0
- Check for transpose effects

---

## Keyboard Reference

### Global Shortcuts

| Shortcut | Action |
|----------|--------|
| Ctrl+S | Save project |
| Ctrl+Shift+S | Save project as |
| F5 | Play song from beginning |
| F6 | Play from current pattern |
| F8 | Stop playback |
| Space | Play/stop toggle |

### Pattern Editor Shortcuts

#### Navigation
| Shortcut | Action |
|----------|--------|
| Arrow keys | Move cursor |
| Tab / Shift+Tab | Next/previous channel |
| Page Up/Down | Jump 16 rows |
| Home / End | First/last row |
| Shift+Arrows | Extend selection |

#### Editing
| Shortcut | Action |
|----------|--------|
| ZSXDCVGBHNJM... | Enter notes (piano keys) |
| . (period) | Enter rest |
| Delete | Clear cell/selection |
| Insert (note column) | Insert 1 tick at cursor row (shift later rows down) |
| Backspace (note column) | Remove 1 tick at cursor row (shift later rows up) |
| [ / ] | Change octave (±1) |
| Ctrl+[ / Ctrl+] | Change edit step (±1) |
| Ctrl+E | Open FX editor |

#### Mouse (Pattern Editor)
| Action | Result |
|--------|--------|
| Click channel header | Mute/unmute that channel during playback monitoring |
| Shift+Click channel header | Solo/unsolo that channel during playback monitoring |

#### Selection & Clipboard
| Shortcut | Action |
|----------|--------|
| Ctrl+A | Select all |
| Ctrl+Shift+A | Select current channel |
| Ctrl+C | Copy selection |
| Ctrl+X | Cut selection |
| Ctrl+V | Paste |

#### Transpose
| Shortcut | Action |
|----------|--------|
| Ctrl+Up/Down | Transpose selection (±1 semitone) |
| Ctrl+Shift+Up/Down | Transpose selection (±1 octave) |
| Ctrl+Shift+,/. | Cycle instrument (prev/next) |

#### Bulk Operations
| Shortcut | Action |
|----------|--------|
| Ctrl+I | Interpolate selection |
| Alt+I | Set instrument on selection |
| Alt+V | Set volume on selection |
| Alt+R | Open song instrument remap |

---

## Glossary

**APU (Audio Processing Unit)**: The SNES's dedicated sound chip that plays audio independently from the CPU.

**ARAM (Audio RAM)**: 64KB of RAM inside the APU used for storing music data and samples.

**BRR (Bit Rate Reduction)**: SNES's lossy audio compression format. Reduces sample size by about 4x.

**Channel**: One of 8 independent audio voices on the SNES. Each can play one note at a time.

**Effect/Vcmd**: Commands that modify playback (volume, pitch, panning, etc.).

**QV**: Quantization/Velocity byte stored in one column; controls note articulation timing/velocity behavior.

**Instrument**: A configuration that defines how a sample is played (envelope, pitch, etc.).

**Pattern**: A grid of notes, instruments, and effects for all 8 channels over a period of time.

**Sample**: Raw audio waveform data in BRR format.

**Sequence**: The song structure—defines which patterns play in which order.

**SPC**: SPC700, the CPU inside the SNES APU. Also refers to .spc files (SNES music archives).

**S-DSP (Sound Digital Signal Processor)**: The actual audio hardware inside the SNES APU that mixes and outputs sound.

**Subroutine**: A reusable pattern fragment that can be called from multiple places to save memory.

**Tick**: A small timing unit used by the engine for command timing and playback updates.

---

## Conclusion

You now have a full working overview of ntrak. The fastest way to learn is to build a small song:

1. Import some samples
2. Create a few instruments
3. Write a simple melody
4. Add bass and harmony
5. Apply effects
6. Build a complete song structure

Experiment in short loops, save often, and use the Quick Guide when you need a reminder. Keep ARAM usage visible so you catch memory issues early.

Happy tracking.

---

**For more information**:
- See [README.md](README.md) for build instructions
- See [STYLE_GUIDE.md](STYLE_GUIDE.md) for developer information
- Report issues at the project repository

**Version**: Rolling (see repository history for latest changes)
