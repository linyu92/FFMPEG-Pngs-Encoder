//
//  main.c
//  FFMPEG-C
//
//  Created by linyu on 11/10/16.
//  Copyright © 2016 linyu. All rights reserved.
//

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <string.h>

#define kPNGDirName_In "/Users/linyu/Desktop/FFMPEG/FFMPEG-C/FFMPEG-C/pngs/礼物合集/天使礼物－序列帧/0000%02d.png"

#define kH264FileName_Out "/Users/linyu/Desktop/FFMPEG/ffmpeg-mac-1/ffmpeg-mac-1/encode/source/test_1.h264"

//为了提高编码和解码效率注意16字节对齐
#define kBitmap_SizeWith 512
#define kBitmap_SizeHeight 512

#define kCodecCtx_qmin 10
#define kCodecCtx_qmax 30
#define kCodecCtx_gop 12

#define kFramesPerSecond 15

static void Help(void) {
    printf("#############################\n");
    printf("用于将PNG序列转为H264视频 Usage:\n");
    printf(" PNG-Encoder [Options] [dir]\n");
    printf(" dir  ............ png文件夹，只有后缀为.png的文件参与编码\n");
    printf(" Options:\n");
    printf("  -help  ............ this help\n");
    printf("  -w ................. 图片宽度 默认512\n");
    printf("  -h ................. 图片高度 默认512\n");
    printf("  -qmin .............. 最小的量化因子[1-51] 默认5\n");
    printf("  -qmax .............. 最大的量化因子[1-51] 默认25\n");
    printf("  -gop .............. 图片组 默认12\n");
    printf("  -fps .............. 帧率 默认15\n");
    printf("\n");
}

uint32_t ExUtilGetUInt(const char* const v, int base, int* const error) {
    char* end = NULL;
    const uint32_t n = (v != NULL) ? (uint32_t)strtoul(v, &end, base) : 0u;
    if (end == v && error != NULL && !*error) {
        *error = 1;
        fprintf(stderr, "Error! '%s' is not an integer.\n",
                (v != NULL) ? v : "(null)");
    }
    return n;
}

int decode(AVCodecContext *avctx, AVFrame *frame, int *got_frame, AVPacket *pkt);
int getPacket(AVPacket *packet,char *filepath);

int main(int argc, char* argv[])
{
    AVCodec *pCodec;
    AVCodecContext *pCodecCtx= NULL;
    int ret;
    FILE *fp_out;
    AVFrame *pFrame;
    AVPacket pkt;
    int framecnt=0;
    
    enum AVCodecID codec_id=AV_CODEC_ID_H264;
    
    DIR* pngFileDir = NULL;
    char *filedir_in = NULL;
    char filename_out[1024];
    int out_w = kBitmap_SizeWith;
    int out_h = kBitmap_SizeHeight;
    int out_qmin = kCodecCtx_qmin;
    int out_qmax = kCodecCtx_qmax;
    int out_gop = kCodecCtx_gop;
    int frame_per_sec = kFramesPerSecond;
    
    
    int c = 1;
    for (c = 1; c < argc; ++c) {
        int parse_error = 0;
        if (!strcmp(argv[c], "-h") || !strcmp(argv[c], "-help")) {
            Help();
            return 0;
        } else if (!strcmp(argv[c], "-width") && c < argc - 1) {
            out_w = ExUtilGetUInt(argv[++c], 0, &parse_error);
        } else if (!strcmp(argv[c], "-height") && c < argc - 1) {
            out_h = ExUtilGetUInt(argv[++c], 0, &parse_error);
        } else if (!strcmp(argv[c], "-qmin") && c < argc - 1) {
            out_qmin = ExUtilGetUInt(argv[++c], 0, &parse_error);
        } else if (!strcmp(argv[c], "-qmax") && c < argc - 1) {
            out_qmax = ExUtilGetUInt(argv[++c], 0, &parse_error);
        } else if (!strcmp(argv[c], "-fps") && c < argc - 1) {
            frame_per_sec = ExUtilGetUInt(argv[++c], 0, &parse_error);
        } else {
            filedir_in = argv[c];
            strcpy(filename_out, filedir_in);
            strcat(filename_out, "/000000.h264");
        }
        
        if (parse_error) {
            printf("Options参数解析失败\n");
            Help();
            return -1;
        }
    }
    
    
    //1 初始化H264编码器
    av_register_all();
    
    pCodec = avcodec_find_encoder(codec_id);
    if (!pCodec) {
        printf("Codec not found\n");
        return -1;
    }
    pCodecCtx = avcodec_alloc_context3(pCodec);
    if (!pCodecCtx) {
        printf("Could not allocate video codec context\n");
        return -1;
    }
    pCodecCtx->bit_rate = 400000;
    pCodecCtx->width = out_w;
    pCodecCtx->height = out_h*2; //为了存储Alpha通道，使用和Y分量同等大小的字节
    pCodecCtx->time_base.num=1;
    pCodecCtx->time_base.den=frame_per_sec;
    pCodecCtx->gop_size = out_gop;
    pCodecCtx->qmin = out_qmin;
    pCodecCtx->qmax = out_qmax;
    pCodecCtx->max_b_frames = 0; // 为了解码方便，此处不放置B帧
    pCodecCtx->pix_fmt = AV_PIX_FMT_NV12; // iOS 唯一支持的硬解码格式
    
    if (codec_id == AV_CODEC_ID_H264)
        av_opt_set(pCodecCtx->priv_data, "preset", "slow", 0);
    
    if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
        printf("Could not open codec\n");
        return -1;
    }

    // 2 初始化PNG解码器
    enum AVCodecID png_codec_id=AV_CODEC_ID_PNG;
    AVCodec *png_pCodec = avcodec_find_decoder(png_codec_id);
    AVCodecContext *png_pCodecCtx= avcodec_alloc_context3(png_pCodec);
    png_pCodecCtx->width = out_w;
    png_pCodecCtx->height = out_h; //这里的size是png的原始size
    if (avcodec_open2(png_pCodecCtx, png_pCodec, NULL) < 0) {
        printf("Could not open codec\n");
        return -1;
    }
    
    // 3 获取将要写入的h264文件描述符
    fp_out = fopen(filename_out, "wb");
    if (!fp_out) {
        printf("Could not open %s\n", filename_out);
        Help();
        return -1;
    }
    
    // 4 初始化 Y(A)UV420P帧结构
    /*
          _______
         |       |
         |   Y   |  8 bit
         |_______|
         |__UV___|  4 bit

         ========>
             _______
            |       |
            |  Y1   |  8 bit
            |_______|
            |       |
            |  Y2   |  8 bit
            |_______|
            |__UV1__|  4 bit
            |__UV2__|  4 bit
     
            ========>
                  _______
                 |       |
                 |   Y   |  8 bit
                 |_______|
                 |       |
                 |   A   |  8 bit
                 |_______|
                 |__UV___|  4 bit
                 |_______|  4 bit
     */
    uint8_t *yPlane, *uvPlane , *aPlane;
    size_t yaPlaneSz, uvPlaneSz;
    yaPlaneSz = pCodecCtx->width*pCodecCtx->height;
    uvPlaneSz = pCodecCtx->width*pCodecCtx->height/2;
    yPlane = av_malloc(yaPlaneSz);
    uvPlane = av_malloc(uvPlaneSz);
    aPlane = yPlane + yaPlaneSz/2; //a分量从Y空间的中间开始写入
    
    pFrame = av_frame_alloc();
    if (!pFrame) {
        printf("Could not allocate video frame\n");
        return -1;
    }
    pFrame->format = pCodecCtx->pix_fmt;
    pFrame->width  = pCodecCtx->width;
    pFrame->height = pCodecCtx->height;
    pFrame->linesize[0] = pCodecCtx->width;
    pFrame->linesize[1] = pCodecCtx->width;
    pFrame->data[0] = yPlane; //Y分量
    pFrame->data[1] = uvPlane; //UV分量
    
    // 最后一个A分量可以利用YUVA420P格式的最后一个数据填充，frameForYUVA仅仅为了A的填充地址指向pframe->aPlane
    uint8_t *t_yPlane, *t_uPlane ,*t_vPlane;
    int t_yPlaneSz = png_pCodecCtx->width * png_pCodecCtx->height;
    int t_uPlaneSz = png_pCodecCtx->width/2 * png_pCodecCtx->height/2;
    int t_vPlaneSz = png_pCodecCtx->width/2 * png_pCodecCtx->height/2;
//    size_t t_aPlaneSz = png_pCodecCtx->width * png_pCodecCtx->height;
    t_yPlane = av_malloc(t_yPlaneSz);
    t_uPlane = av_malloc(t_uPlaneSz);
    t_vPlane = av_malloc(t_vPlaneSz);
    
    AVFrame *frameForYUVA = av_frame_alloc();
    frameForYUVA->format = AV_PIX_FMT_YUVA420P;
    frameForYUVA->width  = png_pCodecCtx->width;
    frameForYUVA->height = png_pCodecCtx->height;
    frameForYUVA->data[0] = t_yPlane;
    frameForYUVA->data[1] = t_uPlane;
    frameForYUVA->data[2] = t_vPlane;
    frameForYUVA->data[3] = aPlane;
    frameForYUVA->linesize[0] = png_pCodecCtx->width;
    frameForYUVA->linesize[1] = png_pCodecCtx->width/2;
    frameForYUVA->linesize[2] = png_pCodecCtx->width/2;
    frameForYUVA->linesize[3] = png_pCodecCtx->width;
    
    // AV_PIX_FMT_PAL8 -> AV_PIX_FMT_NV12
    struct SwsContext *sws_pal_to_nv12_ctx = NULL;
    sws_pal_to_nv12_ctx = sws_getContext(png_pCodecCtx->width,
                             png_pCodecCtx->height,
                             AV_PIX_FMT_PAL8,
                             png_pCodecCtx->width,
                             png_pCodecCtx->height,
                             pCodecCtx->pix_fmt,
                             SWS_BICUBIC,
                             NULL,
                             NULL,
                             NULL);

    // AV_PIX_FMT_PAL8 -> AV_PIX_FMT_YUVA420P
    struct SwsContext *sws_pal_to_yuva_ctx = NULL;
    sws_pal_to_yuva_ctx = sws_getContext(png_pCodecCtx->width,
                             png_pCodecCtx->height,
                             AV_PIX_FMT_PAL8,
                             png_pCodecCtx->width,
                             png_pCodecCtx->height,
                             AV_PIX_FMT_YUVA420P,
                             SWS_BICUBIC,
                             NULL,
                             NULL,
                             NULL);
    
    // 5 开始解码png将其数据填充为我们需要的YANV结构，并用h264编码
    // 注解码png需要另外一张png_pFrame
    AVFrame *png_pFrame = av_frame_alloc();
    AVPacket png_packet;
    av_init_packet(&png_packet);
    png_packet.data = NULL;    // packet data will be allocated by the encoder
    png_packet.size = 0;
    
    pngFileDir = opendir(filedir_in);
    if(pngFileDir == NULL) {
        fprintf(stderr, "Error! open dir %s error!\n", filedir_in);
        return -1;
    }
    
    int i = 1;
    struct dirent* filename = NULL;
    while((filename = readdir(pngFileDir)) != NULL) {
        char *pngname = filename->d_name;
        int prelength = (int)strlen(pngname) - 4;
        if (prelength > 0) {
            char *fn = pngname + prelength;
            if(!strcmp(fn, ".png")) { //检查PNG后缀
                //------------------------ decode & encode begin ------------------------
                char srcpath[1024];
                sprintf(srcpath, "%s/%s", filedir_in, pngname);
                if (getPacket(&png_packet, srcpath)>=0) {
                    int png_got_frame;
                    decode(png_pCodecCtx, png_pFrame, &png_got_frame, &png_packet);
                    
                    sws_scale(sws_pal_to_nv12_ctx, (uint8_t const * const *) png_pFrame->data,
                              png_pFrame->linesize, 0, png_pCodecCtx->height, pFrame->data,
                              pFrame->linesize);
                    
                    sws_scale(sws_pal_to_yuva_ctx, (uint8_t const * const *) png_pFrame->data,
                              png_pFrame->linesize, 0, png_pCodecCtx->height, frameForYUVA->data,
                              frameForYUVA->linesize);
                    
                    pFrame->pts=i++;
                    int got_picture=0;
                    //Encode
                    int ret = avcodec_encode_video2(pCodecCtx, &pkt,pFrame, &got_picture);
                    if(ret < 0){
                        printf("Failed to encode! \n");
                        return -1;
                    }
                    if (got_picture==1){
                        printf("Succeed to encode frame: %5d\tsize:%5d\n",++framecnt,pkt.size);
                        fwrite(pkt.data, 1, pkt.size, fp_out);
                        av_packet_unref(&pkt);
                    }
                }
                //------------------------ decode end ------------------------
            }
        }
    }
    closedir(pngFileDir);
    
    //Flush Encoder 注：编码codeCtx里有剩余的帧
    for (int got_output = 1; got_output;) {
        ret = avcodec_encode_video2(pCodecCtx, &pkt, NULL, &got_output);
        if (ret < 0) {
            printf("Error encoding frame\n");
            return -1;
        }
        if (got_output) {
            printf("Flush: Succeed to encode frame: %5d\tsize:%5d\n",++framecnt,pkt.size);
            fwrite(pkt.data, 1, pkt.size, fp_out);
            av_free_packet(&pkt);
        }
    }
    
    //------------------------ encode end ------------------------
    
    av_free(yPlane);
    av_free(uvPlane);
    av_free(t_yPlane);
    av_free(t_uPlane);
    av_free(t_vPlane);
    
    fclose(fp_out);
    avcodec_close(pCodecCtx);
    av_free(pCodecCtx);
    av_frame_free(&pFrame);
    
    return 0;
}

int decode(AVCodecContext *avctx, AVFrame *frame, int *got_frame, AVPacket *pkt)
{
    int ret;
    
    *got_frame = 0;
    
    if (pkt) {
        ret = avcodec_send_packet(avctx, pkt);
        // In particular, we don't expect AVERROR(EAGAIN), because we read all
        // decoded frames with avcodec_receive_frame() until done.
        if (ret < 0)
            return ret == AVERROR_EOF ? 0 : ret;
    }
    
    ret = avcodec_receive_frame(avctx, frame);
    if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
        return ret;
    if (ret >= 0)
        *got_frame = 1;
    
    return 0;
}

int getPacket(AVPacket *packet,char *filepath)
{
    AVFormatContext *pFormatCtx = NULL;
    if (avformat_open_input(&pFormatCtx, filepath, NULL, NULL) != 0)
        return -1; // Couldn't open file
    
    while (av_read_frame(pFormatCtx, packet) >= 0) {
        avformat_close_input(&pFormatCtx);
        return 0;
    }
    
    return -1;
}


