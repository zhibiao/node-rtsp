#include <napi.h>
#include <chrono>
#include <atomic>
#include <thread>
#include <mutex>

extern "C" {
    #include "libavcodec/avcodec.h"
    #include "libavformat/avformat.h"
    #include "libswscale/swscale.h"
    #include "libavutil/imgutils.h"
}

class Decoder {
public:
    Decoder(){}
    
    int open(const char* url) {
        avformat_network_init();
        format_ctx = avformat_alloc_context();

        AVDictionary* options = NULL;
        av_dict_set(&options, "max_delay", "10000", 0);
        av_dict_set(&options, "buffer_size", "1024000", 0);
        av_dict_set(&options, "rtsp_transport", "tcp", 0);
        av_dict_set(&options, "analyzeduration", "1000000", 0);
        av_dict_set(&options, "stimeout","10000000", 0);
        
        if (avformat_open_input(&format_ctx, url, NULL, &options) != 0) {
           printf("%s failed!\n", "avformat_open_input");
           return -1;
        }
        
        if (avformat_find_stream_info(format_ctx, NULL) < 0) {
           printf("%s failed!\n", "avformat_find_stream_info");
           return -1;
        }
        
        av_dump_format(format_ctx, 0, NULL, 0);
        
        video_index = av_find_best_stream(format_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
        if(video_index < 0){
           printf("%s failed!\n", "av_find_best_stream");
           return -1;
        }

        codec_ctx = avcodec_alloc_context3(NULL);
        avcodec_parameters_to_context(codec_ctx, format_ctx->streams[video_index]->codecpar);

        codec = avcodec_find_decoder(codec_ctx->codec_id);
        if(avcodec_open2(codec_ctx, codec, NULL)<0){
           printf("%s failed!\n", "avcodec_open2");
           return -1;
        }
        
        frame = av_frame_alloc();
        packet=(AVPacket *)av_malloc(sizeof(AVPacket));
        av_new_packet(packet, codec_ctx->width * codec_ctx->height);

        out_frame = av_frame_alloc();
       
        out_buffer_size = av_image_get_buffer_size(AV_PIX_FMT_BGR32, codec_ctx->width, codec_ctx->height, 1);
        out_buffer = (uint8_t *)av_malloc(out_buffer_size);
        av_image_fill_arrays(out_frame->data, out_frame->linesize, out_buffer, AV_PIX_FMT_BGR32, codec_ctx->width, codec_ctx->height, 1);

        img_convert_ctx = sws_getContext(codec_ctx->width, codec_ctx->height,
                                       codec_ctx->pix_fmt, codec_ctx->width, codec_ctx->height,
                                       AV_PIX_FMT_BGR32, SWS_BICUBIC, NULL, NULL, NULL);

        return 0;
    }
    
    int read() {
        if(av_read_frame(format_ctx, packet) < 0) {
            av_packet_unref(packet);
            printf("%s failed!\n", "av_read_frame");
            return -2;
        }
        
        if(packet->stream_index != video_index) {
            av_packet_unref(packet);
            return -1;
        }
        
        if (avcodec_send_packet(codec_ctx, packet) < 0) {
            av_packet_unref(packet);
            return -1;
        }
        
        if (avcodec_receive_frame(codec_ctx, frame) < 0) {
            av_packet_unref(packet);
            return -1;
        }

        if (sws_scale(img_convert_ctx,
                        (const unsigned char* const*)frame->data, 
                        frame->linesize, 
                        0, 
                        codec_ctx->height,
                        out_frame->data, 
                        out_frame->linesize) < 0){
            return -1;
        }

        av_packet_unref(packet);

        return 0;
    }
    
    void close() {
        if (frame) { 
            av_frame_free(&frame); 
            frame = NULL;
        }

        if (format_ctx) { 
            avformat_close_input(&format_ctx); 
            format_ctx = NULL;
        }

        if (codec_ctx) { 
            avcodec_free_context(&codec_ctx); 
            codec_ctx = NULL;
        }

        if (img_convert_ctx) {
            sws_freeContext(img_convert_ctx);
            img_convert_ctx = NULL;
        }
    }
    
    ~Decoder(){ close(); }
    
public:
    AVFormatContext *format_ctx = NULL;
    int video_index = -1;
    AVCodecContext *codec_ctx = NULL;
    AVCodec *codec = NULL;
    AVFrame  *frame = NULL;
    AVPacket *packet = NULL;
    AVFrame  *out_frame = NULL;
    uint8_t *out_buffer = NULL;
    int out_buffer_size = 0;
    SwsContext *img_convert_ctx = NULL;
};

class Worker {
public:
    Worker(){}
    
    ~Worker() {}
    
    static void run(Worker *context) {
        printf("[Worker::run]\n");

        while (context->status) {
            context->decoder = new Decoder();

            if (context->decoder->open(context->url.c_str()) != -1) {
				while (context->status){
					int code = context->decoder->read();
					
					if (code == -2) { break; }
					
					else if (code == 0) {
						context->tsfn.BlockingCall((void*)context, [](Napi::Env env, Napi::Function jsCallback, void *data){
							Worker *context = static_cast<Worker*>(data);

                            if (context != NULL && context->decoder != NULL) {
                                jsCallback.Call({Napi::Buffer<uint8_t>::Copy(env, context->decoder->out_buffer, context->decoder->out_buffer_size)});
                            }
						});
					}
				}
            }

            context->decoder->close();
            context->decoder = NULL;
        }

        printf("[Worker::exit]\n");
    }

public:
    std::thread task;
    std::string url;
    std::atomic<bool> status = true;
    Decoder *decoder = NULL;
    Napi::ThreadSafeFunction tsfn;
};

class Wrapper : public Napi::ObjectWrap<Wrapper> {
public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports) {
          Napi::Function func =
          DefineClass(env,
                      "Rtsp",
                       {
                           InstanceMethod<&Wrapper::open>("open"),
                           InstanceMethod<&Wrapper::close>("close"),
                       });
          Napi::FunctionReference* cons = new Napi::FunctionReference();
          *cons = Napi::Persistent(func);
          env.SetInstanceData<Napi::FunctionReference>(cons);
          exports.Set("Rtsp", func);
          return exports;
    }
    
    Wrapper(const Napi::CallbackInfo& info) : Napi::ObjectWrap<Wrapper>(info) {
    }
    
    ~Wrapper() {
        if(context) { delete context; }
    }
    
    Napi::Value open(const Napi::CallbackInfo& info) {
       Napi::Env env = info.Env();
       printf("[Napi::open]\n");

       if (info.Length() < 2) {
            return env.Null();
       }

       if (!info[0].IsString() || !info[1].IsFunction()) {
            return env.Null();
       }
   
       context = new Worker();
       context->url = info[0].As<Napi::String>().Utf8Value();
       context->tsfn = Napi::ThreadSafeFunction::New(env,                    
                              info[1].As<Napi::Function>(), 
                              "Worker",                 
                              0,        
                              1,       
                              (void*)NULL, 
                              [](Napi::Env env, void *finalizeData, void *data){
                                    printf("[Napi::ThreadSafeFunctionEnd]\n");
                              },
                              (void*)NULL); 
       context->task = std::thread(Worker::run, context);         
       return env.Null();
    }
    
    Napi::Value close(const Napi::CallbackInfo& info) {
        printf("[Napi::close]\n");
        Napi::Env env = info.Env();
       
        context->status = false;
        context->task.join();
        context->tsfn.Release();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        return env.Null();
    }
private:
    Worker *context = NULL;
};

Napi::Object Init(Napi::Env env, Napi::Object exports) {
  return Wrapper::Init(env, exports);
}

NODE_API_MODULE(NODE_GYP_MODULE_NAME, Init);
