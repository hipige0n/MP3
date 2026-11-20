#ifndef MP3_PLAYER_H
#define MP3_PLAYER_H
/* 0 stopped, 1 playing, 2 paused, -1 error. */
int MP3_Start(const char *path);
void MP3_Process(void);
void MP3_Stop(void);
int MP3_State(void);
int MP3_Pause(void);
int MP3_Resume(void);
int MP3_Volume(void);
void MP3_SetVolume(int volume);
#endif
