package com.jansir.audiovideodemo

import android.content.Context
import androidx.appcompat.app.AppCompatActivity
import android.os.Bundle
import android.os.Environment
import android.util.Log
import android.view.Surface
import android.view.SurfaceHolder
import android.view.View
import android.widget.TextView
import androidx.core.util.toRange
import com.danikula.videocache.HttpProxyCacheServer
import com.hjq.permissions.Permission
import com.hjq.permissions.XXPermissions
import com.jansir.audiovideodemo.databinding.ActivityMainBinding
import java.security.Permissions
import kotlin.math.roundToInt

class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding
    private var player: Int? = null
    private var path =
        "http://vd3.bdstatic.com/mda-mhh2mxc2p0x8f45v/fhd/cae_h264_nowatermark/1629251858772587431/mda-mhh2mxc2p0x8f45v.mp4"
//    val path = Environment.getExternalStorageDirectory().absolutePath + "/test.mp4"
//    val path = "rtmp://58.200.131.2:1935/livetv/hunantv"

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        XXPermissions.setScopedStorage(true);
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)
//        binding.tv.text = ffmpegInfo()
        XXPermissions.with(this)
            .permission(Permission.Group.STORAGE)
            .request { permissions, all ->
                initSfv()
            }


    }

    private fun initSfv() {
        binding.sfv.holder.addCallback(object : SurfaceHolder.Callback {
            override fun surfaceChanged(
                holder: SurfaceHolder, format: Int,
                width: Int, height: Int
            ) {
            }

            override fun surfaceDestroyed(holder: SurfaceHolder) {}

            override fun surfaceCreated(holder: SurfaceHolder) {
                if (player == null) {
                    val proxy = getProxy(this@MainActivity)
                    val proxyUrl = proxy!!.getProxyUrl(path)
                    player = createPlayer(proxyUrl, holder.surface)
                    play(player!!)
                }
            }
        })
    }

    fun setProgress(pro : Float){
        Log.e("TAG", "setProgress: $pro" )
        binding.progressBarH.setProgress((pro*100).roundToInt())
    }

    private external fun ffmpegInfo(): String

    private external fun createPlayer(path: String, surface: Surface): Int

    private external fun play(player: Int)

    private external fun rePlay(player: Int)

    private external fun seekTo(player: Int,pos:Float)

    private external fun pause(player: Int)

    private external fun goOn(player: Int)


    fun setProgress(){
        Log.e("TAG", "setProgress: " )
    }


    companion object {
        init {
            System.loadLibrary("native-lib")
        }

        private var proxy: HttpProxyCacheServer? = null

        fun getProxy(context: Context): HttpProxyCacheServer? {
            return if (proxy == null) newProxy(context).also { proxy = it } else proxy
        }

        private fun newProxy(context: Context): HttpProxyCacheServer {
            return HttpProxyCacheServer(context.applicationContext)
        }
    }

    fun rePlay(view: View) {
        rePlay(player!!);
    }

    fun pause(view: View) {
        pause(player!!);
    }

    fun goOn(view: View) {
        goOn(player!!)
    }

    fun seekTo(view: View) {
        seekTo(player!!, 0.5F)
    }


}