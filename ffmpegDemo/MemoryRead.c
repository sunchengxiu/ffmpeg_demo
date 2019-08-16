
/*=============================================================================   
#     FileName: ffmpeg_mem_read.c   
#         Desc: an example of ffmpeg read from memory
#       Author: licaibiao   
#   LastChange: 2017-03-21    
=============================================================================*/  
#include <stdio.h>
 
#define __STDC_CONSTANT_MACROS
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libswscale/swscale.h>
 
//#define QUARTER_SHOW   
 
// char *input_name = "cuc60anniversary_start.ts";
 //https://blog.csdn.net/li_wen01/article/details/64904586
FILE *fp_open = NULL;
 
int read_buffer(void *opaque, uint8_t *buf, int buf_size){
	if(!feof(fp_open)){
		int true_size=fread(buf,1,buf_size,fp_open);
		return true_size;
	}else{
		return -1;
	}
}
 
int main(int argc, char* argv[])
{
	AVFormatContext	*pFormatCtx;
    AVCodecContext	*pCodecCtx;
    AVCodec			*pCodec;
    AVIOContext     *avio;
    AVFrame	        *pFrame;
    AVFrame         *pFrameYUV;
    AVPacket        *packet;
    
    struct SwsContext *img_convert_ctx;
 
    int             videoindex;
	int				i;
    int             ret;
    int             got_picture;
    int             y_size;
    unsigned char   *aviobuffer;
    FILE            *fp_yuv;
 const char *input_name;
 input_name = argv[1];
	// fp_open = fopen(argv[1],"rb+");
	fp_open = fopen(input_name, "rb+");
    fp_yuv  = fopen("output.yuv", "wb+");     
 
	av_register_all();
	pFormatCtx = avformat_alloc_context();
 
	aviobuffer=(unsigned char *)av_malloc(32768);
	avio = avio_alloc_context(aviobuffer, 32768,0,NULL,read_buffer,NULL,NULL);
 
    /* Open an input stream and read the header. */
    pFormatCtx->pb = avio;
	if(avformat_open_input(&pFormatCtx,NULL,NULL,NULL)!=0){
		printf("Couldn't open input stream.\n");
		return -1;
	}
 
    /* Read packets of a media file to get stream information. */
	if(avformat_find_stream_info(pFormatCtx,NULL)<0){
		printf("Couldn't find stream information.\n");
		return -1;
	}
    
	videoindex = -1;
	for(i=0; i<pFormatCtx->nb_streams; i++) {
		if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO){
			videoindex=i;
			break;
		}
    } 
    
	if(videoindex==-1){
		printf("Didn't find a video stream.\n");
		return -1;
	}
 
	av_dump_format(pFormatCtx, 0, input_name, 0);
 
    /* Find a registered decoder with a matching codec ID */
	pCodecCtx = pFormatCtx->streams[videoindex]->codec;
	pCodec    = avcodec_find_decoder(pCodecCtx->codec_id);   
	if(pCodec==NULL){
		printf("Codec not found.\n");
		return -1;
	}
 
    /* Initialize the AVCodecContext to use the given AVCodec */
	if(avcodec_open2(pCodecCtx, pCodec,NULL)<0){
		printf("Could not open codec.\n");
		return -1;
	}
	
	pFrame    = av_frame_alloc();
	pFrameYUV = av_frame_alloc();
	uint8_t *out_buffer=(uint8_t *)av_malloc(avpicture_get_size(AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height));
	avpicture_fill((AVPicture *)pFrameYUV, out_buffer, AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height);
 
    /*  Allocate and return an SwsContext. */
    /* srcW：源图像的宽
     * srcH：源图像的高
     * srcFormat：源图像的像素格式
     * dstW：目标图像的宽
     * dstH：目标图像的高
     * dstFormat：目标图像的像素格式
     * flags：设定图像拉伸使用的算法
    */
#ifndef QUARTER_SHOW
	img_convert_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL); 
#else
    img_convert_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt, pCodecCtx->width/2, pCodecCtx->height/2, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL); 
    printf("out frame  width = %d, height = %d \n", pCodecCtx->width/2,pCodecCtx->height/2);
#endif
	packet = (AVPacket *)av_malloc(sizeof(AVPacket));
	while(av_read_frame(pFormatCtx, packet) >= 0){
		if(packet->stream_index == videoindex){
			ret = avcodec_decode_video2(pCodecCtx, pFrame, &got_picture, packet);
			if(ret < 0){
				printf("Decode Error.\n");
				return -1;
			}
			if(got_picture){
                /* Scale the image slice in srcSlice and put the resulting scaled slice in the image in dst. 图像处理函数 */
                /* c             the scaling context previously created with  sws_getContext()
                 * srcSlice      the array containing the pointers to the planes of the source slice
                 * srcStride     the array containing the strides for each plane of
                 * srcSliceY     the position in the source image of the slice to process, that is the number (counted starting from
                                 zero) in the image of the first row of the slice 输入图像数据的第多少列开始逐行扫描，通常设为0
                 * srcSliceH     the height of the source slice, that is the number of rows in the slice 为需要扫描多少行，通常为输入图像数据的高度
                 * dst           the array containing the pointers to the planes of the destination image
                 * dstStride     the array containing the strides for each plane of the destination image
                 */  
                sws_scale(img_convert_ctx, (const uint8_t* const*)pFrame->data, pFrame->linesize, 0, pCodecCtx->height, pFrameYUV->data, pFrameYUV->linesize);
               
#ifndef QUARTER_SHOW
               // printf("pFrameYUV->height   = %d\n ",pFrameYUV->height);
               // printf("pFrameYUV->width    = %d\n ", pFrameYUV->width);
               // printf("pFrameYUV->pkt_size = %d\n ",pFrameYUV->pkt_size);
 
                y_size = pCodecCtx->width*pCodecCtx->height; 
				fwrite(pFrameYUV->data[0],1,y_size,fp_yuv);    //Y 
				fwrite(pFrameYUV->data[1],1,y_size/4,fp_yuv);  //U
				fwrite(pFrameYUV->data[2],1,y_size/4,fp_yuv);  //V
#else
                for(i=0; i<pCodecCtx->height/2; i++){
                    fwrite(pFrameYUV->data[0]+pCodecCtx->width * i ,1,pCodecCtx->width/2,fp_yuv); 
                }
                for(i=0; i<pCodecCtx->height/2; i = i + 2){
                    fwrite(pFrameYUV->data[1]+pCodecCtx->width * i/4 ,1,pCodecCtx->width/4,fp_yuv);
                }
 
                for(i=0; i<pCodecCtx->height/2; i = i + 2 ){             
                    fwrite(pFrameYUV->data[2]+pCodecCtx->width * i/4 ,1,pCodecCtx->width/4,fp_yuv); 
                }    
#endif             
                    
			}
		}
		av_free_packet(packet);
	}
	sws_freeContext(img_convert_ctx);
 
    fclose(fp_yuv);
	fclose(fp_open);
	av_free(out_buffer);
	av_free(pFrameYUV);
	avcodec_close(pCodecCtx);
	avformat_close_input(&pFormatCtx);
 
	return 0;
}
