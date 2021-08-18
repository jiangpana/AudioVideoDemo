#include <jni.h>
#include <string>
#include <unistd.h>
#include "media/player/def_player/player.h"
#include "media/player/gl_player/gl_player.h"
#include "media/muxer/ff_repack.h"
#include "media/synthesizer/synthesizer.h"

extern "C"
{
#include<libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavfilter/avfilter.h>
#include <libavcodec/jni.h>


JNIEXPORT jstring JNICALL
Java_com_jansir_audiovideodemo_MainActivity_ffmpegInfo(JNIEnv *env, jobject thiz) {
    char info[40000] = {0};
    AVCodec *c_temp = av_codec_next(NULL);
    while (c_temp != NULL) {
        if (c_temp->decode != NULL) {
            sprintf(info, "%sdecode:", info);
        } else {
            sprintf(info, "%sencode:", info);
        }
        switch (c_temp->type) {
            case AVMEDIA_TYPE_VIDEO:
                sprintf(info, "%s(video):", info);
                break;
            case AVMEDIA_TYPE_AUDIO:
                sprintf(info, "%s(audio):", info);
                break;
            default:
                sprintf(info, "%s(other):", info);
                break;
        }
        sprintf(info, "%s[%s]\n", info, c_temp->name);
        c_temp = c_temp->next;
    }

    return env->NewStringUTF(info);
}


JNIEXPORT jint JNICALL
Java_com_jansir_audiovideodemo_MainActivity_createPlayer(JNIEnv *env, jobject thiz, jstring path,
                                                         jobject surface) {
    Player *player = new Player(env, path, surface);
    return (jint) player;

}


JNIEXPORT void JNICALL
Java_com_jansir_audiovideodemo_MainActivity_play(JNIEnv *env, jobject thiz, jint player) {

    Player *p = (Player *) player;
    p->play();
}


JNIEXPORT void JNICALL
Java_com_jansir_audiovideodemo_MainActivity_pause(JNIEnv *env, jobject thiz, jint player) {
    Player *p = (Player *) player;
    p->pause();

}

}