#include "mgensys.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/soundcard.h>
#define NAME_OUT "/dev/dsp"
#define NUM_CHANNELS 2
#define SRATE 96000

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
    fputs("warning: 16 native endian int format unsupported", stderr);

  tmp = NUM_CHANNELS;
  if (ioctl(fd, SNDCTL_DSP_CHANNELS, &tmp) == -1) {
    perror("SNDCTL_DSP_CHANNELS");
    goto ERROR;
  }
  if (tmp != NUM_CHANNELS)
    fprintf(stderr, "Warning: %d channels unsupported.", NUM_CHANNELS);

  tmp = SRATE;
  if (ioctl(fd, SNDCTL_DSP_SPEED, &tmp) == -1) {
    perror("SNDCTL_DSP_SPEED");
    goto ERROR;
  }
  printf("sample rate: %d (%d expected)\n", tmp, SRATE);
  *srate = tmp;

  return fd;

ERROR:
  if (fd != -1) close(fd);
  return -1;
}

static void make_audio(int fd, uint srate, struct MGSProgram *prg) {
  short buf[2048];
  uchar run;
  MGSGenerator *gen = MGSGenerator_create(srate, prg);
  do {
    run = MGSGenerator_run(gen, buf, 1024);
    if (write(fd, buf, sizeof(buf)) != sizeof(buf))
      puts("warning: audio write failed");
  } while (run);
  MGSGenerator_destroy(gen);
}

int main(int argc, char **argv) {
  MGSProgram *prg;
  int fd_out;
  uint srate;
  if (argc < 2) {
    puts("usage: mgensys scriptfile");
    return 0;
  }
  if (!(prg = MGSProgram_create(argv[1]))) {
    printf("error: couldn't open script file \"%s\"\n", argv[1]);
    return 1;
  }
  fd_out = open_audio_dev(NAME_OUT, O_WRONLY, &srate);
  make_audio(fd_out, srate, prg);
  close(fd_out);
  MGSProgram_destroy(prg);
  return 0;
}
