# audiorecorder (ra)

<img src="https://static.kippesp.dev/audiorecorder_demo.gif" width="900" alt="ra demo recording">

A macOS command-line audio recorder that writes 24-bit CAF files, targeting
sessions up to 15+ hours.  It compiles to a single binary with no dependencies
beyond macOS system frameworks.  The design centers on these features:

- **No dropped audio during disk hesitation**: audio capture and file writes
  run on separate threads with a 60-second internal buffer between them
  (lock-free ring buffer decoupling the CoreAudio real-time callback from disk
  I/O)
- **Low power usage**: the writer thread wakes on demand via semaphore
  rather than polling
- **No lost sessions to system sleep**: prevents idle sleep while recording
- **Disk space validation**: the recorder checks available space before
  recording and warns or stops if there isn't enough room for the requested
  duration
- **Visual level meter**: the display shows per-channel level bars with peak
  hold and clip detection
- **Input monitoring**: optionally play the input signal through the default
  output device during recording (`--monitor`)
- **Safe output naming**: if the output file already exists, the recorder
  appends `_1`, `_2`, etc.
- **Test mode**: `--test` captures audio and displays the level meter without
  writing a file
- **Post-session summary**: reports peak audio level with clipping detection;
  `--verbose` adds ring buffer high-water mark

## Output Format

- **Format:** CAF (Core Audio Format)
- **Sample rate:** Matches the input device (typically 44100 or 48000 Hz)
- **Bit depth:** 24-bit signed integer, packed
- **Channels:** Stereo or mono depending on device

## Usage

```
ra [options]

  -o, --output <path>       Output file path (default: ./Recording_YYYYMMDDTHHMMSS.caf)
  -d, --device <#|name|uid> Input device by number, name, or UID (default: system default)
  -l, --list-devices        List available input devices and exit
  -M, --monitor             Play input through default output device
  -t, --test                Test mode: capture audio without writing a file
  -D, --max-duration <min>  Stop after N minutes (default: unlimited)
  -q, --quiet               Suppress level meter display
  -v, --verbose             Print diagnostic output (buffer health, overruns)
      --extend <input.caf>  Extend mode: pad an existing ra recording (requires --pad-to and -o)
      --pad-to <min>        Target declared duration in minutes (extend mode)
  -h, --help                Show this help
  -V, --version             Show version and exit
```

### Examples

List input devices:

```
ra -l
```

Record from device 2, stop after 60 minutes:

```
ra -d 2 -D 60 -o recording.caf
```

Record from the default device with no time limit:

```
ra -o recording.caf
```

### Extend mode

Motivation: when stitching several equally-sized recording chunks into a
single long session in Logic Pro, extend mode fabricates a file at the
target duration in a few seconds so the timeline is already the right
length to drop the other chunks onto.

`ra --extend <input.caf> --pad-to <minutes> -o <output.caf>` produces a new
CAF whose declared duration is at least `<minutes>` minutes long, using a
sparse hole for the padded region so the file consumes no additional disk
space on APFS. The intended workflow is to open the padded output in Logic
Pro, edit additional audio into the extended range, and export. The padded
file is a disposable intermediate; the padded region decodes as silence.

```
ra --extend recording.caf --pad-to 90 -o recording_padded.caf
```

Only `ra`-produced CAF files are accepted as input. Extend mode requires
`--extend`, `--pad-to`, and `-o` together; no other options may be
combined with it.

`-o` follows the same path rules as recording mode: `~/` is expanded,
`.caf` is appended if missing, and if the resulting path already exists
the output is written to `<stem>_1.caf`, `<stem>_2.caf`, and so on. The
success message prints the final path. Delete the prior padded file
before re-running if you want the same name reused.

## Architecture

The ring buffer decouples real-time audio capture from disk I/O.
CoreAudio delivers samples to the callback, which pushes them onto
the ring and signals the writer thread.  The writer drains the ring
and writes to disk through `ExtAudioFileWrite`.

### Source modules

```
src/
  main.cpp            Entry point -- parses args, dispatches to session
  args.h/cpp          CLI argument parsing and usage display
  session.h/cpp         Session lifecycle: setup, recording loop, summary
  capture.h/cpp         CoreAudio input unit setup and callback
  monitor.h/cpp         CoreAudio output unit for input monitoring
  output_file.h/cpp     CAF file creation
  writer.h/cpp          Writer thread: ring drain, file write
  display.h/cpp         Level meter rendering and display loop
  extend_caf.h/cpp      Extend mode: pad CAF recording via sparse hole
  signal_handler.h/cpp  Signal registration for graceful shutdown
  sleep_guard.h         RAII guard to prevent idle sleep during recording
  recording_context.h   Shared recording state and RAII resource guards
  device.h/cpp          Audio device enumeration and selection
  file_util.h/cpp       Path resolution and disk space utilities
  ring_buffer.h         Lock-free SPSC ring buffer
  util.h                Formatted stderr output
```

## Building

See [BUILDING.md](BUILDING.md).

## When to Use Something Else

- **Cross-platform support:** `ffmpeg` or `sox` run on Linux and Windows.
- **Real-time compression:** If recording directly to MP3, AAC, or FLAC is
  needed to save disk space.
- **Live processing:** If real-time effects (EQ, compression) or
  complex signal routing are required.
- **Advanced metadata:** If BWF/iXML embedding for archival purposes is needed.

## License

MIT.  See [LICENSE](LICENSE).
