#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
#include "ff.h"
#include "fat_utf8.h"
#include "main.h"
#include "mp3_player.h"
#include <stdio.h>
#include <string.h>
#define HALF 8192U
static I2S_HandleTypeDef audio;
static DMA_HandleTypeDef dma;
static mp3dec_t decoder __attribute__((section(".bss.ccm_audio")));
static FIL song;
static uint8_t input[16384] __attribute__((section(".bss.ccm_audio")));
static int16_t pcm[2304] __attribute__((section(".bss.ccm_audio")));
static int16_t output[HALF*2] __attribute__((section(".bss.audio_dma"), aligned(4)));
static unsigned used,pos,available;
static int eof,rate,channels,failed;
static volatile uint32_t events;
static volatile int dma_error;
void DMA1_Stream4_IRQHandler(void){HAL_DMA_IRQHandler(&dma);}
void HAL_I2S_TxHalfCpltCallback(I2S_HandleTypeDef *h){if(h==&audio)events++;}
void HAL_I2S_TxCpltCallback(I2S_HandleTypeDef *h){if(h==&audio)events++;}
void HAL_I2S_ErrorCallback(I2S_HandleTypeDef *h){if(h==&audio)dma_error=1;}
static int next_frame(void)
{
 for(;;){
  UINT nread;
  if(!eof && used<sizeof(input)){
   if(f_read(&song,input+used,sizeof(input)-used,&nread)!=FR_OK){failed=1;return 0;}
   used+=nread;if(!nread)eof=1;
  }
  if(!used)return 0;
  mp3dec_frame_info_t info;
  int n=mp3dec_decode_frame(&decoder,input,used,pcm,&info);
  if(info.frame_bytes<0 || (unsigned)info.frame_bytes>used){failed=1;return 0;}
  if(info.frame_bytes){used-=info.frame_bytes;memmove(input,input+info.frame_bytes,used);}
  else if(eof)return 0;
  else {memmove(input,input+1,--used);}
  if(n>0){
   if((info.channels!=1 && info.channels!=2)||(info.hz!=44100 && info.hz!=48000)){failed=1;return 0;}
   if(rate && (rate!=info.hz || channels!=info.channels)){failed=1;return 0;}
   rate=info.hz;channels=info.channels;pos=0;available=n;return 1;
  }
 }
}
static unsigned fill(int16_t *dest)
{
 unsigned written=0;
 while(written<HALF){
  if(pos>=available && !next_frame())break;
  /* 1/8 digital gain; duplicate mono to both channels. */
  dest[written++]=pcm[pos*channels]/8;
  dest[written++]=pcm[pos*channels+(channels==2)]/8;
  pos++;
 }
 memset(dest+written,0,(HALF-written)*sizeof(*dest));return written;
}
static int hardware_init(void)
{
 RCC_PeriphCLKInitTypeDef clock={0};
 clock.PeriphClockSelection=RCC_PERIPHCLK_I2S;
 clock.PLLI2S.PLLI2SN=rate==44100?271:258;
 clock.PLLI2S.PLLI2SR=rate==44100?2:3;
 if(HAL_RCCEx_PeriphCLKConfig(&clock)!=HAL_OK)return 0;
 __HAL_RCC_SPI2_CLK_ENABLE();__HAL_RCC_GPIOB_CLK_ENABLE();__HAL_RCC_DMA1_CLK_ENABLE();
 GPIO_InitTypeDef pins={0};pins.Pin=GPIO_PIN_12|GPIO_PIN_13|GPIO_PIN_15;
 pins.Mode=GPIO_MODE_AF_PP;pins.Pull=GPIO_NOPULL;pins.Speed=GPIO_SPEED_FREQ_VERY_HIGH;pins.Alternate=GPIO_AF5_SPI2;
 HAL_GPIO_Init(GPIOB,&pins);
 audio.Instance=SPI2;audio.Init.Mode=I2S_MODE_MASTER_TX;audio.Init.Standard=I2S_STANDARD_PHILIPS;
 /* Match the hardware-verified minimal program: 16-bit PCM in 32-bit slots. */
 audio.Init.DataFormat=I2S_DATAFORMAT_16B_EXTENDED;audio.Init.MCLKOutput=I2S_MCLKOUTPUT_DISABLE;
 audio.Init.AudioFreq=rate;audio.Init.CPOL=I2S_CPOL_LOW;audio.Init.ClockSource=I2S_CLOCK_PLL;audio.Init.FullDuplexMode=I2S_FULLDUPLEXMODE_DISABLE;
 if(HAL_I2S_Init(&audio)!=HAL_OK)return 0;
 dma.Instance=DMA1_Stream4;dma.Init.Channel=DMA_CHANNEL_0;dma.Init.Direction=DMA_MEMORY_TO_PERIPH;
 dma.Init.PeriphInc=DMA_PINC_DISABLE;dma.Init.MemInc=DMA_MINC_ENABLE;
 dma.Init.PeriphDataAlignment=DMA_PDATAALIGN_HALFWORD;dma.Init.MemDataAlignment=DMA_MDATAALIGN_HALFWORD;
 dma.Init.Mode=DMA_CIRCULAR;dma.Init.Priority=DMA_PRIORITY_HIGH;dma.Init.FIFOMode=DMA_FIFOMODE_DISABLE;
 if(HAL_DMA_Init(&dma)!=HAL_OK)return 0;
 __HAL_LINKDMA(&audio,hdmatx,dma);
 HAL_NVIC_SetPriority(DMA1_Stream4_IRQn,1,0);HAL_NVIC_EnableIRQ(DMA1_Stream4_IRQn);
 return 1;
}
static unsigned counts[2];
static uint32_t served, deadline;
static int state, file_open, hardware_ready;
int MP3_State(void) { return state; }
void MP3_Stop(void)
{
 if(hardware_ready){
  HAL_I2S_DMAStop(&audio);
  HAL_NVIC_DisableIRQ(DMA1_Stream4_IRQn);
  HAL_DMA_DeInit(&dma);HAL_I2S_DeInit(&audio);
  hardware_ready=0;
 }
 if(file_open){(void)f_close(&song);file_open=0;}
 state=0;
}
static void play_error(const char *reason)
{
 printf("[PLAY FAIL] %s\r\n",reason);MP3_Stop();state=-1;
}
int MP3_Pause(void)
{
 if(state!=1)return 0;
 /* Freeze the DMA read position; buffers and decoder remain intact. */
 if(HAL_I2S_DMAPause(&audio)!=HAL_OK){play_error("pause");return 0;}
 __HAL_I2S_DISABLE(&audio);
 state=2;printf("[PLAY PAUSE]\r\n");return 1;
}
int MP3_Resume(void)
{
 if(state!=2)return 0;
 deadline=HAL_GetTick();
 if(HAL_I2S_DMAResume(&audio)!=HAL_OK){play_error("resume");return 0;}
 __HAL_I2S_ENABLE(&audio);
 state=1;printf("[PLAY RESUME]\r\n");return 1;
}
int MP3_Start(const char *path)
{
 static TCHAR unicode_song[512];
 MP3_Stop();
 if(!fat_path_utf16(path,unicode_song,512)){play_error("path");return 0;}
 FRESULT r=f_open(&song,unicode_song,FA_READ);UINT n;DWORD offset=0;
 if(r!=FR_OK){play_error("open");return 0;}
 file_open=1;
 used=pos=available=0;eof=rate=channels=failed=0;events=0;dma_error=0;served=0;
 if(f_read(&song,input,10,&n)!=FR_OK){play_error("header read");return 0;}
 if(n==10 && !memcmp(input,"ID3",3)){
  if((input[6]|input[7]|input[8]|input[9])&128){play_error("ID3 size");return 0;}
  offset=10U+((DWORD)input[6]<<21)+((DWORD)input[7]<<14)+((DWORD)input[8]<<7)+input[9];
  if(input[3]==4 && (input[5]&16))offset+=10;
 }
 if(offset>f_size(&song)||f_lseek(&song,offset)!=FR_OK){play_error("ID3 seek");return 0;}
 mp3dec_init(&decoder);
 counts[0]=fill(output);counts[1]=fill(output+HALF);
 if(failed || !counts[0]){play_error("invalid MP3 (44.1/48 kHz only)");return 0;}
 if(!hardware_init()){
  HAL_NVIC_DisableIRQ(DMA1_Stream4_IRQn);
  if(dma.Instance)HAL_DMA_DeInit(&dma);
  if(audio.Instance)HAL_I2S_DeInit(&audio);
  play_error("I2S/DMA init");return 0;
 }
 hardware_ready=1;
 if(HAL_I2S_Transmit_DMA(&audio,(uint16_t*)output,HALF*2)!=HAL_OK){play_error("DMA start");return 0;}
 deadline=HAL_GetTick();state=1;
 printf("[PLAY] %s rate=%d channels=%d gain=1/8 slot=32bit\r\n",path,rate,channels);
 return 1;
}
void MP3_Process(void)
{
 if(state!=1)return;
 if(dma_error){play_error("DMA/I2S error");return;}
 uint32_t current=events;
 if(current==served){
  if(HAL_GetTick()-deadline>1000)play_error("DMA timeout");
  return;
 }
 if(current-served!=1){play_error("buffer underrun");return;}
 unsigned half=served&1U;
 if(!counts[half]){printf("[PLAY DONE] EOF\r\n");MP3_Stop();return;}
 counts[half]=fill(output+half*HALF);
 if(failed){play_error("SD read or format change");return;}
 if(events!=current){play_error("buffer refill missed deadline");return;}
 served=current;deadline=HAL_GetTick();
 if(served==1 || served%64U==0)
  printf("[DMA RUN] halves=%lu\r\n",(unsigned long)served);
}
