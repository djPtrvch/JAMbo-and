package app.jambo.android

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.media.midi.MidiDevice
import android.media.midi.MidiDeviceInfo
import android.media.midi.MidiManager
import android.media.midi.MidiReceiver
import android.os.Handler
import android.os.Looper
import android.os.ParcelUuid
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import java.util.UUID

data class MidiRow(val id: Int, val name: String, val type: String, val inPorts: Int, val outPorts: Int, val open: Boolean)
data class BleRow(val address: String, val name: String)

/** android.media.midi: устройства USB / Bluetooth / виртуальные; входящие ноты играют тон. */
class MidiHub(private val ctx: Context) {
    private val main = Handler(Looper.getMainLooper())
    private val mm: MidiManager? = ctx.getSystemService(MidiManager::class.java)
    private val opened = HashMap<Int, MidiDevice>()
    private val opening = HashSet<Int>()   // уже открываются — не открывать второй раз

    val devices = mutableStateListOf<MidiRow>()
    val ble = mutableStateListOf<BleRow>()
    val log = mutableStateListOf<String>()
    val scanning = mutableStateOf(false)
    val clockTicks = mutableStateOf(0L)
    private val bleDevices = HashMap<String, BluetoothDevice>()

    val available get() = mm != null

    private val cb = object : MidiManager.DeviceCallback() {
        override fun onDeviceAdded(info: MidiDeviceInfo) { open(info); refresh() }
        override fun onDeviceRemoved(info: MidiDeviceInfo) {
            opened.remove(info.id)?.close(); refresh(); addLog("removed: ${name(info)}")
        }
    }

    fun start() {
        val m = mm ?: return
        m.registerDeviceCallback(cb, main)
        @Suppress("DEPRECATION")
        m.devices.forEach { open(it) }
        refresh()
    }

    fun stop() {
        mm?.unregisterDeviceCallback(cb)
        opened.values.forEach { it.close() }
        opened.clear()
    }

    private fun refresh() {
        val m = mm ?: return
        devices.clear()
        @Suppress("DEPRECATION")
        m.devices.forEach {
            devices += MidiRow(it.id, name(it), typeName(it.type), it.inputPortCount, it.outputPortCount, opened.containsKey(it.id))
        }
    }

    private fun name(i: MidiDeviceInfo): String =
        i.properties.getString(MidiDeviceInfo.PROPERTY_NAME)
            ?: i.properties.getString(MidiDeviceInfo.PROPERTY_PRODUCT) ?: "MIDI ${i.id}"

    private fun typeName(t: Int) = when (t) {
        MidiDeviceInfo.TYPE_USB -> "USB"
        MidiDeviceInfo.TYPE_BLUETOOTH -> "BLE"
        MidiDeviceInfo.TYPE_VIRTUAL -> "VIRTUAL"
        else -> "?"
    }

    private fun open(info: MidiDeviceInfo) {
        val m = mm ?: return
        if (opened.containsKey(info.id) || info.id in opening || info.outputPortCount == 0) return
        opening += info.id
        m.openDevice(info, { dev ->
            if (dev != null) attach(dev, name(info)) else { opening -= info.id; addLog("open failed: ${name(info)}") }
        }, main)
    }

    /** Порты «выхода» устройства — то, что оно шлёт нам. */
    private fun attach(dev: MidiDevice, label: String) {
        opening -= dev.info.id
        if (opened.containsKey(dev.info.id)) { dev.close(); return }
        opened[dev.info.id] = dev
        for (p in 0 until dev.info.outputPortCount) {
            dev.openOutputPort(p)?.connect(Parser(label))
        }
        addLog("opened: $label")
        refresh()
    }

    private fun addLog(line: String) {
        main.post {
            log.add(0, line)
            while (log.size > 200) log.removeAt(log.size - 1)
        }
    }

    /** Разбор потока байт: running status, SysEx, реальное время. */
    private inner class Parser(val label: String) : MidiReceiver() {
        private var status = 0
        private val data = IntArray(2)
        private var n = 0
        private var sysex = false
        private val sx = IntArray(12)
        private var sxLen = 0

        override fun onSend(msg: ByteArray, offset: Int, count: Int, timestamp: Long) {
            for (k in offset until offset + count) byteIn(msg[k].toInt() and 0xFF)
        }

        private fun byteIn(b: Int) {
            if (b >= 0xF8) { realtime(b); return }
            if (sysex) {
                if (b < 0x80) { if (sxLen < sx.size) sx[sxLen] = b; sxLen++; return }
                sysex = false
                logSysex()
                if (b == 0xF7) return
            }
            if (b >= 0x80) {
                if (b == 0xF0) { sysex = true; sxLen = 0; return }
                status = b; n = 0
                if (len(b) == 0) message(b, 0, 0)
                return
            }
            if (status == 0) return
            data[n++] = b
            if (n >= len(status)) {
                message(status, data[0], if (n > 1) data[1] else 0)
                n = 0
                if (status >= 0xF0) status = 0
            }
        }

        /** SysEx: первые байты и длина; MIDI-CI (F0 7E .. 0D) — подписано. */
        private fun logSysex() {
            val shown = (0 until minOf(sxLen, sx.size)).joinToString(" ") { "%02X".format(sx[it]) }
            val ci = sxLen >= 3 && sx[0] == 0x7E && sx[2] == 0x0D
            addLog("$label  F0 $shown${if (sxLen > sx.size) " …" else ""}  SysEx ${sxLen + 2} B${if (ci) " (MIDI-CI)" else ""}")
        }

        private fun len(s: Int) = when (s and 0xF0) {
            0xC0, 0xD0 -> 1
            0xF0 -> when (s) { 0xF1, 0xF3 -> 1; 0xF2 -> 2; else -> 0 }
            else -> 2
        }

        private fun realtime(b: Int) {
            when (b) {
                0xF8 -> main.post { clockTicks.value++ }
                0xFE -> {}
                0xFA -> addLog("$label  Start")
                0xFB -> addLog("$label  Continue")
                0xFC -> addLog("$label  Stop")
                else -> addLog("$label  %02X".format(b))
            }
        }

        private fun message(s: Int, d1: Int, d2: Int) {
            val ch = (s and 0x0F) + 1
            val hex = "%02X %02X %02X".format(s, d1, d2)
            val text = when (s and 0xF0) {
                0x90 -> if (d2 > 0) { Native.noteOn(d1, d2); "NoteOn ch$ch ${noteName(d1)} v$d2" }
                        else { Native.noteOff(d1); "NoteOff ch$ch ${noteName(d1)}" }
                0x80 -> { Native.noteOff(d1); "NoteOff ch$ch ${noteName(d1)}" }
                0xB0 -> "CC ch$ch #$d1 = $d2"
                0xC0 -> "Program ch$ch $d1"
                0xE0 -> "PitchBend ch$ch ${(d2 shl 7 or d1) - 8192}"
                0xD0 -> "Pressure ch$ch $d1"
                0xA0 -> "PolyAT ch$ch ${noteName(d1)} $d2"
                else -> "System"
            }
            addLog("$label  $hex  $text")
        }
    }

    // --- Bluetooth MIDI: поиск по сервису BLE-MIDI и открытие через MidiManager ---

    private val bleUuid = ParcelUuid(UUID.fromString("03B80E5A-EDE8-4B33-A751-6CE34EC4C700"))
    private val scanCb = object : ScanCallback() {
        @SuppressLint("MissingPermission")
        override fun onScanResult(type: Int, r: ScanResult) {
            val d = r.device
            if (bleDevices.put(d.address, d) == null) {
                ble += BleRow(d.address, runCatching { d.name }.getOrNull() ?: d.address)
            }
        }
    }

    @SuppressLint("MissingPermission")
    fun scanBle() {
        val scanner = ctx.getSystemService(BluetoothManager::class.java)?.adapter?.bluetoothLeScanner
        if (scanner == null) { addLog("Bluetooth is off or unavailable"); return }
        ble.clear(); bleDevices.clear()
        scanning.value = true
        val filter = ScanFilter.Builder().setServiceUuid(bleUuid).build()
        val settings = ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build()
        scanner.startScan(listOf(filter), settings, scanCb)
        main.postDelayed({ scanner.stopScan(scanCb); scanning.value = false }, 10_000)
    }

    fun connectBle(address: String) {
        val d = bleDevices[address] ?: return
        val m = mm ?: return
        addLog("connecting: ${d.address}")
        m.openBluetoothDevice(d, { dev ->
            if (dev == null) addLog("BLE connect failed")
            else if (!opened.containsKey(dev.info.id)) attach(dev, dev.info.properties.getString(MidiDeviceInfo.PROPERTY_NAME) ?: d.address)
        }, main)
    }
}

private val NOTE_NAMES = arrayOf("C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B")
fun noteName(n: Int) = NOTE_NAMES[n % 12] + (n / 12 - 2)   // 60 = C3, как в JAMbo
