# Videos SFD de Infinite World (frente R7, 2026-10-16): decodificador MPEG-1/2 del FFmpeg 4.4 que ya trae el SDK
# (rexglue-sdk-0.10/thirdparty/FFmpeg), compilado aqui como objetivo estatico propio y enlazado SOLO en dbz3.exe:
# las DLL canonicas del runtime no cambian. libavutil es la del SDK instalado (rex::libavutil, la misma que usa el
# runtime para XMA). El demux MPEG-PS y el ADX son propios (src/video_sfd.cpp).
if(NOT WIN32)   # config de FFmpeg solo para Windows x64; fuera de Windows video_sfd.cpp no reproduce
    return()
endif()
enable_language(C)
set(DBZ3_FF "${CMAKE_CURRENT_SOURCE_DIR}/rexglue-sdk-0.10/thirdparty/FFmpeg")
set(DBZ3_FF_OV "${CMAKE_CURRENT_BINARY_DIR}/ffmpeg_video")
# config.h propio = el del SDK con los componentes del decodificador de video encendidos (configure de 4.4:
# mpeg2video_decoder_select="mpegvideo", mpegvideo_select="blockdsp h264chroma hpeldsp idctdsp me_cmp mpeg_er
# videodsp", me_cmp_select="fdctdsp idctdsp pixblockdsp", mpeg_er_select="error_resilience")
file(CONFIGURE OUTPUT "${DBZ3_FF_OV}/config.h" @ONLY CONTENT [[
#include "config_windows_x86_64.h"
#undef CONFIG_MPEG1VIDEO_DECODER
#undef CONFIG_MPEG2VIDEO_DECODER
#undef CONFIG_MPEGVIDEO_PARSER
#undef CONFIG_MPEGVIDEO
#undef CONFIG_MPEG_ER
#undef CONFIG_ERROR_RESILIENCE
#undef CONFIG_BLOCKDSP
#undef CONFIG_H264CHROMA
#undef CONFIG_HPELDSP
#undef CONFIG_ME_CMP
#undef CONFIG_VIDEODSP
#undef CONFIG_PIXBLOCKDSP
#undef CONFIG_FDCTDSP
#undef CONFIG_IDCTDSP
#define CONFIG_MPEG1VIDEO_DECODER 1
#define CONFIG_MPEG2VIDEO_DECODER 1
#define CONFIG_MPEGVIDEO_PARSER 1
#define CONFIG_MPEGVIDEO 1
#define CONFIG_MPEG_ER 1
#define CONFIG_ERROR_RESILIENCE 1
#define CONFIG_BLOCKDSP 1
#define CONFIG_H264CHROMA 1
#define CONFIG_HPELDSP 1
#define CONFIG_ME_CMP 1
#define CONFIG_VIDEODSP 1
#define CONFIG_PIXBLOCKDSP 1
#define CONFIG_FDCTDSP 1
#define CONFIG_IDCTDSP 1
]])
file(CONFIGURE OUTPUT "${DBZ3_FF_OV}/libavcodec/codec_list.c" @ONLY CONTENT [[
static const AVCodec * const codec_list[] = { &ff_mpeg1video_decoder, &ff_mpeg2video_decoder, &ff_xmaframes_decoder, NULL };
]])
file(CONFIGURE OUTPUT "${DBZ3_FF_OV}/libavcodec/parser_list.c" @ONLY CONTENT [[
static const AVCodecParser * const parser_list[] = { &ff_mpegvideo_parser, NULL };
]])
file(CONFIGURE OUTPUT "${DBZ3_FF_OV}/libavcodec/bsf_list.c" @ONLY CONTENT [[
static const AVBitStreamFilter * const bitstream_filters[] = { &ff_null_bsf, NULL };
]])
set(_ff_src
    allcodecs avcodec avpacket avpicture bitstream bitstream_filter bitstream_filters bsf codec_desc codec_par decode
    encode imgconvert mathtables options parser parsers profiles raw utils null_bsf pthread pthread_slice
    pthread_frame mpeg12framerate idctdsp simple_idct jrevdct faanidct faandct fdctdsp jfdctfst jfdctint
    mpeg12dec mpeg12 mpeg12data mpegvideo mpegvideodsp rl mpegvideo_motion mpegutils mpegvideodata mpegpicture
    mpeg_er error_resilience blockdsp h264chroma hpeldsp me_cmp videodsp pixblockdsp mpegvideo_parser
    wmaprodec wma wma_common wma_freqs sinewin mdct_float mdct_fixed_32 fft_float fft_fixed_32 fft_init_table avfft
    rdft dct dct32_float dct32_fixed aactab x86/fft_init x86/dct_init
    x86/constants x86/idctdsp_init x86/fdctdsp_init x86/fdct x86/blockdsp_init x86/h264chroma_init
    x86/hpeldsp_init x86/me_cmp_init x86/videodsp_init x86/pixblockdsp_init x86/mpegvideo x86/mpegvideodsp)
list(TRANSFORM _ff_src PREPEND "${DBZ3_FF}/libavcodec/")
list(TRANSFORM _ff_src APPEND ".c")
add_library(dbz3_ffvideo STATIC ${_ff_src})
target_include_directories(dbz3_ffvideo BEFORE PRIVATE "${DBZ3_FF_OV}")
target_include_directories(dbz3_ffvideo PRIVATE "${DBZ3_FF}" "${DBZ3_FF}/compat/atomics/win32")
target_compile_definitions(dbz3_ffvideo PRIVATE HAVE_AV_CONFIG_H _USE_MATH_DEFINES)
target_compile_options(dbz3_ffvideo PRIVATE -w)
target_link_libraries(dbz3_ffvideo PUBLIC rex::libavutil bcrypt)
target_link_libraries(dbz3 PRIVATE dbz3_ffvideo winmm)
# solo el reproductor ve las cabeceras de FFmpeg (la raiz tiene un config.h que no debe ver el resto del juego)
set_property(SOURCE src/video_sfd.cpp APPEND PROPERTY INCLUDE_DIRECTORIES "${DBZ3_FF}")
