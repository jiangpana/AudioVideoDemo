//
// 音视频解码基类
// Author: Chen Xiaoping
// Create Date: 2019-08-02
//

#include "base_decoder.h"
#include "../../utils/timer.c"
#include "../../utils/logger.h"


BaseDecoder::BaseDecoder(JNIEnv *env, jstring path, bool for_synthesizer)
        : m_for_synthesizer(for_synthesizer) {
    Init(env, path);
    CreateDecodeThread();
}

void BaseDecoder::Init(JNIEnv *env, jstring path) {
    m_path_ref = env->NewGlobalRef(path);
    m_path = env->GetStringUTFChars(path, NULL);
    //获取JVM虚拟机，为创建线程作准备
    env->GetJavaVM(&m_jvm_for_thread);
}

BaseDecoder::~BaseDecoder() {
    if (m_format_ctx != NULL) delete m_format_ctx;
    if (m_codec_ctx != NULL) delete m_codec_ctx;
    if (m_frame != NULL) delete m_frame;
    if (m_packet != NULL) delete m_packet;
}

void BaseDecoder::CreateDecodeThread() {
    // 使用智能指针，线程结束时，自动删除本类指针
    std::shared_ptr<BaseDecoder> that(this);
    std::thread t(Decode, that);
    //detach作用是将子线程和主线程的关联分离，也就是说detach()后子线程在后台独立继续运行，
    // 主线程无法再取得子线程的控制权，即使主线程结束，子线程未执行也不会结束。
    // 当主线程结束时，由运行时库负责清理与子线程相关的资源。
    t.detach();
}

void BaseDecoder::Decode(std::shared_ptr<BaseDecoder> that) {
    JNIEnv *env;

    //将线程附加到虚拟机，并获取env
    if (that->m_jvm_for_thread->AttachCurrentThread(&env, NULL) != JNI_OK) {
        LOG_ERROR(that->TAG, that->LogSpec(), "Fail to Init decode thread");
        return;
    }

    that->CallbackState(PREPARE);

    that->InitFFMpegDecoder(env);
    that->AllocFrameBuffer();
    av_usleep(1000);
    that->Prepare(env);
    that->LoopDecode(env);
    that->DoneDecode(env);

    that->CallbackState(STOP);

    //解除线程和jvm关联
    that->m_jvm_for_thread->DetachCurrentThread();

}

void BaseDecoder::InitFFMpegDecoder(JNIEnv *env) {
    //1，初始化上下文
    m_format_ctx = avformat_alloc_context();
    LOG_ERROR(TAG, LogSpec(), "m_path =  %s", m_path);
    //2，打开文件
    if (avformat_open_input(&m_format_ctx, m_path, NULL, NULL) != 0) {
        LOG_ERROR(TAG, LogSpec(), "Fail to open file [%s]", m_path);
        DoneDecode(env);
        return;
    }

    //3，获取音视频流信息
    if (avformat_find_stream_info(m_format_ctx, NULL) < 0) {
        LOG_ERROR(TAG, LogSpec(), "Fail to find stream info");
        DoneDecode(env);
        return;
    }

    //4，查找编解码器
    //4.1 获取视频流的索引
    int vIdx = -1;//存放视频流的索引
    for (int i = 0; i < m_format_ctx->nb_streams; ++i) {
        if (m_format_ctx->streams[i]->codecpar->codec_type == GetMediaType()) {
            vIdx = i;
            break;
        }
    }
    if (vIdx == -1) {
        LOG_ERROR(TAG, LogSpec(), "Fail to find stream index")
        DoneDecode(env);
        return;
    }
    m_stream_index = vIdx;

    //4.2 获取解码器参数
    AVCodecParameters *codecPar = m_format_ctx->streams[vIdx]->codecpar;

    //4.3 获取解码器
//    m_codec = avcodec_find_decoder_by_name("h264_mediacodec");//硬解码
    m_codec = avcodec_find_decoder(codecPar->codec_id);

    //4.4 获取解码器上下文
    m_codec_ctx = avcodec_alloc_context3(m_codec);
    if (avcodec_parameters_to_context(m_codec_ctx, codecPar) != 0) {
        LOG_ERROR(TAG, LogSpec(), "Fail to obtain av codec context");
        DoneDecode(env);
        return;
    }

    //5，打开解码器
    if (avcodec_open2(m_codec_ctx, m_codec, NULL) < 0) {
        LOG_ERROR(TAG, LogSpec(), "Fail to open av codec");
        DoneDecode(env);
        return;
    }

    m_duration = (long) ((float) m_format_ctx->duration / AV_TIME_BASE * 1000);
    LOGE(TAG, "总时间 m_duration = %s", std::to_string(m_duration).c_str());

    LOG_INFO(TAG, LogSpec(), "Decoder init success")
}

void BaseDecoder::AllocFrameBuffer() {
    // 初始化待解码和解码数据结构
    // 1）初始化AVPacket，存放解码前的数据
    m_packet = av_packet_alloc();
    // 2）初始化AVFrame，存放解码后的数据
    m_frame = av_frame_alloc();
}

void BaseDecoder::LoopDecode(JNIEnv *env) {
    if (STOP == m_state) { // 如果已被外部改变状态，维持外部配置
        m_state = START;
    }

    CallbackState(START);

    LOG_INFO(TAG, LogSpec(), "Start loop decode")
    while (1) {
        if (m_state != DECODING &&
            m_state != START &&
            m_state != STOP) {
            CallbackState(m_state);
            Wait();
            CallbackState(m_state);
            // 恢复同步起始时间，去除等待流失的时间
            m_started_t = GetCurMsTime() - m_cur_t_s;
        }

        if (m_state == STOP) {
            break;
        }

        if (-1 == m_started_t) {
            m_started_t = GetCurMsTime();
        }

        if (DecodeOneFrame(env) != NULL) {
            SyncRender();
            Render(m_frame);

            if (m_state == START) {
                m_state = PAUSE;
            }
        } else {
            LOG_INFO(TAG, LogSpec(), "m_state = %d", m_state)
            if (ForSynthesizer()) {
                m_state = STOP;
            } else {
                m_state = FINISH;
            }
            CallbackState(FINISH);
        }
    }
}

/**
 * 解码一帧
 *  av_read_frame 方法 : 从 m_format_ctx 中读取一帧解封好的待解码数据，存放在 m_packet 中
 *  avcodec_send_packet 方法 : 将 m_packet 发送到解码器中解码，解码好的数据存放在 m_codec_ctx 中
 *  avcodec_receive_frame 方法 : 接收一帧解码好的数据，存放在 m_frame
 *  av_packet_unref 方法 : 释放内存，否则会导致内存泄漏。
 * @return
 */
AVFrame *BaseDecoder::DecodeOneFrame(JNIEnv *env) {
    int ret = av_read_frame(m_format_ctx, m_packet);
    while (ret == 0) {
        if (m_packet->stream_index == m_stream_index) {
            switch (avcodec_send_packet(m_codec_ctx, m_packet)) {
                case AVERROR_EOF: {
                    av_packet_unref(m_packet);
                    LOG_ERROR(TAG, LogSpec(), "Decode error: %s", av_err2str(AVERROR_EOF));
                    return NULL; //解码结束
                }
                case AVERROR(EAGAIN):
                    LOG_ERROR(TAG, LogSpec(), "Decode error: %s", av_err2str(AVERROR(EAGAIN)));
                    break;
                case AVERROR(EINVAL):
                    LOG_ERROR(TAG, LogSpec(), "Decode error: %s", av_err2str(AVERROR(EINVAL)));
                    break;
                case AVERROR(ENOMEM):
                    LOG_ERROR(TAG, LogSpec(), "Decode error: %s", av_err2str(AVERROR(ENOMEM)));
                    break;
                default:
                    break;
            }
            //TODO 这里需要考虑一个packet有可能包含多个frame的情况
            int result = avcodec_receive_frame(m_codec_ctx, m_frame);
            if (result == 0) {
                ObtainTimeStamp(env);
                av_packet_unref(m_packet);
                return m_frame;
            } else {
                LOG_INFO(TAG, LogSpec(), "Receive frame error result: %s",
                         av_err2str(AVERROR(result)))
            }
        }
        // 释放packet
        av_packet_unref(m_packet);
        ret = av_read_frame(m_format_ctx, m_packet);
    }
    av_packet_unref(m_packet);
    LOGI(TAG, "ret = %s", av_err2str(AVERROR(ret)))
    return NULL;
}


void BaseDecoder::CallbackState(DecodeState status) {
    if (m_state_cb != NULL) {
        switch (status) {
            case PREPARE:
                m_state_cb->DecodePrepare(this);
                break;
            case START:
                m_state_cb->DecodeReady(this);
                break;
            case DECODING:
                m_state_cb->DecodeRunning(this);
                break;
            case PAUSE:
                m_state_cb->DecodePause(this);
                break;
            case FINISH:
                m_state_cb->DecodeFinish(this);
                break;
            case STOP:
                m_state_cb->DecodeStop(this);
                break;
        }
    }
}

void BaseDecoder::ObtainTimeStamp(JNIEnv *env) {
    if (m_frame->pkt_dts != AV_NOPTS_VALUE) {
        m_cur_t_s = m_packet->dts;
        LOGE(TAG, "当前时间m_packet->dts %s", std::to_string(m_packet->dts).c_str());
    } else if (m_frame->pts != AV_NOPTS_VALUE) {
        LOGE(TAG, "当前时间 m_frame->pts %s", std::to_string(m_frame->pts).c_str());
        m_cur_t_s = m_frame->pts;
    } else {
        m_cur_t_s = 0;
    }
    m_cur_t_s = (int64_t) ((m_cur_t_s * av_q2d(m_format_ctx->streams[m_stream_index]->time_base)) *
                           1000);
    if(progress_CallBack){
        //m_duration =30232
        progress_CallBack((float)m_cur_t_s/(float)m_duration,env);
    }

    LOGE(TAG, "当前时间 m_cur_t_s = %s", std::to_string(m_cur_t_s).c_str());


}

void BaseDecoder::SyncRender() {
    if (ForSynthesizer()) {
//        av_usleep(15000);
        return;
    }
    int64_t ct = GetCurMsTime();
    int64_t passTime = ct - m_started_t;
    if (m_cur_t_s > passTime) {
        av_usleep((unsigned int) ((m_cur_t_s - passTime) * 1000));
    }
}

void BaseDecoder::DoneDecode(JNIEnv *env) {
    LOG_INFO(TAG, LogSpec(), "Decode done and decoder release")
    // 释放缓存
    if (m_packet != NULL) {
        av_packet_free(&m_packet);
    }
    if (m_frame != NULL) {
        av_frame_free(&m_frame);
    }
    // 关闭解码器
    if (m_codec_ctx != NULL) {
        avcodec_close(m_codec_ctx);
        avcodec_free_context(&m_codec_ctx);
    }
    // 关闭输入流
    if (m_format_ctx != NULL) {
        avformat_close_input(&m_format_ctx);
        avformat_free_context(m_format_ctx);
    }
    // 释放转换参数
    if (m_path_ref != NULL && m_path != NULL) {
        env->ReleaseStringUTFChars((jstring) m_path_ref, m_path);
        env->DeleteGlobalRef(m_path_ref);
    }

    // 通知子类释放资源
    Release();
}

void BaseDecoder::Wait(long second, long ms) {
//    LOG_INFO(TAG, LogSpec(), "Decoder run into wait, state：%s", GetStateStr())
    pthread_mutex_lock(&m_mutex);
    if (second > 0 || ms > 0) {
        timeval now;
        timespec outtime;
        gettimeofday(&now, NULL);
        int64_t destNSec = now.tv_usec * 1000 + ms * 1000000;
        outtime.tv_sec = static_cast<__kernel_time_t>(now.tv_sec + second + destNSec / 1000000000);
        outtime.tv_nsec = static_cast<long>(destNSec % 1000000000);
        pthread_cond_timedwait(&m_cond, &m_mutex, &outtime);
    } else {
        pthread_cond_wait(&m_cond, &m_mutex);
    }
    pthread_mutex_unlock(&m_mutex);
}

void BaseDecoder::SendSignal() {
//    LOG_INFO(TAG, LogSpec(), "Decoder wake up, state: %s", GetStateStr())
    pthread_mutex_lock(&m_mutex);
    pthread_cond_signal(&m_cond);
    pthread_mutex_unlock(&m_mutex);
}

void BaseDecoder::GoOn() {
    m_state = DECODING;
    SendSignal();
}

void BaseDecoder::Pause() {
    m_state = PAUSE;
}

void BaseDecoder::Stop() {
    m_state = STOP;
    SendSignal();
}

bool BaseDecoder::IsRunning() {
    return DECODING == m_state;
}

long BaseDecoder::GetDuration() {
    return m_duration;
}

long BaseDecoder::GetCurPos() {
    return (long) m_cur_t_s;
}

void BaseDecoder::SeekTo(float progress) {
    Pause();
    LOGE(TAG, "SeekTo progress =%f", progress);
    m_cur_t_s = (int64_t) progress * m_duration;
    //AVSEEK_FLAG_BACKWARD 的作用是seek到请求的timestamp之前最近的关键帧
    int64_t timestamp = (int64_t) (progress * m_duration / 1000)/av_q2d(time_base());
    av_seek_frame(m_format_ctx, m_stream_index, timestamp, AVSEEK_FLAG_BACKWARD);
    GoOn();
}

void BaseDecoder::SetProgressCallBack(Progress_CallBack callBack) {
    progress_CallBack = callBack;
}
