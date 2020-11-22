{
  "targets": [
    {
        "target_name": "rtsp",
        "sources": [ "src/rtsp.cpp" ],
        "include_dirs": [
            "<(module_root_dir)/vendor/ffmpeg-4.2.2/include"
        ],
        "conditions": [
            ["OS=='win'", {
                "libraries": [
                    "<(module_root_dir)/vendor/ffmpeg-4.2.2/lib/avcodec.lib",
                    "<(module_root_dir)/vendor/ffmpeg-4.2.2/lib/avdevice.lib",
                    "<(module_root_dir)/vendor/ffmpeg-4.2.2/lib/avfilter.lib",
                    "<(module_root_dir)/vendor/ffmpeg-4.2.2/lib/avformat.lib",
                    "<(module_root_dir)/vendor/ffmpeg-4.2.2/lib/avutil.lib",
                    "<(module_root_dir)/vendor/ffmpeg-4.2.2/lib/postproc.lib",
                    "<(module_root_dir)/vendor/ffmpeg-4.2.2/lib/swresample.lib",
                    "<(module_root_dir)/vendor/ffmpeg-4.2.2/lib/swscale.lib" 
                ]
            }]
        ]
    },
    
    {
        "target_name": "copy-vendors",
        "dependencies" : [ "rtsp" ],
        "copies": [
            {
                "destination": "<(module_root_dir)/build/Release/",
                "files": [
                    "<(module_root_dir)/vendor/ffmpeg-4.2.2/bin/*"
                ]
            }
        ]
    }
  ]
}