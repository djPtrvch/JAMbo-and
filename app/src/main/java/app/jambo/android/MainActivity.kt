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
        setContent {
            SpikeScreen(
                midi = midi,
                ble = blePeripheral,
                onScanBle = ::requestBleScan,
                onAdvertise = ::setAdvertise,
                onLink = ::setLink,
            )
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
