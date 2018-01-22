struct SGSWAVFile;
typedef struct SGSWAVFile SGSWAVFile;

SGSWAVFile *SGS_begin_wav_file(const char *fpath, ushort channels, uint srate);
int SGS_end_wav_file(SGSWAVFile *wf);
uchar SGS_wav_file_write(SGSWAVFile *wf, const short *buf, uint samples);
