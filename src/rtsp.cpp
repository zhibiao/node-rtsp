#include <node_api.h>

extern "C" {
    #include "libavcodec/avcodec.h"
    #include "libavformat/avformat.h"
    #include "libswscale/swscale.h"
    #include "libavutil/imgutils.h"
}

class AVContext {
public:
    AVContext(){}
    
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
       
        out_buffer_size = av_image_get_buffer_size(AV_PIX_FMT_RGB32, codec_ctx->width, codec_ctx->height, 1);
        out_buffer = (uint8_t *)av_malloc(out_buffer_size);
        av_image_fill_arrays(out_frame->data, out_frame->linesize, out_buffer, AV_PIX_FMT_RGB32, codec_ctx->width, codec_ctx->height, 1);

        img_convert_ctx = sws_getContext(codec_ctx->width, codec_ctx->height,
                                       codec_ctx->pix_fmt, codec_ctx->width, codec_ctx->height,
                                       AV_PIX_FMT_RGB32, SWS_BICUBIC, NULL, NULL, NULL);

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
    
    ~AVContext(){ close(); }
    
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

class Wrapper {
public:
    Wrapper(){}
    
    ~Wrapper(){
        if (_ctx) { delete _ctx; }
        
        napi_delete_reference(_env, _wrapper);
    }
    
    static napi_value Init(napi_env env, napi_value exports){
        
        napi_property_descriptor properties[] = {
          { "open", 0, open, 0, 0, 0, napi_default, 0 },
          { "read", 0, read, 0, 0, 0, napi_default, 0 },
          { "close", 0, close, 0, 0, 0, napi_default, 0 }
        };
        
        napi_value cons;
        if (napi_define_class(env,
                        "Rtsp",
                        NAPI_AUTO_LENGTH,
                        New, 
                        NULL, 
                        sizeof(properties) / sizeof(properties[0]), 
                        properties,
                        &cons) != napi_ok) {
            napi_throw_error(env, NULL, "napi_define_class");
            return NULL;
        }
        
       napi_ref* cons_ref = new napi_ref;
       if (napi_create_reference(env, cons, 1, cons_ref) != napi_ok){
            napi_throw_error(env, NULL, "napi_create_reference");
            return NULL;
       }
       
       if (napi_set_instance_data(env, 
                             cons_ref,
                             [](napi_env env, void* data, void* hint) {
                                napi_ref* cons_ref = static_cast<napi_ref*>(data);
                                napi_delete_reference(env, *cons_ref);
                                delete cons_ref;
                             },
                             NULL) != napi_ok) {
            napi_throw_error(env, NULL, "napi_set_instance_data");
            return NULL;
        }
                             
       if (napi_set_named_property(env, exports, "Rtsp", cons) != napi_ok) {
           napi_throw_error(env, NULL, "napi_set_named_property");
           return NULL;
       }
       
       return exports;
    }
    
    static napi_value New(napi_env env, napi_callback_info info) {

        napi_value target;
        if (napi_get_new_target(env, info, &target) != napi_ok){
           napi_throw_error(env, NULL, "napi_get_new_target");
           return NULL;
        }
        
        if (target != NULL) {
            size_t argc = 1;
            napi_value args[1];
            napi_value _this;
            if (napi_get_cb_info(env, info, &argc, args, &_this, NULL) != napi_ok){
                napi_throw_error(env, NULL, "napi_get_cb_info");
                return NULL;
            }
            
            Wrapper* obj = new Wrapper();
            obj->_env = env;
            obj->_ctx = new AVContext();
            
            if (napi_wrap(env,
                      _this,
                      reinterpret_cast<void*>(obj),
                      Wrapper::Destructor,
                      NULL, 
                      &obj->_wrapper) != napi_ok) {
                napi_throw_error(env, NULL, "napi_wrap");
                return NULL;
            }
                      
            return _this;
        }else {
            
            size_t argc = 1;
            napi_value args[1];
            if (napi_get_cb_info(env, info, &argc, args, NULL, NULL) != napi_ok ){
                napi_throw_error(env, NULL, "napi_get_cb_info");
                return NULL;
            }

            napi_value argv[] = { args[0] };

            napi_value instance;
            if (napi_new_instance(env, 
                              Constructor(env), 
                              sizeof(argv) / sizeof(argv[0]), 
                              argv, 
                              &instance) != napi_ok) {
                napi_throw_error(env, NULL, "napi_new_instance");
                return NULL;
            }
           
            return instance;
        }
    }
    
    static napi_value Constructor(napi_env env){
        void* data = NULL;
        if (napi_get_instance_data(env, &data) != napi_ok) {
            napi_throw_error(env, NULL, "napi_get_instance_data");
            return NULL;
        }

        napi_ref* cons_ref = static_cast<napi_ref*>(data);
        
        napi_value cons;
        if (napi_get_reference_value(env, *cons_ref, &cons) != napi_ok){
            napi_throw_error(env, NULL, "napi_get_reference_value");
            return NULL;
        }
        return cons;
    }
    
    static void Destructor(napi_env env, void* data, void* hint){
        reinterpret_cast<Wrapper*>(data)->~Wrapper();
    }
    
    static napi_value open(napi_env env, napi_callback_info info) {
        napi_value _this;
        if (napi_get_cb_info(env, info, NULL, NULL, &_this, NULL) != napi_ok) {
            napi_throw_error(env, NULL, "napi_get_cb_info");
            return NULL;
        }

        Wrapper* obj = NULL;
        if (napi_unwrap(env, _this, reinterpret_cast<void**>(&obj)) != napi_ok){
            napi_throw_error(env, NULL, "napi_unwrap");
            return NULL;
        }
        AVContext *ctx = static_cast<AVContext*>(obj->_ctx);
        
        size_t argc = 1;
        napi_value args[1];
        if (napi_get_cb_info(env, info, &argc, args, NULL, NULL) != napi_ok){
            napi_throw_error(env, NULL, "napi_get_cb_info");
            return NULL;
        }
        
        char url[128] = {0};
        size_t len = 0;
        if (napi_get_value_string_utf8(env, args[0], url, sizeof(url), &len) != napi_ok){
            napi_throw_error(env, NULL, "napi_get_value_string_utf8");
            return NULL;
        }

        if (ctx->open(url) < 0) {
            napi_throw_error(env, NULL, "open");
            return NULL;
        }

        return NULL;
    }
    
    static napi_value read(napi_env env, napi_callback_info info) {
       napi_value _this;
        if (napi_get_cb_info(env, info, NULL, NULL, &_this, NULL) != napi_ok) {
            napi_throw_error(env, NULL, "napi_get_cb_info");
            return NULL;
        }

        Wrapper* obj = NULL;
        if (napi_unwrap(env, _this, reinterpret_cast<void**>(&obj)) != napi_ok){
            napi_throw_error(env, NULL, "napi_unwrap");
            return NULL;
        }
        AVContext *ctx = static_cast<AVContext*>(obj->_ctx);

        switch (ctx->read()) {
            case -2: 
                napi_throw_error(env, NULL, "read");
                return NULL;
               
            case 0: {
                void *result_data = NULL;
                napi_value result;
                if (napi_create_buffer_copy(env, 
                                            ctx->out_buffer_size, 
                                            ctx->out_buffer,
                                            &result_data,
                                            &result) != napi_ok){
                    return NULL;
                }
                return result;
            }

            default: { return NULL; }
        }
    }
    
    static napi_value close(napi_env env, napi_callback_info info) {
        napi_value _this;
        if (napi_get_cb_info(env, info, NULL, NULL, &_this, NULL) != napi_ok) {
            napi_throw_error(env, NULL, "napi_get_cb_info");
            return NULL;
        }

        Wrapper* obj = NULL;
        if (napi_unwrap(env, _this, reinterpret_cast<void**>(&obj)) != napi_ok){
            napi_throw_error(env, NULL, "napi_unwrap");
            return NULL;
        }
        AVContext *ctx = static_cast<AVContext*>(obj->_ctx);
        ctx->close();
        return NULL;
    }
    
private:
    napi_env _env = NULL;
    napi_ref _wrapper = NULL;
    AVContext *_ctx = NULL;
};

napi_value Init (napi_env env, napi_value exports) {
    return Wrapper::Init(env, exports);
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, Init);