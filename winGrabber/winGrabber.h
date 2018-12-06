
struct DShowDevice
{
	int types[20];	//0-Video device, 1-Audio device
	char *deviceNames[20];
	int count;
	char *deviceNameUTFs[20];
};

int hasVideo;
int hasAudio;
int fps;
int width;
int height;

AVFormatContext *fmt_ctx;
AVCodecContext *pVideoCodecCtx;
AVCodecContext *pAudioCodecCtx;
AVFormatContext *ofmt_ctx;
int video_stream_index;
AVStream *videoOutStream;
AVStream *audioOutStream;
int outVideoIndex;
int inAudioIndex;
int outAudioIndex;
int bitrate;
int stopGrab;
SwrContext *swr_ctx = NULL;
AVAudioFifo *audioFifo = NULL;
SwsContext *img_convert_ctx = NULL;

void setResolution(int w, int h);
void setFPS(int f);
void setBitrate(int b);

void initFFmpeg();
struct DShowDevice *listDshowDevice();	//list all the camera and microphone devices on this computer.
int initGDIGrab(char *handleName);	//Capture the desktop or program's window in running. 

int initDshow(char *deviceName);

int initOutput(char *url, int isRtmp);	//output url or local file.
void pushToURL();
void release();

void DwordToHexString(DWORD dwValue, char *szBuf);

#define TEST_FRMAE_COUNT 500