package com.jansir.audiovideodemo

import androidx.appcompat.app.AppCompatActivity
import android.os.Bundle
import android.os.Environment
import android.view.Surface
import android.view.SurfaceHolder
import android.widget.TextView
import com.hjq.permissions.Permission
import com.hjq.permissions.XXPermissions
import com.jansir.audiovideodemo.databinding.ActivityMainBinding
import java.security.Permissions

class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding
    private var player: Int? = null
//    private var path ="https://vd3.bdstatic.com/mda-mhh2mxc2p0x8f45v/fhd/cae_h264_nowatermark/1629251858772587431/mda-mhh2mxc2p0x8f45v.mp4"
    val path = Environment.getExternalStorageDirectory().absolutePath + "/test.mp4"

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)
        binding.tv.text = ffmpegInfo()
        XXPermissions.with(this)
            .permission(Permission.Group.STORAGE)
            .request { permissions, all ->
                initSfv()
            }


    }

    private fun initSfv() {
        binding.sfv.holder.addCallback(object : SurfaceHolder.Callback {
            override fun surfaceChanged(holder: SurfaceHolder, format: Int,
                                        width: Int, height: Int) {}
            override fun surfaceDestroyed(holder: SurfaceHolder) {}

            override fun surfaceCreated(holder: SurfaceHolder) {
                if (player == null) {
                    player = createPlayer(path, holder.surface)
                    play(player!!)
                }
            }
        })
    }

    private external fun ffmpegInfo(): String

    private external fun createPlayer(path: String, surface: Surface): Int

    private external fun play(player: Int)

    private external fun pause(player: Int)

    companion object {
        init {
            System.loadLibrary("native-lib")
        }
    }
}