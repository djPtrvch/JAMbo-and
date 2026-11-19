package app.jambo.android

import android.Manifest
import android.content.Context
import android.net.wifi.WifiManager
import android.os.Build
import android.os.Bundle
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.displayCutout
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.material3.Text
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import app.jambo.android.ui.MainScreen

class MainActivity : ComponentActivity() {
    private lateinit var midi: MidiHub
    private lateinit var blePeripheral: BleMidiPeripheral
    private var multicast: WifiManager.MulticastLock? = null

    private val blePermissions = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { granted -> if (granted.values.all { it }) midi.scanBle() }

    private val advertisePermissions = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { granted -> if (granted.values.all { it }) blePeripheral.start() else midi.log("Bluetooth permission denied") }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        midi = MidiHub(this)
        blePeripheral = BleMidiPeripheral(this, midi::feed, midi::log)
        // Во весь экран: системные панели скрыты, появляются свайпом от края.
        WindowCompat.setDecorFitsSystemWindows(window, false)
        WindowInsetsControllerCompat(window, window.decorView).apply {
            hide(WindowInsetsCompat.Type.systemBars())
            systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        }
        setContent {
            // Бутерброд — пока экран спайка (MIDI-устройства, Bluetooth, Link, звук); меню JAMbo — позже.
            var spike by remember { mutableStateOf(false) }
            Box(Modifier.fillMaxSize().background(Color.Black).windowInsetsPadding(WindowInsets.displayCutout)) {
                if (spike) {
                    SpikeScreen(
                        midi = midi,
                        ble = blePeripheral,
                        onScanBle = ::requestBleScan,
                        onAdvertise = ::setAdvertise,
                        onLink = ::setLink,
                    )
                    Text("BACK", color = Color.White, fontSize = 14.sp, fontWeight = FontWeight.Bold,
                        modifier = Modifier.align(Alignment.TopEnd).padding(16.dp).clickable { spike = false }
                            .padding(12.dp))
                } else {
                    MainScreen(onMenu = { spike = true })
                }
            }
        }
    }

    override fun onStart() {
        super.onStart()
        Native.start()
        midi.start()
    }

    override fun onStop() {
        midi.stop()
        Native.stop()
        super.onStop()
    }

    override fun onDestroy() {
        blePeripheral.stop()
        super.onDestroy()
    }

    private fun setAdvertise(on: Boolean) {
        if (!on) { blePeripheral.stop(); return }
        if (Build.VERSION.SDK_INT >= 31)
            advertisePermissions.launch(arrayOf(Manifest.permission.BLUETOOTH_ADVERTISE, Manifest.permission.BLUETOOTH_CONNECT))
        else blePeripheral.start()
    }

    private fun requestBleScan() {
        val perms = if (Build.VERSION.SDK_INT >= 31)
            arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT)
        else arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
        blePermissions.launch(perms)
    }

    /** Link ищет участников мультикастом: без MulticastLock Wi-Fi может отбрасывать пакеты. */
    private fun setLink(on: Boolean) {
        if (on && multicast == null) {
            val wifi = applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
            multicast = wifi.createMulticastLock("jambo-link").apply { setReferenceCounted(false); acquire() }
        } else if (!on) {
            multicast?.release(); multicast = null
        }
        Native.linkEnable(on)
    }
}
