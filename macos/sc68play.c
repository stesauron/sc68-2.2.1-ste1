/*
 * sc68play - play sc68 / SNDH files through CoreAudio on macOS.
 *
 * The stock sc68 CLI writes headerless PCM to stdout, which macOS has no way
 * to play directly (afplay only accepts a file path, not a stream). This is a
 * small AudioQueue front-end over the same api68 library, so playback starts
 * immediately and nothing is written to disk.
 *
 * Copyright (C) 1998-2003 Benjamin Gerard  (sc68 itself, GPLv2+)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include <AudioToolbox/AudioToolbox.h>

#include "api68/api68.h"

#define SAMPLE_RATE 44100
#define CHANNELS    2
#define FRAME_BYTES (CHANNELS * 2)      /* 16-bit stereo */
#define NBUFFERS    3
#define FRAMES      4096                /* ~93ms per buffer */

static api68_t *  sc68     = 0;
static volatile int finished = 0;       /* emulation reached end of disk  */
static volatile int quit     = 0;       /* user pressed Ctrl-C           */
static volatile int inflight = 0;       /* buffers still owned by the queue */
static int          loop_forever = 0;
static int          max_frames   = 0;   /* --seconds limit, 0 = unlimited */
static int          done_frames  = 0;

static void on_sigint(int sig)
{
  (void)sig;
  quit = 1;
}

/* Pull one buffer's worth of PCM out of the emulator.
 *
 * Once we are done we deliberately stop re-enqueueing: handing the buffer back
 * would keep the queue permanently non-empty and a drain-then-stop would never
 * complete. Letting inflight fall to zero is what lets main() finish. */
static void fill(AudioQueueRef queue, AudioQueueBufferRef buf)
{
  int code;

  if (finished || quit) {
    return;
  }

  code = api68_process(sc68, buf->mAudioData,
                       buf->mAudioDataBytesCapacity / FRAME_BYTES);

  if (code == API68_MIX_ERROR) {
    fprintf(stderr, "sc68play: mix error\n");
    finished = 1;
    return;
  }

  buf->mAudioDataByteSize = buf->mAudioDataBytesCapacity;
  if (AudioQueueEnqueueBuffer(queue, buf, 0, 0) == noErr) {
    ++inflight;
  }
  done_frames += buf->mAudioDataBytesCapacity / FRAME_BYTES;

  if (code & API68_END) {
    finished = 1;
  } else if (!loop_forever && (code & API68_LOOP)) {
    finished = 1;
  } else if (max_frames && done_frames >= max_frames) {
    finished = 1;
  }
}

static void callback(void *user, AudioQueueRef queue, AudioQueueBufferRef buf)
{
  (void)user;
  --inflight;
  fill(queue, buf);
}

static void show_info(int track)
{
  api68_music_info_t info;

  if (api68_music_info(sc68, &info, track, 0)) {
    return;
  }
  fprintf(stderr, "Track    : %d/%d\n", info.track, info.tracks);
  fprintf(stderr, "Title    : %s\n", info.title);
  fprintf(stderr, "Author   : %s\n", info.author);
  if (info.composer && strcmp(info.composer, info.author)) {
    fprintf(stderr, "Composer : %s\n", info.composer);
  }
  fprintf(stderr, "Hardware : %s\n", info.hwname);
  fprintf(stderr, "Duration : %s\n", info.time);
  fprintf(stderr, "Replay   : %s\n", info.replay ? info.replay : "built-in");
}

static int usage(void)
{
  printf(
    "sc68play - play Atari ST / Amiga chip tunes through CoreAudio\n"
    "\n"
    "usage: sc68play [options] <file.sndh|file.sc68>\n"
    "\n"
    "options:\n"
    "  --help       this message\n"
    "  --track=N    play track N (default: first track)\n"
    "  --seconds=N  stop after N seconds\n"
    "  --loop       keep looping instead of stopping at the end\n"
    "  --quiet      do not print track information\n"
    "\n"
    "Set SC68_DATA to the directory holding Replay/ if the tunes need an\n"
    "external replay routine (defaults to the compiled-in share path).\n");
  return 0;
}

int main(int argc, char **argv)
{
  api68_init_t          init;
  AudioStreamBasicDescription fmt;
  AudioQueueRef         queue = 0;
  AudioQueueBufferRef   bufs[NBUFFERS];
  OSStatus              err;
  const char *          fname = 0;
  int                   track = -1, quiet = 0, i;

  for (i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "--help")) {
      return usage();
    } else if (!strcmp(argv[i], "--quiet")) {
      quiet = 1;
    } else if (!strcmp(argv[i], "--loop")) {
      loop_forever = 1;
    } else if (!strncmp(argv[i], "--track=", 8)) {
      track = atoi(argv[i] + 8);
    } else if (!strncmp(argv[i], "--seconds=", 10)) {
      max_frames = atoi(argv[i] + 10) * SAMPLE_RATE;
    } else if (argv[i][0] == '-' && argv[i][1]) {
      fprintf(stderr, "sc68play: unknown option \"%s\"\n", argv[i]);
      return 2;
    } else if (!fname) {
      fname = argv[i];
    } else {
      fprintf(stderr, "sc68play: unexpected argument \"%s\"\n", argv[i]);
      return 2;
    }
  }

  if (!fname) {
    return usage(), 2;
  }

  memset(&init, 0, sizeof(init));
  init.alloc = malloc;
  init.free  = free;
  init.sampling_rate = SAMPLE_RATE;

  sc68 = api68_init(&init);
  if (!sc68) {
    fprintf(stderr, "sc68play: init failed: %s\n", api68_error());
    return 1;
  }

  if (api68_verify_file(fname) < 0) {
    fprintf(stderr, "sc68play: \"%s\" is not an sc68/SNDH file\n", fname);
    goto fail;
  }
  if (api68_load_file(sc68, fname)) {
    fprintf(stderr, "sc68play: cannot load \"%s\": %s\n", fname, api68_error());
    goto fail;
  }
  if (api68_play(sc68, track > 0 ? track : 1)) {
    fprintf(stderr, "sc68play: cannot play: %s\n", api68_error());
    goto fail;
  }

  if (!quiet) {
    show_info(track > 0 ? track : 1);
  }

  memset(&fmt, 0, sizeof(fmt));
  fmt.mSampleRate       = SAMPLE_RATE;
  fmt.mFormatID         = kAudioFormatLinearPCM;
  fmt.mFormatFlags      = kAudioFormatFlagIsSignedInteger
                        | kAudioFormatFlagIsPacked;
  fmt.mChannelsPerFrame = CHANNELS;
  fmt.mBitsPerChannel   = 16;
  fmt.mFramesPerPacket  = 1;
  fmt.mBytesPerFrame    = FRAME_BYTES;
  fmt.mBytesPerPacket   = FRAME_BYTES;

  err = AudioQueueNewOutput(&fmt, callback, 0, 0, 0, 0, &queue);
  if (err) {
    fprintf(stderr, "sc68play: AudioQueueNewOutput failed (%d)\n", (int)err);
    goto fail;
  }

  signal(SIGINT, on_sigint);

  /* Prime the queue: the callback only fires for buffers already enqueued. */
  for (i = 0; i < NBUFFERS; ++i) {
    err = AudioQueueAllocateBuffer(queue, FRAMES * FRAME_BYTES, &bufs[i]);
    if (err) {
      fprintf(stderr, "sc68play: buffer alloc failed (%d)\n", (int)err);
      goto fail;
    }
    fill(queue, bufs[i]);
  }

  err = AudioQueueStart(queue, 0);
  if (err) {
    fprintf(stderr, "sc68play: AudioQueueStart failed (%d)\n", (int)err);
    goto fail;
  }

  /* The callback runs on the queue's own thread; just wait it out. */
  while (!finished && !quit) {
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.25, false);
  }

  /* Let the audio already handed to the queue finish playing. Bounded so a
   * stalled queue cannot hang us. */
  if (!quit) {
    int spins = 0;
    while (inflight > 0 && spins++ < 40) {
      CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, false);
    }
  }

  AudioQueueStop(queue, true);
  AudioQueueDispose(queue, true);

  if (quit) {
    fprintf(stderr, "\n");
  }
  api68_shutdown(sc68);
  return 0;

 fail:
  if (queue) {
    AudioQueueDispose(queue, true);
  }
  api68_shutdown(sc68);
  return 1;
}
