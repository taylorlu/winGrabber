#include <stdio.h>
#include <string.h>
#include "libavfilter/avfilter.h"
#include "libavfilter/buffersink.h"
#include "libavdevice/avdevice.h"
#include "libavutil/avstring.h"
#include "libavutil/eval.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/imgutils.h"
#include "libavutil/avassert.h"
#include "libswscale/swscale.h"
#include "libswscale/swscale_internal.h"
#include "libavutil/pixfmt.h"
#include "libavutil/samplefmt.h"
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavutil/audio_fifo.h"
#include <windows.h>
#include <stdlib.h>
#include "libavdevice/dshow_capture.h"
#include "libavresample/avresample.h"
#include "libswresample/swresample.h"
#include "winGrabber.h"

wchar_t * ANSIToUnicode(const char* str) {
	int textlen;
	wchar_t * result;
	textlen = MultiByteToWideChar(CP_ACP, 0, str, -1, NULL, 0);
	result = (wchar_t *)malloc((textlen + 1)*sizeof(wchar_t));
	memset(result, 0, (textlen + 1)*sizeof(wchar_t));
	MultiByteToWideChar(CP_ACP, 0, str, -1, (LPWSTR)result, textlen);
	return result;
}

char * UnicodeToANSI(const wchar_t* str) {
	char* result;
	int textlen;
	textlen = WideCharToMultiByte(CP_ACP, 0, str, -1, NULL, 0, NULL, NULL);
	result = (char *)malloc((textlen + 1)*sizeof(char));
	memset(result, 0, sizeof(char)* (textlen + 1));
	WideCharToMultiByte(CP_ACP, 0, str, -1, result, textlen, NULL, NULL);
	return result;
}

wchar_t * UTF8ToUnicode(const char* str) {
	int textlen;
	wchar_t * result;
	textlen = MultiByteToWideChar(CP_UTF8, 0, str, -1, NULL, 0);
	result = (wchar_t *)malloc((textlen + 1)*sizeof(wchar_t));
	memset(result, 0, (textlen + 1)*sizeof(wchar_t));
	MultiByteToWideChar(CP_UTF8, 0, str, -1, (LPWSTR)result, textlen);
	return result;
}

char * UnicodeToUTF8(const wchar_t* str) {
	char* result;
	int textlen;
	textlen = WideCharToMultiByte(CP_UTF8, 0, str, -1, NULL, 0, NULL, NULL);
	result = (char *)malloc((textlen + 1)*sizeof(char));
	memset(result, 0, sizeof(char)* (textlen + 1));
	WideCharToMultiByte(CP_UTF8, 0, str, -1, result, textlen, NULL, NULL);
	return result;
}

char* w2m(const wchar_t* wcs) {
	int len;
	char* buf;
	len = wcstombs(NULL, wcs, 0);
	if (len == 0)
		return NULL;
	buf = (char *)malloc(sizeof(char)*(len + 1));
	memset(buf, 0, sizeof(char)*(len + 1));
	len = wcstombs(buf, wcs, len + 1);
	return buf;
}

wchar_t* m2w(const char* mbs) {
	int len;
	wchar_t* buf;
	len = mbstowcs(NULL, mbs, 0);
	if (len == 0)
		return NULL;
	buf = (wchar_t *)malloc(sizeof(wchar_t)*(len + 1));
	memset(buf, 0, sizeof(wchar_t)*(len + 1));
	len = mbstowcs(buf, mbs, len + 1);
	return buf;
}

char* ANSIToUTF8(const char* str) {
	return UnicodeToUTF8(ANSIToUnicode(str));
}

char* UTF8ToANSI(const char* str) {
	return UnicodeToANSI(UTF8ToUnicode(str));
}

void DwordToHexString(DWORD dwValue, char *szBuf) {
	BYTE bValue;
	BYTE k;

	*szBuf = '0';
	*(++szBuf) = 'x';
	++szBuf;
	for (k = 0; k < 8; ++k) {
		bValue = (BYTE)((dwValue & 0xF0000000) >> 28);
		if (bValue == 0) {
			dwValue <<= 4;
			*szBuf = '0';
			szBuf++;
			continue;
		}
		if (bValue < 10) {
			*szBuf = bValue + '0';
		}
		else {
			*szBuf = (bValue - 10) + 'A';
		}
		dwValue <<= 4;
		szBuf++;
	}
	*szBuf = '\0';
}

void sws_process(AVFrame *src, AVFrame *dst) {

	if (img_convert_ctx == NULL) {
		img_convert_ctx = sws_alloc_context();
		av_opt_set_int(img_convert_ctx, "sws_flags", SWS_BICUBIC | SWS_PRINT_INFO, 0);
		av_opt_set_int(img_convert_ctx, "srcw", src->width, 0);
		av_opt_set_int(img_convert_ctx, "srch", src->height, 0);
		av_opt_set_int(img_convert_ctx, "src_format", src->format, 0);
		av_opt_set_int(img_convert_ctx, "dstw", dst->width, 0);
		av_opt_set_int(img_convert_ctx, "dsth", dst->height, 0);
		av_opt_set_int(img_convert_ctx, "dst_format", dst->format, 0);
		sws_init_context(img_convert_ctx, NULL, NULL);
	}
	sws_scale(img_convert_ctx, src->data, src->linesize, 0, src->height, dst->data, dst->linesize);
}

static void setup_array(uint8_t* out[SWR_CH_MAX], AVFrame* in_frame, int format, int samples) {

	if (av_sample_fmt_is_planar(format)) {
		int i; int plane_size = av_get_bytes_per_sample((format & 0xFF)) * samples; 
		format &= 0xFF;
		in_frame->data[0] + i*plane_size;
		for (i = 0; i < in_frame->channels; i++) {
			out[i] = in_frame->data[i];
		}
	}
	else {
		out[0] = in_frame->data[0];
	}
}

static int encode_write_frame(AVFrame *filt_frame, unsigned int stream_index, AVFormatContext *ofmt_ctx) {

	int ret;
	AVPacket enc_pkt;

	int got = 1;
	int *got_frame = &got;

	/* encode filtered frame */
	enc_pkt.data = NULL;
	enc_pkt.size = 0;
	av_init_packet(&enc_pkt);

	if (hasVideo && stream_index == video_stream_index) {
		ret = avcodec_encode_video2(ofmt_ctx->streams[stream_index]->codec, &enc_pkt, filt_frame, got_frame);
	}
	else {
		ret = avcodec_encode_audio2(ofmt_ctx->streams[stream_index]->codec, &enc_pkt, filt_frame, got_frame);
	}
	
	av_frame_free(&filt_frame);
	if (ret < 0)
		return ret;
	if (!(*got_frame))
		return 0;

	/* prepare packet for muxing */
	enc_pkt.stream_index = stream_index;
	av_packet_rescale_ts(&enc_pkt,
		ofmt_ctx->streams[stream_index]->codec->time_base,
		ofmt_ctx->streams[stream_index]->time_base);

	av_log(NULL, AV_LOG_DEBUG, "Muxing frame\n");
	/* mux encoded frame */
	ret = av_interleaved_write_frame(ofmt_ctx, &enc_pkt);
	return ret;
}

void initFFmpeg() {

	av_register_all();
	avfilter_register_all();
	avdevice_register_all();
	avformat_network_init();

	hasVideo = 0;
	hasAudio = 0;
	video_stream_index = 0;
	stopGrab = 0;
}

void setResolution(int w, int h) {
	width = w;
	height = h;
}

void setFPS(int f) {
	fps = f;
}

void setBitrate(int b) {
	bitrate = b;
}

int initGDIGrab(char *handleName) {

	//char video_size[20] = { 0 };
	//sprintf(video_size, "%dx%d", width, height);
	char frameRate[20] = {0};
	sprintf(frameRate, "%d", fps);
	char *title = handleName;

	AVDictionary* options = NULL;
	av_dict_set(&options, "framerate", frameRate, 0);
	//av_dict_set(&options, "video_size", video_size, 0);

	fmt_ctx = avformat_alloc_context();
	AVCodec *dec;
	int ret = 0;

	AVInputFormat *ifmt = av_find_input_format("gdigrab");
	if ((ret = avformat_open_input(&fmt_ctx, title, ifmt, &options)) != 0){
		printf("Couldn't open input stream.\n");
		return ret;
	}
	if ((ret = avformat_find_stream_info(fmt_ctx, NULL)) < 0) {
		printf("Cannot find stream information\n");
		return ret;
	}

	/* select the video stream */
	ret = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0);
	if (ret < 0) {
		printf("Cannot find a video stream in the input file\n");
		return ret;
	}
	video_stream_index = ret;
	pVideoCodecCtx = fmt_ctx->streams[video_stream_index]->codec;
	hasVideo = 1;

	/* init the video decoder */
	if ((ret = avcodec_open2(pVideoCodecCtx, dec, NULL)) < 0) {
		printf("Cannot open video decoder\n");
		return ret;
	}
	return ret;
}

int initOutput(char *url, int isRtmp) {

	int ret = 0;
	if (isRtmp) {
		avformat_alloc_output_context2(&ofmt_ctx, NULL, "flv", url);
	}
	else {
		avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, url);
	}
	if (!ofmt_ctx) {
		fprintf(stderr, "Could not create output context\n");
		ret = AVERROR_UNKNOWN;
		return ret;
	}

	if (hasVideo) {
		AVCodec *videoEncoder = avcodec_find_encoder(AV_CODEC_ID_H264);
		videoOutStream = avformat_new_stream(ofmt_ctx, videoEncoder);
		videoOutStream->time_base = (AVRational){ 1, 90000 };
		AVCodecContext *videoEnc_ctx = videoOutStream->codec;
		videoEnc_ctx->width = pVideoCodecCtx->width;
		videoEnc_ctx->height = pVideoCodecCtx->height;
		videoEnc_ctx->pix_fmt = PIX_FMT_YUV420P;
		videoEnc_ctx->time_base = (AVRational){ 1, fps };
		videoEnc_ctx->bit_rate = bitrate;
		outVideoIndex = videoOutStream->index;

		if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
			videoEnc_ctx->flags |= CODEC_FLAG_GLOBAL_HEADER;

		ret = avcodec_open2(videoEnc_ctx, videoEncoder, NULL);
		if (ret < 0) {
			av_log(NULL, AV_LOG_ERROR, "Cannot open video encoder for stream \n");
			return ret;
		}
	}

	if (hasAudio) {
		AVCodec *audioEncoder = avcodec_find_encoder(AV_CODEC_ID_AAC);
		audioOutStream = avformat_new_stream(ofmt_ctx, audioEncoder);
		audioOutStream->time_base = (AVRational){ 1, 90000 };
		AVCodecContext *audioEnc_ctx = audioOutStream->codec;

		audioEnc_ctx->sample_rate = pAudioCodecCtx->sample_rate;
		audioEnc_ctx->channels = pAudioCodecCtx->channels;
		audioEnc_ctx->time_base = (AVRational){ 1, audioEnc_ctx->sample_rate };
		audioEnc_ctx->channel_layout = av_get_default_channel_layout(pAudioCodecCtx->channels);
		audioEnc_ctx->sample_fmt = audioEncoder->sample_fmts[0];
		audioEnc_ctx->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
		outAudioIndex = audioOutStream->index;

		if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
			audioEnc_ctx->flags |= CODEC_FLAG_GLOBAL_HEADER;

		ret = avcodec_open2(audioEnc_ctx, audioEncoder, NULL);
		if (ret < 0) {
			return ret;
		}
		swr_ctx = swr_alloc();
		av_opt_set_int(swr_ctx, "ich", pAudioCodecCtx->channels, 0);
		av_opt_set_int(swr_ctx, "och", audioEnc_ctx->channels, 0);
		av_opt_set_int(swr_ctx, "in_sample_rate", pAudioCodecCtx->sample_rate, 0);
		av_opt_set_int(swr_ctx, "out_sample_rate", audioEnc_ctx->sample_rate, 0);
		av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", pAudioCodecCtx->sample_fmt, 0);
		av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", audioEnc_ctx->sample_fmt, 0);
		swr_init(swr_ctx);
		audioFifo = av_audio_fifo_alloc(audioEnc_ctx->sample_fmt, audioEnc_ctx->channels, 1);
	}

	if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
		if ((ret = avio_open(&ofmt_ctx->pb, url, AVIO_FLAG_WRITE)) < 0) {
			printf("can not open the out put file handle!\n");
			return ret;
		}
	}
	return ret;
}

void pushCallBack() {
	printf("Error occurred when writing to url.\r\n");
}

DWORD WINAPI runLoop(LPVOID threadNo) {

	int ret = avformat_write_header(ofmt_ctx, NULL);
	if (ret < 0) {
		pushCallBack();
		fprintf(stderr, "Error occurred when opening output file\n");
		return ret;
	}

	AVPacket pkt;
	int got_frame = 0;
	int got_packet = 0;
	int64_t audio_pts = -1;
	int64_t ts_offset = 0;
	int64_t next_dts[10] = { 0 };
	int64_t durations[10] = { 0 };
	int64_t delta_abs = 0;
	int vibrate_count = 0;

	int audioIndex = 1;
	int videoIndex = 0;
	for (int i = 0; i < ofmt_ctx->nb_streams; i++) {
		if (AVMEDIA_TYPE_VIDEO == ofmt_ctx->streams[i]->codec->codec_type) {
			videoIndex = i;
		}
		else if (AVMEDIA_TYPE_AUDIO == ofmt_ctx->streams[i]->codec->codec_type) {
			audioIndex = i;
		}
	}

	while (!stopGrab) {
		if ((ret = av_read_frame(fmt_ctx, &pkt)) < 0) {
			continue;
		}
		AVStream *in_stream = fmt_ctx->streams[pkt.stream_index];
		int type = in_stream->codec->codec_type;
		int stream_index = pkt.stream_index;
		if (type == AVMEDIA_TYPE_VIDEO) {
			stream_index = videoIndex;
		}
		else {
			stream_index = audioIndex;
		}
		if (pkt.dts != AV_NOPTS_VALUE) {
			pkt.dts -= in_stream->start_time;
		}
		if (pkt.pts != AV_NOPTS_VALUE) {
			pkt.pts -= in_stream->start_time;
		}

		/************************************************************************/
		/*                                                                      */
		/************************************************************************/

		if (hasVideo && video_stream_index == pkt.stream_index) {
			int64_t inDts = pkt.dts;
			int64_t inPts = pkt.pts;
			AVFrame *pFrame = av_frame_alloc();
			if (!pFrame) {
				pushCallBack();
				ret = AVERROR(ENOMEM);
				break;
			}
			ret = avcodec_decode_video2(pVideoCodecCtx, pFrame, &got_frame, &pkt);
			av_free_packet(&pkt);
			if (ret < 0 || !got_frame) {
				av_frame_free(&pFrame);
				continue;
			}
			pFrame->pts = av_frame_get_best_effort_timestamp(pFrame);

			AVFrame *scaledFrame = avcodec_alloc_frame();
			scaledFrame->width = pVideoCodecCtx->width;
			scaledFrame->height = pVideoCodecCtx->height;
			scaledFrame->format = PIX_FMT_YUV420P;

			static uint8_t *buffer2 = NULL;
			if (buffer2 == NULL) {
				int size2 = avpicture_get_size(scaledFrame->format, scaledFrame->width, scaledFrame->height);
				buffer2 = (uint8_t *)av_malloc(size2);
			}
			avpicture_fill((AVPicture *)scaledFrame, buffer2, scaledFrame->format, scaledFrame->width, scaledFrame->height);
			sws_process(pFrame, scaledFrame);
			scaledFrame->pict_type = AV_PICTURE_TYPE_NONE;
			scaledFrame->pts = pFrame->pts;
			av_frame_free(&pFrame);

			AVPacket packet;
			packet.data = NULL;
			packet.size = 0;
			av_init_packet(&packet);
			ret = avcodec_encode_video2(ofmt_ctx->streams[outVideoIndex]->codec, &packet, scaledFrame, &got_packet);

			if (ret < 0) {
				av_frame_free(&scaledFrame);
				av_free_packet(&packet);
				continue;
			}
			if (got_packet) {
				/* prepare packet for muxing */
				packet.stream_index = outVideoIndex;
				packet.dts = av_rescale_q(inDts, in_stream->time_base, videoOutStream->time_base);
				packet.pts = av_rescale_q(inPts, in_stream->time_base, videoOutStream->time_base);
				av_interleaved_write_frame(ofmt_ctx, &packet);
			}

			av_free_packet(&packet);
			av_frame_free(&scaledFrame);
		}
		else if (hasAudio && inAudioIndex == pkt.stream_index) {//hasAudio && inAudioIndex == pkt.stream_index

			av_packet_rescale_ts(&pkt, in_stream->time_base, in_stream->codec->time_base);
			AVFrame *pFrame = av_frame_alloc();

			ret = avcodec_decode_audio4(pAudioCodecCtx, pFrame, &got_frame, &pkt);
			av_free_packet(&pkt);
			if (ret < 0 || !got_frame) {
				av_frame_free(&pFrame);
				continue;
			}

			pFrame->pts = av_frame_get_best_effort_timestamp(pFrame);
			AVFrame *resampleFrame = av_frame_alloc();
			resampleFrame->channel_layout = audioOutStream->codec->channel_layout;
			resampleFrame->format = audioOutStream->codec->sample_fmt;
			resampleFrame->channels = audioOutStream->codec->channels;
			resampleFrame->sample_rate = audioOutStream->codec->sample_rate;
			resampleFrame->nb_samples = av_rescale_rnd(pFrame->nb_samples,
				audioOutStream->codec->sample_rate, in_stream->codec->sample_rate, AV_ROUND_UP);//swr_get_delay(swr_ctx, audioStream->codec->sample_rate) + 
			int nb_samples = resampleFrame->nb_samples;
			ret = av_samples_alloc(resampleFrame->data,
				&resampleFrame->linesize[0],
				audioOutStream->codec->channels,
				resampleFrame->nb_samples,
				audioOutStream->codec->sample_fmt, 0);

			if (ret < 0) {
				av_log(NULL, AV_LOG_WARNING, "Audio resample: Could not allocate samples Buffer\n");
				av_frame_free(&pFrame);
				av_free(resampleFrame->data[0]);
				av_frame_free(&resampleFrame);
				continue;
			}

			uint8_t* m_ain[32];
			setup_array(m_ain, pFrame, audioOutStream->codec->sample_fmt, pFrame->nb_samples);

			int len = swr_convert(swr_ctx, resampleFrame->data, resampleFrame->nb_samples, (const uint8_t**)m_ain, pFrame->nb_samples);
			if (len < 0) {
				av_log(NULL, AV_LOG_WARNING, "swr_convert failed.\r\n");
				av_frame_free(&pFrame);
				av_free(resampleFrame->data[0]);
				av_frame_free(&resampleFrame);
				continue;
			}

			resampleFrame->pts = av_rescale(pFrame->pts, in_stream->codec->sample_rate, audioOutStream->codec->sample_rate);
			audio_pts = resampleFrame->pts;

			av_audio_fifo_write(audioFifo, (void **)resampleFrame->data, resampleFrame->nb_samples);
			av_frame_free(&pFrame);
			av_free(resampleFrame->data[0]);
			av_frame_free(&resampleFrame);

			while (av_audio_fifo_size(audioFifo) >= audioOutStream->codec->frame_size) {

				AVFrame *resampleFrame2 = av_frame_alloc();
				resampleFrame2->nb_samples = audioOutStream->codec->frame_size;
				resampleFrame2->channels = audioOutStream->codec->channels;
				resampleFrame2->channel_layout = audioOutStream->codec->channel_layout;
				resampleFrame2->format = audioOutStream->codec->sample_fmt;
				resampleFrame2->sample_rate = audioOutStream->codec->sample_rate;

				ret = av_frame_get_buffer(resampleFrame2, 0);
				if (ret >= 0) {
					ret = av_audio_fifo_read(audioFifo, (void **)resampleFrame2->data, audioOutStream->codec->frame_size);
					len = av_audio_fifo_size(audioFifo);
				}
				if (ret < 0) {
					av_frame_free(&resampleFrame2);
					continue;
				}

				resampleFrame2->pts = audio_pts;
				audio_pts += resampleFrame2->nb_samples;

				pkt.data = NULL;
				pkt.size = 0;
				av_init_packet(&pkt);
				ret = avcodec_encode_audio2(audioOutStream->codec, &pkt, resampleFrame2, &got_frame);

				if (ret < 0 || !got_frame) {
					av_free_packet(&pkt);
					av_frame_free(&resampleFrame2);
					continue;
				}
				/* prepare packet for muxing */
				pkt.stream_index = outAudioIndex;

				av_packet_rescale_ts(&pkt, audioOutStream->codec->time_base, audioOutStream->time_base);
				ret = av_interleaved_write_frame(ofmt_ctx, &pkt);
				av_free_packet(&pkt);
				av_frame_free(&resampleFrame2);

				if (ret < 0) {
					av_log(NULL, AV_LOG_WARNING, "Error muxing audio packet.\r\n");
					continue;
				}
			}
		}
	}
	stopGrab = 0;
}

void pushToURL() {
	CreateThread(NULL, 0, runLoop, NULL, 0, NULL);
}

void release() {
	stopGrab = 1;
	while (stopGrab!=0) {
		Sleep(100);
		break;
	}
	av_write_trailer(ofmt_ctx);
	avformat_close_input(&ofmt_ctx);
}

struct DShowDevice *listDshowDevice() {
	AVFormatContext *pFmt_ctx = avformat_alloc_context();
	AVDictionary *options = NULL;
	av_dict_set(&options, "list_devices", "true", 0);
	AVInputFormat *iformat = av_find_input_format("dshow");
	
	int ret = avformat_open_input(&pFmt_ctx, "video=dummy", iformat, &options);
	struct DShowDevice *ctx = pFmt_ctx->priv_data;
	for (int i = 0; i < ctx->count;i++) {
		char utfName[100] = { 0 };
		strcpy(utfName, ctx->deviceNames[i]);
		ctx->deviceNameUTFs[i] = UTF8ToANSI(utfName);
	}
	return ctx;
}

int initDshow(char *deviceName) {

	char video_size[20] = { 0 };
	sprintf(video_size, "%dx%d", width, height);
	char frameRate[20] = { 0 };
	sprintf(frameRate, "%d", fps);

	AVDictionary* options = NULL;
	av_dict_set(&options, "framerate", frameRate, 0);
	av_dict_set(&options, "video_size", video_size, 0);

	fmt_ctx = avformat_alloc_context();
	AVCodec *dec;
	int ret;

	AVInputFormat *ifmt = av_find_input_format("dshow");
	if (avformat_open_input(&fmt_ctx, deviceName, ifmt, &options) != 0) {
		printf("Couldn't open input stream.\n");
		return -1;
	}

	if ((ret = avformat_find_stream_info(fmt_ctx, NULL)) < 0) {
		printf("Cannot find stream information\n");
		return ret;
	}

	/* select the video stream */
	ret = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0);
	if (ret < 0) {
		printf("Cannot find a video stream in the input file\n");
	}
	else {
		video_stream_index = ret;
		pVideoCodecCtx = fmt_ctx->streams[video_stream_index]->codec;

		/* init the video decoder */
		if ((ret = avcodec_open2(pVideoCodecCtx, dec, NULL)) < 0) {
			printf("Cannot open video decoder\n");
			return ret;
		}
		hasVideo = 1;
	}

	/* select the audio stream */
	ret = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, &dec, 0);
	if (ret < 0) {
		printf("Cannot find a audio stream in the input file\n");
	}
	else {
		pAudioCodecCtx = fmt_ctx->streams[ret]->codec;
		inAudioIndex = ret;

		/* init the audio decoder */
		if ((ret = avcodec_open2(pAudioCodecCtx, dec, NULL)) < 0) {
			printf("Cannot open audio decoder\n");
			return ret;
		}
		hasAudio = 1;
	}

	return ret;
}

int main() {

	initFFmpeg();

	//setFPS(25);
	//setBitrate(600 * 1024);

	//int ret = initGDIGrab("desktop", 25);
	//if (ret < 0) {
	//	printf("initGDIGrab error.\r\n");
	//}
	////ret = initOutput("output.mp4", 0);
	//ret = initOutput("rtmp://10.200.10.192:1936/publish/1cce619239164103aff94df0594e178c", 1);
	//if (ret < 0) {
	//	printf("initOutput error.\r\n");
	//}
	//pushToURL();

	//Sleep(100000);
	//release();

	struct DShowDevice *devices = listDshowDevice();

	char dshowName[200] = { 0 };
	sprintf(dshowName, "video=%s:audio=%s", devices->deviceNames[0], devices->deviceNames[1]);
	setResolution(640, 480);
	setFPS(25);
	setBitrate(500000);

	initDshow(dshowName);

	initOutput("rtmp://10.200.10.192:1936/publish/1cce619239164103aff94df0594e178c", 1);
	//initOutput("output.mp4", 0);
	pushToURL();

	Sleep(10000);
	release();
}
