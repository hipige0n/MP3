#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
#include "ff.h"
#include "sdio.h"
#include <stdio.h>
#include <string.h>
static mp3dec_t decoder;
static unsigned char input[16384];
static mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
static FIL song;
void MP3_Verify(const char *path)
{
    FRESULT r=f_open(&song,path,FA_READ);
    UINT got;
    unsigned used=0,frames=0;
    uint64_t samples_total=0,audio_us=0;
    uint32_t skipped=0,empty_frames=0,tail=0,changes=0,read_ms=0,read_bytes=0;
    int last_hz=0,last_channels=0;
    int eof=0,peak=0;
    uint32_t decode_ms=0, started=0;
    if(r!=FR_OK){printf("[MP3 FAIL] open=%u\r\n",r);return;}
    /* Skip ID3v2 by its synchsafe size, including optional v2.4 footer. */
    r=f_read(&song,input,10,&got);
    if(r!=FR_OK)goto fail;
    DWORD offset=0;
    if(got==10 && !memcmp(input,"ID3",3)) {
        if((input[6]|input[7]|input[8]|input[9])&0x80){r=FR_INVALID_OBJECT;goto fail;}
        offset=10U+((DWORD)input[6]<<21)+((DWORD)input[7]<<14)+((DWORD)input[8]<<7)+input[9];
        if(input[3]==4 && (input[5]&0x10))offset+=10;
        if(offset>f_size(&song)){r=FR_INVALID_OBJECT;goto fail;}
    }
    r=f_lseek(&song,offset);if(r!=FR_OK)goto fail;
    printf("[MP3] %s ID3_skip=%lu; decode entire file to EOF\r\n",path,(unsigned long)offset);
    mp3dec_init(&decoder);
    started=HAL_GetTick();
    while(1) {
        if(!eof && used<sizeof(input)) {
            uint32_t read_tick=HAL_GetTick();
            r=f_read(&song,input+used,sizeof(input)-used,&got);
            read_ms+=HAL_GetTick()-read_tick;
            read_bytes+=got;
            if(r!=FR_OK)goto fail;
            used+=got;if(!got)eof=1;
        }
        if(!used)break;
        mp3dec_frame_info_t info;
        uint32_t tick=HAL_GetTick();
        int n=mp3dec_decode_frame(&decoder,input,(int)used,pcm,&info);
        decode_ms+=HAL_GetTick()-tick;
        if(info.frame_bytes<0 || (unsigned)info.frame_bytes>used){r=FR_INT_ERR;goto fail;}
        if(n>0) {
            if(info.hz<=0 || info.channels<1 || info.channels>2 || n>1152){r=FR_INT_ERR;goto fail;}
            if(frames && (last_hz!=info.hz || last_channels!=info.channels)) changes++;
            last_hz=info.hz;last_channels=info.channels;
            audio_us+=(uint64_t)n*1000000U/(unsigned)info.hz;
            if(!frames)printf("[MP3 FORMAT] rate=%d Hz channels=%d bitrate=%d kbps layer=%d\r\n",info.hz,info.channels,info.bitrate_kbps,info.layer);
            for(int i=0;i<n*info.channels;i++){int v=pcm[i];if(v<0)v=-v;if(v>peak)peak=v;}
            frames++;samples_total+=(unsigned)n;
            if(frames%1000U==0) printf("[MP3 PROGRESS] frames=%u audio_ms=%lu file_pos=%lu/%lu\r\n",frames,(unsigned long)(audio_us/1000U),(unsigned long)f_tell(&song),(unsigned long)f_size(&song));
        }
        if(n==0 && info.frame_bytes) {
            if(info.hz>0) empty_frames++;
            else skipped+=(unsigned)info.frame_bytes;
        }
        if(info.frame_bytes) {
            used-=(unsigned)info.frame_bytes;
            memmove(input,input+info.frame_bytes,used);
        } else if(eof){tail=used;break;}
        else if(used==sizeof(input)) {memmove(input,input+1,--used);skipped++;}
    }
    uint32_t total_ms=HAL_GetTick()-started;
    int reached_eof=eof && f_eof(&song);
    r=f_close(&song);
    if(r!=FR_OK){printf("[MP3 FAIL] close=%u\r\n",r);return;}
    printf("[MP3 %s] EOF=%d decoded_frames=%u samples_per_channel=%llu peak=%d\r\n",
           frames && reached_eof?"PASS":"FAIL",reached_eof,frames,(unsigned long long)samples_total,peak);
    printf("[MP3 TIME] audio_ms=%llu total_ms=%lu read_ms=%lu decode_ms=%lu read_bytes=%lu\r\n",
           (unsigned long long)(audio_us/1000U),(unsigned long)total_ms,(unsigned long)read_ms,(unsigned long)decode_ms,(unsigned long)read_bytes);
    printf("[MP3 CHECK] no_pcm_frames=%lu skipped_bytes=%lu tail_bytes=%lu format_changes=%lu\r\n",
           (unsigned long)empty_frames,(unsigned long)skipped,(unsigned long)tail,(unsigned long)changes);
    printf("[MP3 NOTE] PASS means decoded audio and reached file EOF; inspect CHECK counters for anomalies.\r\n");
    printf("PCM is discarded after each frame; no audio output yet.\r\n");return;
fail:
    printf("[MP3 FAIL] FAT result=%u\r\n",r);(void)f_close(&song);
}
