/*
    This file is part of the Fluggo Media Library for high-quality
    video and audio processing.

    Copyright 2009 Brian J. Crowell <brian@fluggo.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdint.h>

#include "pyframework.h"
#include <structmember.h>
#include <libavformat/avformat.h>

PyObject *py_writeVideo( PyObject *self, PyObject *args, PyObject *kw );

static PyMethodDef module_methods[] = {
    { "write_video", (PyCFunction) py_writeVideo, METH_VARARGS | METH_KEYWORDS,
        "TBD" },
    { NULL }
};

void init_AVVideoSource( PyObject *module );
void init_AVVideoDecoder( PyObject *module );
void init_AVVideoEncoder( PyObject *module );
void init_AVAudioSource( PyObject *module );
void init_AVAudioDecoder( PyObject *module );
void init_AVDemuxer( PyObject *module );
void init_AVMuxer( PyObject *module );
void init_AVContainer( PyObject *module );

EXPORT PyMODINIT_FUNC
PyInit_libav() {
    static PyModuleDef mdef = {
        .m_base = PyModuleDef_HEAD_INIT,
        .m_name = "libav",
        .m_doc = "Libav support for the Fluggo media processing library.",

        // TODO: Consider making use of this; see Python docs
        .m_size = -1,
        .m_methods = module_methods,

        // TODO: Consider supporting module cleanup
    };

    PyObject *m = PyModule_Create( &mdef );

    // Make sure process is available and initialized
    if( !PyImport_ImportModule( "fluggo.media.process" ) )
        return NULL;

    init_AVVideoSource( m );
    init_AVVideoDecoder( m );
    init_AVVideoEncoder( m );
    init_AVAudioSource( m );
    init_AVAudioDecoder( m );
    init_AVDemuxer( m );
    init_AVMuxer( m );
    init_AVContainer( m );

    if( !g_thread_supported() )
        g_thread_init( NULL );

    // Declare Libav pixel formats
    PyModule_AddIntMacro( m, PIX_FMT_NONE );
    PyModule_AddIntMacro( m, PIX_FMT_YUV420P );
    PyModule_AddIntMacro( m, PIX_FMT_YUYV422 );
    PyModule_AddIntMacro( m, PIX_FMT_RGB24 );
    PyModule_AddIntMacro( m, PIX_FMT_BGR24 );
    PyModule_AddIntMacro( m, PIX_FMT_YUV422P );
    PyModule_AddIntMacro( m, PIX_FMT_YUV444P );
    PyModule_AddIntMacro( m, PIX_FMT_YUV410P );
    PyModule_AddIntMacro( m, PIX_FMT_YUV411P );
    PyModule_AddIntMacro( m, PIX_FMT_GRAY8 );
    PyModule_AddIntMacro( m, PIX_FMT_MONOWHITE );
    PyModule_AddIntMacro( m, PIX_FMT_MONOBLACK );
    PyModule_AddIntMacro( m, PIX_FMT_PAL8 );
    PyModule_AddIntMacro( m, PIX_FMT_YUVJ420P );
    PyModule_AddIntMacro( m, PIX_FMT_YUVJ422P );
    PyModule_AddIntMacro( m, PIX_FMT_YUVJ444P );
    PyModule_AddIntMacro( m, PIX_FMT_XVMC_MPEG2_MC );
    PyModule_AddIntMacro( m, PIX_FMT_XVMC_MPEG2_IDCT );
    PyModule_AddIntMacro( m, PIX_FMT_UYVY422 );
    PyModule_AddIntMacro( m, PIX_FMT_UYYVYY411 );
    PyModule_AddIntMacro( m, PIX_FMT_BGR8 );
    PyModule_AddIntMacro( m, PIX_FMT_BGR4 );
    PyModule_AddIntMacro( m, PIX_FMT_BGR4_BYTE );
    PyModule_AddIntMacro( m, PIX_FMT_RGB8 );
    PyModule_AddIntMacro( m, PIX_FMT_RGB4 );
    PyModule_AddIntMacro( m, PIX_FMT_RGB4_BYTE );
    PyModule_AddIntMacro( m, PIX_FMT_NV12 );
    PyModule_AddIntMacro( m, PIX_FMT_NV21 );

    PyModule_AddIntMacro( m, PIX_FMT_ARGB );
    PyModule_AddIntMacro( m, PIX_FMT_RGBA );
    PyModule_AddIntMacro( m, PIX_FMT_ABGR );
    PyModule_AddIntMacro( m, PIX_FMT_BGRA );

    PyModule_AddIntMacro( m, PIX_FMT_GRAY16BE );
    PyModule_AddIntMacro( m, PIX_FMT_GRAY16LE );
    PyModule_AddIntMacro( m, PIX_FMT_YUV440P );
    PyModule_AddIntMacro( m, PIX_FMT_YUVJ440P );
    PyModule_AddIntMacro( m, PIX_FMT_YUVA420P );
    PyModule_AddIntMacro( m, PIX_FMT_VDPAU_H264 );
    PyModule_AddIntMacro( m, PIX_FMT_VDPAU_MPEG1 );
    PyModule_AddIntMacro( m, PIX_FMT_VDPAU_MPEG2 );
    PyModule_AddIntMacro( m, PIX_FMT_VDPAU_WMV3 );
    PyModule_AddIntMacro( m, PIX_FMT_VDPAU_VC1 );
    PyModule_AddIntMacro( m, PIX_FMT_RGB48BE );
    PyModule_AddIntMacro( m, PIX_FMT_RGB48LE );

    //PyModule_AddIntMacro( m, PIX_FMT_RGB565BE );
    //PyModule_AddIntMacro( m, PIX_FMT_RGB565LE );
    //PyModule_AddIntMacro( m, PIX_FMT_RGB555BE );
    //PyModule_AddIntMacro( m, PIX_FMT_RGB555LE );

    //PyModule_AddIntMacro( m, PIX_FMT_BGR565BE );
    //PyModule_AddIntMacro( m, PIX_FMT_BGR565LE );
    //PyModule_AddIntMacro( m, PIX_FMT_BGR555BE );
    //PyModule_AddIntMacro( m, PIX_FMT_BGR555LE );

    PyModule_AddIntMacro( m, PIX_FMT_VAAPI_MOCO );
    PyModule_AddIntMacro( m, PIX_FMT_VAAPI_IDCT );
    PyModule_AddIntMacro( m, PIX_FMT_VAAPI_VLD );

    //PyModule_AddIntMacro( m, PIX_FMT_YUV420P16LE );
    //PyModule_AddIntMacro( m, PIX_FMT_YUV420P16BE );
    //PyModule_AddIntMacro( m, PIX_FMT_YUV422P16LE );
    //PyModule_AddIntMacro( m, PIX_FMT_YUV422P16BE );
    //PyModule_AddIntMacro( m, PIX_FMT_YUV444P16LE );
    //PyModule_AddIntMacro( m, PIX_FMT_YUV444P16BE );

    PyModule_AddIntMacro( m, PIX_FMT_RGB32 );
    PyModule_AddIntMacro( m, PIX_FMT_RGB32_1 );
    PyModule_AddIntMacro( m, PIX_FMT_BGR32 );
    PyModule_AddIntMacro( m, PIX_FMT_BGR32_1 );

    PyModule_AddIntMacro( m, PIX_FMT_GRAY16 );
    PyModule_AddIntMacro( m, PIX_FMT_RGB48 );
    PyModule_AddIntMacro( m, PIX_FMT_RGB565 );
    PyModule_AddIntMacro( m, PIX_FMT_RGB555 );
    PyModule_AddIntMacro( m, PIX_FMT_BGR565 );
    PyModule_AddIntMacro( m, PIX_FMT_BGR555 );

    //PyModule_AddIntMacro( m, PIX_FMT_YUV420P16 );
    //PyModule_AddIntMacro( m, PIX_FMT_YUV422P16 );
    //PyModule_AddIntMacro( m, PIX_FMT_YUV444P16 );

    // Codecs
    PyModule_AddIntMacro( m, CODEC_ID_NONE );

    PyModule_AddIntMacro( m, CODEC_ID_MPEG1VIDEO );
    PyModule_AddIntMacro( m, CODEC_ID_MPEG2VIDEO );
    PyModule_AddIntMacro( m, CODEC_ID_MPEG2VIDEO_XVMC );
    PyModule_AddIntMacro( m, CODEC_ID_H261 );
    PyModule_AddIntMacro( m, CODEC_ID_H263 );
    PyModule_AddIntMacro( m, CODEC_ID_RV10 );
    PyModule_AddIntMacro( m, CODEC_ID_RV20 );
    PyModule_AddIntMacro( m, CODEC_ID_MJPEG );
    PyModule_AddIntMacro( m, CODEC_ID_MJPEGB );
    PyModule_AddIntMacro( m, CODEC_ID_LJPEG );
    PyModule_AddIntMacro( m, CODEC_ID_SP5X );
    PyModule_AddIntMacro( m, CODEC_ID_JPEGLS );
    PyModule_AddIntMacro( m, CODEC_ID_MPEG4 );
    PyModule_AddIntMacro( m, CODEC_ID_RAWVIDEO );
    PyModule_AddIntMacro( m, CODEC_ID_MSMPEG4V1 );
    PyModule_AddIntMacro( m, CODEC_ID_MSMPEG4V2 );
    PyModule_AddIntMacro( m, CODEC_ID_MSMPEG4V3 );
    PyModule_AddIntMacro( m, CODEC_ID_WMV1 );
    PyModule_AddIntMacro( m, CODEC_ID_WMV2 );
    PyModule_AddIntMacro( m, CODEC_ID_H263P );
    PyModule_AddIntMacro( m, CODEC_ID_H263I );
    PyModule_AddIntMacro( m, CODEC_ID_FLV1 );
    PyModule_AddIntMacro( m, CODEC_ID_SVQ1 );
    PyModule_AddIntMacro( m, CODEC_ID_SVQ3 );
    PyModule_AddIntMacro( m, CODEC_ID_DVVIDEO );
    PyModule_AddIntMacro( m, CODEC_ID_HUFFYUV );
    PyModule_AddIntMacro( m, CODEC_ID_CYUV );
    PyModule_AddIntMacro( m, CODEC_ID_H264 );
    PyModule_AddIntMacro( m, CODEC_ID_INDEO3 );
    PyModule_AddIntMacro( m, CODEC_ID_VP3 );
    PyModule_AddIntMacro( m, CODEC_ID_THEORA );
    PyModule_AddIntMacro( m, CODEC_ID_ASV1 );
    PyModule_AddIntMacro( m, CODEC_ID_ASV2 );
    PyModule_AddIntMacro( m, CODEC_ID_FFV1 );
    PyModule_AddIntMacro( m, CODEC_ID_4XM );
    PyModule_AddIntMacro( m, CODEC_ID_VCR1 );
    PyModule_AddIntMacro( m, CODEC_ID_CLJR );
    PyModule_AddIntMacro( m, CODEC_ID_MDEC );
    PyModule_AddIntMacro( m, CODEC_ID_ROQ );
    PyModule_AddIntMacro( m, CODEC_ID_INTERPLAY_VIDEO );
    PyModule_AddIntMacro( m, CODEC_ID_XAN_WC3 );
    PyModule_AddIntMacro( m, CODEC_ID_XAN_WC4 );
    PyModule_AddIntMacro( m, CODEC_ID_RPZA );
    PyModule_AddIntMacro( m, CODEC_ID_CINEPAK );
    PyModule_AddIntMacro( m, CODEC_ID_WS_VQA );
    PyModule_AddIntMacro( m, CODEC_ID_MSRLE );
    PyModule_AddIntMacro( m, CODEC_ID_MSVIDEO1 );
    PyModule_AddIntMacro( m, CODEC_ID_IDCIN );
    PyModule_AddIntMacro( m, CODEC_ID_8BPS );
    PyModule_AddIntMacro( m, CODEC_ID_SMC );
    PyModule_AddIntMacro( m, CODEC_ID_FLIC );
    PyModule_AddIntMacro( m, CODEC_ID_TRUEMOTION1 );
    PyModule_AddIntMacro( m, CODEC_ID_VMDVIDEO );
    PyModule_AddIntMacro( m, CODEC_ID_MSZH );
    PyModule_AddIntMacro( m, CODEC_ID_ZLIB );
    PyModule_AddIntMacro( m, CODEC_ID_QTRLE );
    PyModule_AddIntMacro( m, CODEC_ID_SNOW );
    PyModule_AddIntMacro( m, CODEC_ID_TSCC );
    PyModule_AddIntMacro( m, CODEC_ID_ULTI );
    PyModule_AddIntMacro( m, CODEC_ID_QDRAW );
    PyModule_AddIntMacro( m, CODEC_ID_VIXL );
    PyModule_AddIntMacro( m, CODEC_ID_QPEG );
    PyModule_AddIntMacro( m, CODEC_ID_PNG );
    PyModule_AddIntMacro( m, CODEC_ID_PPM );
    PyModule_AddIntMacro( m, CODEC_ID_PBM );
    PyModule_AddIntMacro( m, CODEC_ID_PGM );
    PyModule_AddIntMacro( m, CODEC_ID_PGMYUV );
    PyModule_AddIntMacro( m, CODEC_ID_PAM );
    PyModule_AddIntMacro( m, CODEC_ID_FFVHUFF );
    PyModule_AddIntMacro( m, CODEC_ID_RV30 );
    PyModule_AddIntMacro( m, CODEC_ID_RV40 );
    PyModule_AddIntMacro( m, CODEC_ID_VC1 );
    PyModule_AddIntMacro( m, CODEC_ID_WMV3 );
    PyModule_AddIntMacro( m, CODEC_ID_LOCO );
    PyModule_AddIntMacro( m, CODEC_ID_WNV1 );
    PyModule_AddIntMacro( m, CODEC_ID_AASC );
    PyModule_AddIntMacro( m, CODEC_ID_INDEO2 );
    PyModule_AddIntMacro( m, CODEC_ID_FRAPS );
    PyModule_AddIntMacro( m, CODEC_ID_TRUEMOTION2 );
    PyModule_AddIntMacro( m, CODEC_ID_BMP );
    PyModule_AddIntMacro( m, CODEC_ID_CSCD );
    PyModule_AddIntMacro( m, CODEC_ID_MMVIDEO );
    PyModule_AddIntMacro( m, CODEC_ID_ZMBV );
    PyModule_AddIntMacro( m, CODEC_ID_AVS );
    PyModule_AddIntMacro( m, CODEC_ID_SMACKVIDEO );
    PyModule_AddIntMacro( m, CODEC_ID_NUV );
    PyModule_AddIntMacro( m, CODEC_ID_KMVC );
    PyModule_AddIntMacro( m, CODEC_ID_FLASHSV );
    PyModule_AddIntMacro( m, CODEC_ID_CAVS );
    PyModule_AddIntMacro( m, CODEC_ID_JPEG2000 );
    PyModule_AddIntMacro( m, CODEC_ID_VMNC );
    PyModule_AddIntMacro( m, CODEC_ID_VP5 );
    PyModule_AddIntMacro( m, CODEC_ID_VP6 );
    PyModule_AddIntMacro( m, CODEC_ID_VP6F );
    PyModule_AddIntMacro( m, CODEC_ID_TARGA );
    PyModule_AddIntMacro( m, CODEC_ID_DSICINVIDEO );
    PyModule_AddIntMacro( m, CODEC_ID_TIERTEXSEQVIDEO );
    PyModule_AddIntMacro( m, CODEC_ID_TIFF );
    PyModule_AddIntMacro( m, CODEC_ID_GIF );
    PyModule_AddIntMacro( m, CODEC_ID_FFH264 );
    PyModule_AddIntMacro( m, CODEC_ID_DXA );
    PyModule_AddIntMacro( m, CODEC_ID_DNXHD );
    PyModule_AddIntMacro( m, CODEC_ID_THP );
    PyModule_AddIntMacro( m, CODEC_ID_SGI );
    PyModule_AddIntMacro( m, CODEC_ID_C93 );
    PyModule_AddIntMacro( m, CODEC_ID_BETHSOFTVID );
    PyModule_AddIntMacro( m, CODEC_ID_PTX );
    PyModule_AddIntMacro( m, CODEC_ID_TXD );
    PyModule_AddIntMacro( m, CODEC_ID_VP6A );
    PyModule_AddIntMacro( m, CODEC_ID_AMV );
    PyModule_AddIntMacro( m, CODEC_ID_VB );
    PyModule_AddIntMacro( m, CODEC_ID_PCX );
    PyModule_AddIntMacro( m, CODEC_ID_SUNRAST );
    PyModule_AddIntMacro( m, CODEC_ID_INDEO4 );
    PyModule_AddIntMacro( m, CODEC_ID_INDEO5 );
    PyModule_AddIntMacro( m, CODEC_ID_MIMIC );
    PyModule_AddIntMacro( m, CODEC_ID_RL2 );
    PyModule_AddIntMacro( m, CODEC_ID_8SVX_EXP );
    PyModule_AddIntMacro( m, CODEC_ID_8SVX_FIB );
    PyModule_AddIntMacro( m, CODEC_ID_ESCAPE124 );
    PyModule_AddIntMacro( m, CODEC_ID_DIRAC );
    PyModule_AddIntMacro( m, CODEC_ID_BFI );
    PyModule_AddIntMacro( m, CODEC_ID_CMV );
    PyModule_AddIntMacro( m, CODEC_ID_MOTIONPIXELS );
    PyModule_AddIntMacro( m, CODEC_ID_TGV );
    PyModule_AddIntMacro( m, CODEC_ID_TGQ );
    PyModule_AddIntMacro( m, CODEC_ID_TQI );
    //PyModule_AddIntMacro( m, CODEC_ID_AURA );
    //PyModule_AddIntMacro( m, CODEC_ID_AURA2 );
    //PyModule_AddIntMacro( m, CODEC_ID_V210X );
    //PyModule_AddIntMacro( m, CODEC_ID_TMV );
    //PyModule_AddIntMacro( m, CODEC_ID_V210 );
    //PyModule_AddIntMacro( m, CODEC_ID_DPX );
    //PyModule_AddIntMacro( m, CODEC_ID_MAD );

    PyModule_AddIntMacro( m, CODEC_ID_PCM_S16LE );
    PyModule_AddIntMacro( m, CODEC_ID_PCM_S16BE );
    PyModule_AddIntMacro( m, CODEC_ID_PCM_U16LE );
    PyModule_AddIntMacro( m, CODEC_ID_PCM_U16BE );
    PyModule_AddIntMacro( m, CODEC_ID_PCM_S8 );
    PyModule_AddIntMacro( m, CODEC_ID_PCM_U8 );
    PyModule_AddIntMacro( m, CODEC_ID_PCM_MULAW );
    PyModule_AddIntMacro( m, CODEC_ID_PCM_ALAW );
    PyModule_AddIntMacro( m, CODEC_ID_PCM_S32LE );
    PyModule_AddIntMacro( m, CODEC_ID_PCM_S32BE );
    PyModule_AddIntMacro( m, CODEC_ID_PCM_U32LE );
    PyModule_AddIntMacro( m, CODEC_ID_PCM_U32BE );
    PyModule_AddIntMacro( m, CODEC_ID_PCM_S24LE );
    PyModule_AddIntMacro( m, CODEC_ID_PCM_S24BE );
    PyModule_AddIntMacro( m, CODEC_ID_PCM_U24LE );
    PyModule_AddIntMacro( m, CODEC_ID_PCM_U24BE );
    PyModule_AddIntMacro( m, CODEC_ID_PCM_S24DAUD );
    PyModule_AddIntMacro( m, CODEC_ID_PCM_ZORK );
    PyModule_AddIntMacro( m, CODEC_ID_PCM_S16LE_PLANAR );
    PyModule_AddIntMacro( m, CODEC_ID_PCM_DVD );
    PyModule_AddIntMacro( m, CODEC_ID_PCM_F32BE );
    PyModule_AddIntMacro( m, CODEC_ID_PCM_F32LE );
    PyModule_AddIntMacro( m, CODEC_ID_PCM_F64BE );
    PyModule_AddIntMacro( m, CODEC_ID_PCM_F64LE );
    //PyModule_AddIntMacro( m, CODEC_ID_PCM_BLURAY );

    PyModule_AddIntMacro( m, CODEC_ID_ADPCM_IMA_QT );
    PyModule_AddIntMacro( m, CODEC_ID_ADPCM_IMA_WAV );
    PyModule_AddIntMacro( m, CODEC_ID_ADPCM_IMA_DK3 );
    PyModule_AddIntMacro( m, CODEC_ID_ADPCM_IMA_DK4 );
    PyModule_AddIntMacro( m, CODEC_ID_ADPCM_IMA_WS );
    PyModule_AddIntMacro( m, CODEC_ID_ADPCM_IMA_SMJPEG );
    PyModule_AddIntMacro( m, CODEC_ID_ADPCM_MS );
    PyModule_AddIntMacro( m, CODEC_ID_ADPCM_4XM );
    PyModule_AddIntMacro( m, CODEC_ID_ADPCM_XA );
    PyModule_AddIntMacro( m, CODEC_ID_ADPCM_ADX );
    PyModule_AddIntMacro( m, CODEC_ID_ADPCM_EA );
    PyModule_AddIntMacro( m, CODEC_ID_ADPCM_G726 );
    PyModule_AddIntMacro( m, CODEC_ID_ADPCM_CT );
    PyModule_AddIntMacro( m, CODEC_ID_ADPCM_SWF );
    PyModule_AddIntMacro( m, CODEC_ID_ADPCM_YAMAHA );
    PyModule_AddIntMacro( m, CODEC_ID_ADPCM_SBPRO_4 );
    PyModule_AddIntMacro( m, CODEC_ID_ADPCM_SBPRO_3 );
    PyModule_AddIntMacro( m, CODEC_ID_ADPCM_SBPRO_2 );
    PyModule_AddIntMacro( m, CODEC_ID_ADPCM_THP );
    PyModule_AddIntMacro( m, CODEC_ID_ADPCM_IMA_AMV );
    PyModule_AddIntMacro( m, CODEC_ID_ADPCM_EA_R1 );
    PyModule_AddIntMacro( m, CODEC_ID_ADPCM_EA_R3 );
    PyModule_AddIntMacro( m, CODEC_ID_ADPCM_EA_R2 );
    PyModule_AddIntMacro( m, CODEC_ID_ADPCM_IMA_EA_SEAD );
    PyModule_AddIntMacro( m, CODEC_ID_ADPCM_IMA_EA_EACS );
    PyModule_AddIntMacro( m, CODEC_ID_ADPCM_EA_XAS );
    PyModule_AddIntMacro( m, CODEC_ID_ADPCM_EA_MAXIS_XA );
    PyModule_AddIntMacro( m, CODEC_ID_ADPCM_IMA_ISS );

    PyModule_AddIntMacro( m, CODEC_ID_AMR_NB );
    PyModule_AddIntMacro( m, CODEC_ID_AMR_WB );

    PyModule_AddIntMacro( m, CODEC_ID_RA_144 );
    PyModule_AddIntMacro( m, CODEC_ID_RA_288 );

    PyModule_AddIntMacro( m, CODEC_ID_ROQ_DPCM );
    PyModule_AddIntMacro( m, CODEC_ID_INTERPLAY_DPCM );
    PyModule_AddIntMacro( m, CODEC_ID_XAN_DPCM );
    PyModule_AddIntMacro( m, CODEC_ID_SOL_DPCM );

    PyModule_AddIntMacro( m, CODEC_ID_MP2 );
    PyModule_AddIntMacro( m, CODEC_ID_MP3 );
    PyModule_AddIntMacro( m, CODEC_ID_AAC );
    PyModule_AddIntMacro( m, CODEC_ID_AC3 );
    PyModule_AddIntMacro( m, CODEC_ID_DTS );
    PyModule_AddIntMacro( m, CODEC_ID_VORBIS );
    PyModule_AddIntMacro( m, CODEC_ID_DVAUDIO );
    PyModule_AddIntMacro( m, CODEC_ID_WMAV1 );
    PyModule_AddIntMacro( m, CODEC_ID_WMAV2 );
    PyModule_AddIntMacro( m, CODEC_ID_MACE3 );
    PyModule_AddIntMacro( m, CODEC_ID_MACE6 );
    PyModule_AddIntMacro( m, CODEC_ID_VMDAUDIO );
    PyModule_AddIntMacro( m, CODEC_ID_SONIC );
    PyModule_AddIntMacro( m, CODEC_ID_SONIC_LS );
    PyModule_AddIntMacro( m, CODEC_ID_FLAC );
    PyModule_AddIntMacro( m, CODEC_ID_MP3ADU );
    PyModule_AddIntMacro( m, CODEC_ID_MP3ON4 );
    PyModule_AddIntMacro( m, CODEC_ID_SHORTEN );
    PyModule_AddIntMacro( m, CODEC_ID_ALAC );
    PyModule_AddIntMacro( m, CODEC_ID_WESTWOOD_SND1 );
    PyModule_AddIntMacro( m, CODEC_ID_GSM );
    PyModule_AddIntMacro( m, CODEC_ID_QDM2 );
    PyModule_AddIntMacro( m, CODEC_ID_COOK );
    PyModule_AddIntMacro( m, CODEC_ID_TRUESPEECH );
    PyModule_AddIntMacro( m, CODEC_ID_TTA );
    PyModule_AddIntMacro( m, CODEC_ID_SMACKAUDIO );
    PyModule_AddIntMacro( m, CODEC_ID_QCELP );
    PyModule_AddIntMacro( m, CODEC_ID_WAVPACK );
    PyModule_AddIntMacro( m, CODEC_ID_DSICINAUDIO );
    PyModule_AddIntMacro( m, CODEC_ID_IMC );
    PyModule_AddIntMacro( m, CODEC_ID_MUSEPACK7 );
    PyModule_AddIntMacro( m, CODEC_ID_MLP );
    PyModule_AddIntMacro( m, CODEC_ID_GSM_MS );
    PyModule_AddIntMacro( m, CODEC_ID_ATRAC3 );
    PyModule_AddIntMacro( m, CODEC_ID_VOXWARE );
    PyModule_AddIntMacro( m, CODEC_ID_APE );
    PyModule_AddIntMacro( m, CODEC_ID_NELLYMOSER );
    PyModule_AddIntMacro( m, CODEC_ID_MUSEPACK8 );
    PyModule_AddIntMacro( m, CODEC_ID_SPEEX );
    PyModule_AddIntMacro( m, CODEC_ID_WMAVOICE );
    PyModule_AddIntMacro( m, CODEC_ID_WMAPRO );
    PyModule_AddIntMacro( m, CODEC_ID_WMALOSSLESS );
    PyModule_AddIntMacro( m, CODEC_ID_ATRAC3P );
    PyModule_AddIntMacro( m, CODEC_ID_EAC3 );
    PyModule_AddIntMacro( m, CODEC_ID_SIPR );
    PyModule_AddIntMacro( m, CODEC_ID_MP1 );
    //PyModule_AddIntMacro( m, CODEC_ID_TWINVQ );
    //PyModule_AddIntMacro( m, CODEC_ID_TRUEHD );
    //PyModule_AddIntMacro( m, CODEC_ID_MP4ALS );

    PyModule_AddIntMacro( m, CODEC_ID_DVD_SUBTITLE );
    PyModule_AddIntMacro( m, CODEC_ID_DVB_SUBTITLE );
    PyModule_AddIntMacro( m, CODEC_ID_TEXT );
    PyModule_AddIntMacro( m, CODEC_ID_XSUB );
    PyModule_AddIntMacro( m, CODEC_ID_SSA );
    PyModule_AddIntMacro( m, CODEC_ID_MOV_TEXT );
    //PyModule_AddIntMacro( m, CODEC_ID_HDMV_PGS_SUBTITLE );

    PyModule_AddIntMacro( m, CODEC_ID_TTF );

    PyModule_AddIntMacro( m, CODEC_ID_PROBE );

    return m;
}

