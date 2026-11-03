已实现功能：读取sd卡并循环播放一首歌
模块：stm32f407vgt6 mini板，pcm5102模块

pcm5102模块接线：
Vin --> +5V
GND --> GND
LRCK --> PB12
DIN --> PB15
BCK --> PB13
SCK --> GND

注意：
pcm5102上的Vin要用电源模块接，主控板上的5V实测只有2.8V

