#include "sgensys.h"
#include "wavfile.h"
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>
#define NAME_OUT "/dev/dsp"
#define NUM_CHANNELS 2
#define DEFAULT_SRATE 44100

static int open_audio_dev(const char *name, int mode, uint *srate) {
  int tmp, fd;
  if ((fd = open(name, mode, 0)) == -1) {
    perror(name);
    return -1;
  }

  tmp = AFMT_S16_NE;
  if (ioctl(fd, SNDCTL_DSP_SETFMT, &tmp) == -1) {
    perror("SNDCTL_DSP_SETFMT");
    goto ERROR;
  }
  if (tmp != AFMT_S16_NE)
    fputs("warning: 16 native endian int format unsupported\n", stderr);

  tmp = NUM_CHANNELS;
  if (ioctl(fd, SNDCTL_DSP_CHANNELS, &tmp) == -1) {
    perror("SNDCTL_DSP_CHANNELS");
    goto ERROR;
  }
  if (tmp != NUM_CHANNELS)
    fprintf(stderr, "warning: %d channels unsupported\n", NUM_CHANNELS);

  tmp = *srate;
  if (ioctl(fd, SNDCTL_DSP_SPEED, &tmp) == -1) {
    perror("SNDCTL_DSP_SPEED");
    goto ERROR;
  }
  if (tmp != (int)*srate)
    fprintf(stderr, "warning: sample rate %d (%d expected)\n", tmp, *srate);
  *srate = tmp;

  return fd;

ERROR:
  if (fd != -1) close(fd);
  fputs("error: couldn't configure audio device output", stderr);
  return -1;
}

static void wav_file_out(SGSWAVFile *wf, uint srate, struct SGSProgram *prg) {
  short buf[2048];
  uchar run;
  SGSGenerator *gen = SGS_generator_create(srate, prg);
  do {
    run = SGS_generator_run(gen, buf, 1024);
    if (SGS_wav_file_write(wf, buf, 1024) != 0)
      puts("warning: audio write failed");
  } while (run);
  SGS_generator_destroy(gen);
}

static void audio_dev_out(int fd, uint srate, struct SGSProgram *prg) {
  short buf[2048];
  uchar run;
  SGSGenerator *gen = SGS_generator_create(srate, prg);
  do {
    run = SGS_generator_run(gen, buf, 1024);
    if (write(fd, buf, sizeof(buf)) != sizeof(buf))
      puts("warning: audio write failed");
  } while (run);
  SGS_generator_destroy(gen);
}

static void print_usage(void) {
  puts(
"usage: sgensys [-o wavfile] [-r srate] scriptfile\n"
"  By default, audio output is sent to the audio device.\n"
"  -o \twrite output to a 16-bit PCM WAV file\n"
"  -r \tset sample rate in Hz, default 44100; for audio device output,\n"
"     \ta warning may be printed as setting the given rate may fail"
  );
}

static int get_piarg(const char *str) {
  char *endp;
  int i;
  errno = 0;
  i = strtol(str, &endp, 10);
  if (errno || i < 0 || endp == str || *endp) return -1;
  return i;
}

int main(int argc, char **argv) {
  const char *script_path = 0,
             *wav_path = 0;
  SGSProgram *prg;
  int fd_out;
  uint srate = DEFAULT_SRATE;
  for (;;) {
    --argc;
    ++argv;
    if (argc < 1) {
      if (!script_path) USAGE: {
        print_usage();
        return 0;
      }
      break;
    }
    if (!strcmp(*argv, "-o")) {
      --argc;
      ++argv;
      if (argc < 1) goto USAGE;
      wav_path = *argv;
    } else if (!strcmp(*argv, "-r")) {
      int i;
      --argc;
      ++argv;
      i = get_piarg(*argv);
      if (i < 0) goto USAGE;
      srate = i;
    } else {
      if (script_path) goto USAGE;
      script_path = *argv;
    }
  }
  if (!(prg = SGS_program_create(script_path))) {
    fprintf(stderr, "error: couldn't open script file \"%s\"\n", script_path);
    return 1;
  }
  if (wav_path) {
    SGSWAVFile *wf = SGS_begin_wav_file(wav_path, NUM_CHANNELS, srate);
    if (!wf) {
      fprintf(stderr, "error: couldn't open wav file \"%s\"\n", wav_path);
      return 1;
    }
    wav_file_out(wf, srate, prg);
    SGS_end_wav_file(wf);
  } else {
    fd_out = open_audio_dev(NAME_OUT, O_WRONLY, &srate);
    if (fd_out == -1) return 1;
    audio_dev_out(fd_out, srate, prg);
    close(fd_out);
  }
  SGS_program_destroy(prg);
  return 0;
}
