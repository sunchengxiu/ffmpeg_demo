#define _CRT_SECURE_NO_WARNINGS

extern "C"
{
#include "libavcodec\avcodec.h"
#include "libavformat\avformat.h"
#include "libswscale\swscale.h"
};

#define VIDEO_AVIO_SIZE 6220800//176400//32768*8//1920*1080*3
#define AUDIO_AVIO_SIZE 16384//5120//512 * 10//4096*2*2

//video parameter
AVFormatContext* pFormatCtx_v;
AVStream* video_st;
AVCodecContext* pCodecCtx_v;
AVCodec* pCodec_v;
AVPacket pkt_v;
uint8_t *picture_buf_RGB, *picture_buf_YUV;
AVFrame *pFrameRGB, *pFrameYUV;
SwsContext* scxt;
int picture_size_RGB, picture_size_YUV;
int y_size;
int in_w = 1280, in_h = 720; //Input data's width and height
FILE *in_file_v = fopen("vid0_1280x720.rgb", "rb"); //Input raw RGB data

//audio parameter
AVFormatContext* pFormatCtx_a;
AVStream* audio_st;
AVCodecContext* pCodecCtx_a;
AVCodec* pCodec_a;
AVPacket pkt_a;
uint8_t* frame_buf_a;
AVFrame* frame_a;
int frame_size_a;
FILE *in_file_a = fopen("aud0.pcm", "rb");	//音频PCM采样数据 

//mux parameter
AVOutputFormat *ofmt = NULL;
//输入对应一个AVFormatContext，输出对应一个AVFormatContext
AVFormatContext *ifmt_ctx_v = NULL, *ifmt_ctx_a = NULL, *ofmt_ctx = NULL;
AVPacket pkt2;
int ret2, k;
int videoindex_v = -1, videoindex_out = -1;
int audioindex_a = -1, audioindex_out = -1;
int frame_index = 0;
int64_t cur_pts_v = 0, cur_pts_a = 0;
unsigned char* inbuffer_v = NULL;
AVIOContext *avio_in_v = NULL;
unsigned char* inbuffer_a = NULL;
AVIOContext *avio_in_a = NULL;
AVBitStreamFilterContext* aacbsfc;

int FirstFlg_video = 0;
int FirstFlg_audio = 0;
int InitFlg = 0;
//累计h264帧size，当AVIO的buffer满时调用mux
int videoencodesize = 0;
int audioencodesize = 0;
//mux帧数
int muxcount = 0;
char videoBuf[VIDEO_AVIO_SIZE];
unsigned int frmsize_v = 0;
char audioBuf[AUDIO_AVIO_SIZE];
unsigned int frmsize_a = 0;

int read_video_buffer(void *opaque, uint8_t *buf, int buf_size)
{
	int lsize = 0;
	if (videoencodesize != 0)
	{
		//printf("read video data: %d\n", readcnt++);
		memcpy(buf, videoBuf, videoencodesize);
		lsize = videoencodesize;
		//fwrite(videoBuf, 1, videoencodesize, out_video);
		videoencodesize = 0;
	}
	return lsize;
}

int read_audio_buffer(void *opaque, uint8_t *buf, int buf_size)
{
	int lsize = 0;
	if (audioencodesize != 0)
	{
		//printf("read audio data: %d\n", readcnt++);
		memcpy(buf, audioBuf, audioencodesize);
		lsize = audioencodesize;
		audioencodesize = 0;
	}
	return lsize;
}

int ffmpeg_muxer_video_init()
{
	//video in
	inbuffer_v = (unsigned char*)av_malloc(VIDEO_AVIO_SIZE);
	avio_in_v = avio_alloc_context(inbuffer_v, VIDEO_AVIO_SIZE, 0, 0,
		read_video_buffer, NULL, NULL);

	if (avio_in_v == NULL)
		goto end;

	ifmt_ctx_v = avformat_alloc_context();
	ifmt_ctx_v->pb = avio_in_v;
	ifmt_ctx_v->flags = AVFMT_FLAG_CUSTOM_IO;

	if ((ret2 = avformat_open_input(&ifmt_ctx_v, "NOTHING", 0, 0)) < 0) {
		printf("Could not open input file1.");
		goto end;
	}
	if ((ret2 = avformat_find_stream_info(ifmt_ctx_v, 0)) < 0) {
		printf("Failed to retrieve input stream information");
		goto end;
	}

	//视频流
	for (k = 0; k < ifmt_ctx_v->nb_streams; k++) {
		//根据输入流创建输出流
		if (ifmt_ctx_v->streams[k]->codec->codec_type == AVMEDIA_TYPE_VIDEO){
			AVStream *in_stream = ifmt_ctx_v->streams[k];
			AVStream *out_stream = avformat_new_stream(ofmt_ctx, in_stream->codec->codec);
			videoindex_v = k;
			if (!out_stream) {
				printf("Failed allocating output stream\n");
				ret2 = AVERROR_UNKNOWN;
				goto end;
			}
			videoindex_out = out_stream->index;
			//复制AVCodecContext的设置
			if (avcodec_copy_context(out_stream->codec, in_stream->codec) < 0) {
				printf("Failed to copy context from input to output stream codec context\n");
				goto end;
			}
			out_stream->codec->codec_tag = 0;
			if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
				out_stream->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;
			break;
		}
	}

end:
	if (ret2 < 0 && ret2 != AVERROR_EOF) {
		printf("Error occurred.\n");
		return -1;
	}

	return 0;
}

int ffmpeg_muxer_audio_init()
{
	//audio in
	inbuffer_a = (unsigned char*)av_malloc(AUDIO_AVIO_SIZE);
	avio_in_a = avio_alloc_context(inbuffer_a, AUDIO_AVIO_SIZE, 0, 0,
		read_audio_buffer, NULL, NULL);

	if (avio_in_a == NULL)
		goto end;

	ifmt_ctx_a = avformat_alloc_context();
	ifmt_ctx_a->pb = avio_in_a;
	ifmt_ctx_a->flags = AVFMT_FLAG_CUSTOM_IO;

	if ((ret2 = avformat_open_input(&ifmt_ctx_a, "NOTHING", 0, 0)) < 0) {
		printf("Could not open input file.");
		goto end;
	}
	if ((ret2 = avformat_find_stream_info(ifmt_ctx_a, 0)) < 0) {
		printf("Failed to retrieve input stream information");
		goto end;
	}

	//音频流
	for (k = 0; k < ifmt_ctx_a->nb_streams; k++) {
		//根据输入流创建输出流
		if (ifmt_ctx_a->streams[k]->codec->codec_type == AVMEDIA_TYPE_AUDIO){
			AVStream *in_stream = ifmt_ctx_a->streams[k];
			AVStream *out_stream = avformat_new_stream(ofmt_ctx, in_stream->codec->codec);
			audioindex_a = k;
			if (!out_stream) {
				printf("Failed allocating output stream\n");
				ret2 = AVERROR_UNKNOWN;
				goto end;
			}
			audioindex_out = out_stream->index;
			//复制AVCodecContext的设置
			if (avcodec_copy_context(out_stream->codec, in_stream->codec) < 0) {
				printf("Failed to copy context from input to output stream codec context\n");
				goto end;
			}
			out_stream->codec->codec_tag = 0;
			if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
				out_stream->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;

			break;
		}
	}

end:
	if (ret2 < 0 && ret2 != AVERROR_EOF) {
		printf("Error occurred.\n");
		return -1;
	}

	return 0;
}

int ffmpeg_muxer_init()
{
	char *out_filename = "myfile.mp4";//输出文件名

	av_register_all();

	//输出（Output）
	avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, out_filename);
	if (!ofmt_ctx) {
		printf("Could not create output context\n");
		ret2 = AVERROR_UNKNOWN;
		goto end;
	}
	ofmt = ofmt_ctx->oformat;

	//视频流
	ffmpeg_muxer_video_init();
	//音频流
	ffmpeg_muxer_audio_init();

	//打开输出文件
	if (!(ofmt->flags & AVFMT_NOFILE)) {
		if (avio_open(&ofmt_ctx->pb, out_filename, AVIO_FLAG_WRITE) < 0) {
			printf("Could not open output file '%s'", out_filename);
			goto end;
		}
	}
	//写文件头
	if (avformat_write_header(ofmt_ctx, NULL) < 0) {
		printf("Error occurred when opening output file\n");
		goto end;
	}

	aacbsfc = av_bitstream_filter_init("aac_adtstoasc");

end:
	if (ret2 < 0 && ret2 != AVERROR_EOF) {
		printf("Error occurred.\n");
		return -1;
	}

	return 0;
}

int ffmpeg_muxer(int enccnt)
{
	if (InitFlg == 1){
		ffmpeg_muxer_init();
		InitFlg = 0;
	}

	while (1) {
		AVFormatContext *ifmt_ctx;
		int stream_index = 0;
		AVStream *in_stream, *out_stream;
		int cmpflg = 0;

		if ((ifmt_ctx_v != NULL) && (ifmt_ctx_a != NULL))
			cmpflg = av_compare_ts(cur_pts_v, ifmt_ctx_v->streams[videoindex_v]->time_base, cur_pts_a, ifmt_ctx_a->streams[audioindex_a]->time_base);
		if ((ifmt_ctx_v == NULL) && (ifmt_ctx_a != NULL))
			cmpflg = 1;
		if ((ifmt_ctx_v != NULL) && (ifmt_ctx_a == NULL))
			cmpflg = 0;

		//获取一个AVPacket
		if (cmpflg <= 0){
			ifmt_ctx = ifmt_ctx_v;
			stream_index = videoindex_out;

			if (av_read_frame(ifmt_ctx, &pkt2) >= 0){
				do{
					in_stream = ifmt_ctx->streams[pkt2.stream_index];
					out_stream = ofmt_ctx->streams[stream_index];

					if (pkt2.stream_index == videoindex_v){
						//FIX：No PTS (Example: Raw H.264)
						//Simple Write PTS
						if (pkt2.pts == AV_NOPTS_VALUE){
							//Write PTS
							AVRational time_base1 = in_stream->time_base;
							//Duration between 2 frames (us)
							int64_t calc_duration = (double)AV_TIME_BASE / av_q2d(in_stream->r_frame_rate);
							//Parameters
							pkt2.pts = (double)(frame_index*calc_duration) / (double)(av_q2d(time_base1)*AV_TIME_BASE);
							pkt2.dts = pkt2.pts;
							pkt2.duration = (double)calc_duration / (double)(av_q2d(time_base1)*AV_TIME_BASE);
							frame_index++;
						}

						cur_pts_v = pkt2.pts;
						break;
					}
				} while (av_read_frame(ifmt_ctx, &pkt2) >= 0);
			}
			else{
				break;
			}
		}
		else{
			ifmt_ctx = ifmt_ctx_a;
			stream_index = audioindex_out;
			if (av_read_frame(ifmt_ctx, &pkt2) >= 0){
				do{
					in_stream = ifmt_ctx->streams[pkt2.stream_index];
					out_stream = ofmt_ctx->streams[stream_index];

					if (pkt2.stream_index == audioindex_a){

						//FIX：No PTS
						//Simple Write PTS
						if (pkt2.pts == AV_NOPTS_VALUE){
							//Write PTS
							AVRational time_base1 = in_stream->time_base;
							//Duration between 2 frames (us)
							int64_t calc_duration = (double)AV_TIME_BASE / av_q2d(in_stream->r_frame_rate);
							//Parameters
							pkt2.pts = (double)(frame_index*calc_duration) / (double)(av_q2d(time_base1)*AV_TIME_BASE);
							pkt2.dts = pkt2.pts;
							pkt2.duration = (double)calc_duration / (double)(av_q2d(time_base1)*AV_TIME_BASE);
							frame_index++;
						}
						cur_pts_a = pkt2.pts;

						break;
					}
				} while (av_read_frame(ifmt_ctx, &pkt2) >= 0);
			}
			else{
				break;
			}

		}

		av_bitstream_filter_filter(aacbsfc, out_stream->codec, NULL, &pkt2.data, &pkt2.size, pkt2.data, pkt2.size, 0);

		/* copy packet */
		//转换PTS/DTS（Convert PTS/DTS）
		pkt2.pts = av_rescale_q_rnd(pkt2.pts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
		pkt2.dts = av_rescale_q_rnd(pkt2.dts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
		pkt2.duration = av_rescale_q(pkt2.duration, in_stream->time_base, out_stream->time_base);
		pkt2.pos = -1;
		pkt2.stream_index = stream_index;

		printf("Write 1 Packet. size:%5d\tpts:%8d\n", pkt2.size, pkt2.pts);
		//写入（Write）
		if (av_interleaved_write_frame(ofmt_ctx, &pkt2) < 0) {
			printf("Error muxing packet\n");
			break;
		}
		av_free_packet(&pkt2);
	}

end:
	if (ret2 < 0 && ret2 != AVERROR_EOF) {
		printf("Error occurred.\n");
		return -1;
	}

	return 0;

}

int ffmpeg_video_encode_init()
{
	AVOutputFormat* fmt;

	const char* out_file = "record.h264"; //Output Filepath,用于设置AVFormatContext

	//初始化并注册视频文件格式与编解码库
	av_register_all();
	//初始化AVFormatContext,Allocate an AVFormatContext. 
	pFormatCtx_v = avformat_alloc_context();
	fmt = av_guess_format(NULL, out_file, NULL);
	pFormatCtx_v->oformat = fmt;

	//AVStream的初始化函数
	video_st = avformat_new_stream(pFormatCtx_v, 0);
	video_st->time_base.num = 1;
	video_st->time_base.den = 25;

	if (video_st == NULL){
		return -1;
	}
	//Param that must set
	pCodecCtx_v = video_st->codec;
	pCodecCtx_v->codec_id = fmt->video_codec;
	pCodecCtx_v->codec_type = AVMEDIA_TYPE_VIDEO;
	pCodecCtx_v->pix_fmt = PIX_FMT_YUV420P;
	pCodecCtx_v->width = in_w;
	pCodecCtx_v->height = in_h;
	pCodecCtx_v->time_base.num = 1;
	pCodecCtx_v->time_base.den = 25;  //10&25
	pCodecCtx_v->bit_rate = 4334000;// 400000;
	pCodecCtx_v->gop_size = 10;    //250 //关键帧的最大间隔帧数/
	//H264
	//pCodecCtx->me_range = 16;
	//pCodecCtx->max_qdiff = 4;
	//pCodecCtx->qcompress = 0.6;
	pCodecCtx_v->qmin = 10;
	pCodecCtx_v->qmax = 51;

	//Optional Param
	pCodecCtx_v->max_b_frames = 1; //3

	// Set Option
	AVDictionary *param = 0;
	//H.264
	if (pCodecCtx_v->codec_id == AV_CODEC_ID_H264) {
		av_dict_set(&param, "preset", "superfast", 0);
		av_dict_set(&param, "tune", "zerolatency", 0);
	}

	//Show some Information
	av_dump_format(pFormatCtx_v, 0, out_file, 1);

	pCodec_v = avcodec_find_encoder(pCodecCtx_v->codec_id);
	if (!pCodec_v){
		printf("Can not find video encoder! \n");
		return -1;
	}
	if (avcodec_open2(pCodecCtx_v, pCodec_v, &param) < 0){
		printf("Failed to open video encoder! \n");
		return -1;
	}

	pFrameRGB = av_frame_alloc();
	pFrameYUV = av_frame_alloc();

	picture_size_RGB = avpicture_get_size(PIX_FMT_RGB24, pCodecCtx_v->width, pCodecCtx_v->height);
	picture_size_YUV = avpicture_get_size(PIX_FMT_YUV420P, pCodecCtx_v->width, pCodecCtx_v->height);
	picture_buf_RGB = (uint8_t *)av_malloc(picture_size_RGB);
	picture_buf_YUV = (uint8_t *)av_malloc(picture_size_YUV);
	avpicture_fill((AVPicture *)pFrameRGB, picture_buf_RGB, PIX_FMT_RGB24, pCodecCtx_v->width, pCodecCtx_v->height);
	avpicture_fill((AVPicture *)pFrameYUV, picture_buf_YUV, PIX_FMT_YUV420P, pCodecCtx_v->width, pCodecCtx_v->height);

	scxt = sws_getContext(pCodecCtx_v->width, pCodecCtx_v->height, PIX_FMT_RGB24, pCodecCtx_v->width, pCodecCtx_v->height,
		PIX_FMT_YUV420P, SWS_POINT, NULL, NULL, NULL);

	av_new_packet(&pkt_v, picture_size_YUV);
	y_size = pCodecCtx_v->width * pCodecCtx_v->height;

	return 0;
}

int ffmpeg_video_encode(int i)
{
	int got_picture = 0;
	//Read raw YUV data
	//YUV420 数据在内存中的长度是 width * hight * 3 / 2
	if (fread(picture_buf_RGB, 1, y_size * 3, in_file_v) != y_size * 3){
		printf("video goto end！\n");
		fseek(in_file_v, 0, SEEK_SET);
		fread(picture_buf_RGB, 1, y_size * 3, in_file_v);
	}
	
	sws_scale(scxt, pFrameRGB->data, pFrameRGB->linesize, 0, pCodecCtx_v->height, pFrameYUV->data, pFrameYUV->linesize);

	pFrameYUV->data[0] = picture_buf_YUV;              // Y
	pFrameYUV->data[1] = picture_buf_YUV + y_size;      // U 
	pFrameYUV->data[2] = picture_buf_YUV + y_size * 5 / 4;  // V
	//PTS
	//pFrameYUV->pkt_pts = i;
	pFrameYUV->pkt_pts = i * (video_st->time_base.den) / ((video_st->time_base.num) * 25);

	av_init_packet(&pkt_v);
	//Encode
	int ret = avcodec_encode_video2(pCodecCtx_v, &pkt_v, pFrameYUV, &got_picture);
	if (ret < 0){
		printf("Failed to video encode! \n");
		return -1;
	}
	if (got_picture == 1){
		if ((videoencodesize + pkt_v.size) > VIDEO_AVIO_SIZE)
		{
			printf("video avio buffer is full: %d, to mux!\n", videoencodesize);
			ffmpeg_muxer(i);
			//fwrite(videoBuf, 1, videoencodesize, out_encode);
			videoencodesize = 0;
		}
		memcpy(videoBuf + videoencodesize, pkt_v.data, pkt_v.size);
		frmsize_v = pkt_v.size;
		videoencodesize += frmsize_v;
		printf("Succeed to encode video frame: %5d\tsize:%5d\n", i, pkt_v.size);
		av_free_packet(&pkt_v);
	}
}

void ffmpeg_video_encode_close()
{
	//video Clean
	if (video_st){
		avcodec_close(video_st->codec);
		av_free(pFrameRGB);
		av_free(pFrameYUV);
		av_free(picture_buf_RGB);
		av_free(picture_buf_YUV);
	}
	avio_close(pFormatCtx_v->pb);
	avformat_free_context(pFormatCtx_v);

	fclose(in_file_v);
}

int ffmpeg_audio_encode_init()
{
	AVOutputFormat* fmt;

	const char* out_file = "record_audio.aac";	//输出文件,用于设置AVFormatContext

	av_register_all();

	avformat_alloc_output_context2(&pFormatCtx_a, NULL, NULL, out_file);
	fmt = pFormatCtx_a->oformat;

	audio_st = avformat_new_stream(pFormatCtx_a, 0);
	if (audio_st == NULL){
		return -1;
	}
	pCodecCtx_a = audio_st->codec;
	pCodecCtx_a->codec_id = fmt->audio_codec;
	pCodecCtx_a->codec_type = AVMEDIA_TYPE_AUDIO;
	pCodecCtx_a->sample_fmt = AV_SAMPLE_FMT_S16;
	pCodecCtx_a->sample_rate = 48000;  //44100---11025(8000)
	pCodecCtx_a->channel_layout = AV_CH_LAYOUT_STEREO;
	pCodecCtx_a->channels = av_get_channel_layout_nb_channels(pCodecCtx_a->channel_layout);
	pCodecCtx_a->bit_rate = 1536000;    //64000

	//输出格式信息
	av_dump_format(pFormatCtx_a, 0, out_file, 1);

	pCodec_a = avcodec_find_encoder(pCodecCtx_a->codec_id);
	if (!pCodec_a)
	{
		printf("audio 没有找到合适的编码器！\n");
		return -1;
	}
	if (avcodec_open2(pCodecCtx_a, pCodec_a, NULL) < 0)
	{
		printf("audio 编码器打开失败！\n");
		return -1;
	}
	frame_a = av_frame_alloc();
	frame_a->nb_samples = pCodecCtx_a->frame_size;
	frame_a->format = pCodecCtx_a->sample_fmt;

	frame_size_a = av_samples_get_buffer_size(NULL, pCodecCtx_a->channels, pCodecCtx_a->frame_size, pCodecCtx_a->sample_fmt, 1);
	frame_buf_a = (uint8_t *)av_malloc(frame_size_a);
	avcodec_fill_audio_frame(frame_a, pCodecCtx_a->channels, pCodecCtx_a->sample_fmt, (const uint8_t*)frame_buf_a, frame_size_a, 1);
	av_new_packet(&pkt_a, frame_size_a);

	return 0;
}

int ffmpeg_audio_encode(int i)
{
	//读入PCM
	if (fread(frame_buf_a, 1, frame_size_a, in_file_a) != frame_size_a)
	{
		printf("audio goto end！\n");
		fseek(in_file_a, 0, SEEK_SET);
		fread(frame_buf_a, 1, frame_size_a, in_file_a);
	}

	frame_a->data[0] = frame_buf_a;  //采样信号
	frame_a->pts = i;//i*100
	int got_frame = 0;
	//编码
	int ret = avcodec_encode_audio2(pCodecCtx_a, &pkt_a, frame_a, &got_frame);
	if (ret < 0)
	{
		printf("audio 编码错误！\n");
		return -1;
	}
	if (got_frame == 1)
	{
		if ((audioencodesize + pkt_a.size) > AUDIO_AVIO_SIZE)
		{
			printf("audio avio buffer is full: %d, to mux!\n", audioencodesize);
			ffmpeg_muxer(i);
			audioencodesize = 0;
		}
		memcpy(audioBuf + audioencodesize, pkt_a.data, pkt_a.size);
		frmsize_a = pkt_a.size;
		audioencodesize += frmsize_a;

		printf("audio 编码成功第%d帧！\n", i);
		av_free_packet(&pkt_a);
	}

	return 0;
}

void ffmpeg_audio_encode_close()
{
	//audio清理
	if (audio_st)
	{
		avcodec_close(audio_st->codec);
		av_free(frame_a);
		av_free(frame_buf_a);
	}
	avio_close(pFormatCtx_a->pb);
	avformat_free_context(pFormatCtx_a);

	fclose(in_file_a);
}

int flush_video_encoder(AVFormatContext *fmt_ctx, unsigned int stream_index){
	int ret;
	int got_frame;
	AVPacket enc_pkt;
	if (!(fmt_ctx->streams[stream_index]->codec->codec->capabilities &
		CODEC_CAP_DELAY))
		return 0;
	while (1) {
		enc_pkt.data = NULL;
		enc_pkt.size = 0;
		av_init_packet(&enc_pkt);
		ret = avcodec_encode_video2(fmt_ctx->streams[stream_index]->codec, &enc_pkt,
			NULL, &got_frame);
		av_frame_free(NULL);
		if (ret < 0)
			break;
		if (!got_frame){
			ret = 0;
			break;
		}
		printf("Flush Encoder: Succeed to encode 1 frame!\tsize:%5d\n", enc_pkt.size);
		/* mux encoded frame */
		ret = av_write_frame(fmt_ctx, &enc_pkt);
		printf("Succeed to encode pkt.pts: %5d\tpkt.dts:%5d\n", enc_pkt.pts, enc_pkt.dts);
		if (ret < 0)
			break;
	}
	return ret;
}

int main(int argc, char* argv[])
{
	int framenum = 1000;	//帧数
	int i = 0;
	
	//mux初始化flag
	InitFlg = 1;
	//video编码初始化函数
	ffmpeg_video_encode_init();
	//audio编码初始化函数
	ffmpeg_audio_encode_init();

	for (i = 0; i<framenum; i++){
		ffmpeg_video_encode(i);
		ffmpeg_audio_encode(i);
	}

	//写MP4文件尾（Write file trailer）
	av_write_trailer(ofmt_ctx);
	av_bitstream_filter_close(aacbsfc);
	ffmpeg_video_encode_close();
	ffmpeg_audio_encode_close();

	return 0;
}
