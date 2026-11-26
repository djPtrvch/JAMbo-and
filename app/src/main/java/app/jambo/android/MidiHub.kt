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
import android.media.midi.MidiInputPort
import android.media.midi.MidiManager
import android.media.midi.MidiOutputPort
import android.media.midi.MidiReceiver
import android.os.Handler
import android.os.Looper
import android.os.ParcelUuid
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import java.util.UUID

data class MidiRow(val id: Int, val key: String, val name: String, val type: String, val inPorts: Int, val outPorts: Int, val open: Boolean)
data class BleRow(val address: String, val name: String)

/** Android MIDI: открытие устройств, разбор входов и отправка клока с метками времени. */
class MidiHub(private val ctx: Context) {
    private val main = Handler(Looper.getMainLooper())
    private val mm: MidiManager? = ctx.getSystemService(MidiManager::class.java)
    private data class Connection(
        val device: MidiDevice, val key: String,
        val inputs: List<Pair<MidiEndpoint, MidiOutputPort>>,
        val outputs: List<Pair<MidiEndpoint, MidiInputPort>>,
    ) {
        fun close() {
            inputs.forEach { runCatching { it.second.close() } }
            outputs.forEach { runCatching { it.second.close() } }
            runCatching { device.close() }
        }
    }
    private val opened = HashMap<Int, Connection>()
    private var started = false
    private var generation = 0
    val controls = MidiControls(ctx)
    val sync = MidiSync(ctx, this)
    val inputs = mutableStateListOf<MidiEndpoint>()
    val outputs = mutableStateListOf<MidiEndpoint>()
    private val opening = HashSet<Int>()   // уже открываются — не открывать второй раз

    val devices = mutableStateListOf<MidiRow>()
    val ble = mutableStateListOf<BleRow>()
    val log = mutableStateListOf<String>()
    val scanning = mutableStateOf(false)
    val clockTicks = mutableStateOf(0L)
    private val bleDevices = HashMap<String, BluetoothDevice>()
    private val parsers = HashMap<String, MidiReceiver>()

    /** Байты MIDI не из MidiManager (ADVERTISE): тот же разбор, лог и звук. */
    fun feed(label: String, bytes: ByteArray) {
        main.post {
            if (!started) return@post
            val endpoint = MidiEndpoint(label, 0, label)
            controls.rememberDevice(endpoint.device)
            if (inputs.none { it.key == endpoint.key }) inputs.add(endpoint)
            parsers.getOrPut(label) { Parser(endpoint) }.send(bytes, 0, bytes.size)
        }
    }

    fun log(line: String) = addLog(line)

    val available get() = mm != null

    private val cb = object : MidiManager.DeviceCallback() {
        override fun onDeviceAdded(info: MidiDeviceInfo) { open(info); refresh() }
        override fun onDeviceRemoved(info: MidiDeviceInfo) {
            val removed = opened.remove(info.id)
            removed?.close()
            if (removed != null) {
                controls.releaseDevice(removed.key)
                sync.disconnected(removed.key)
            }
            refresh()
            addLog("removed: ${name(info)}")
        }
    }

    fun start() {
        if (started) return
        started = true
        sync.start()
        val m = mm ?: return
        m.registerDeviceCallback(cb, main)
        @Suppress("DEPRECATION")
        m.devices.forEach { open(it) }
        refresh()
    }

    fun stop() {
        if (!started) return
        started = false
        generation++
        sync.stop()
        controls.releaseDevice()
        mm?.unregisterDeviceCallback(cb)
        opened.values.forEach { it.close() }
        opened.clear()
        opening.clear()
        parsers.clear()
        refresh()
    }

    private fun refresh() {
        @Suppress("DEPRECATION")
        val listed = mm?.devices?.toList().orEmpty()
        val all = (listed + opened.values.map { it.device.info }).distinctBy { it.id }
        devices.clear()
        all.forEach {
            devices += MidiRow(it.id, deviceKey(it), name(it), typeName(it.type), it.inputPortCount, it.outputPortCount, opened.containsKey(it.id))
        }
        inputs.clear()
        outputs.clear()
        for (c in opened.values.sortedBy { it.key }) {
            inputs.addAll(c.inputs.map { it.first })
            outputs.addAll(c.outputs.map { it.first })
        }
        inputs.addAll(parsers.keys.map { MidiEndpoint(it, 0, it) })
    }

    private fun deviceKey(i: MidiDeviceInfo) = "${typeName(i.type)} · ${name(i)}"

    internal fun sendClock(batch: LongArray, selected: Set<String>, skipDevice: String?) {
        val flush = batch[0] != 0L
        for (c in opened.values) for ((endpoint, port) in c.outputs) {
            if (!flush && (c.key == skipDevice || selected.isNotEmpty() && endpoint.key !in selected)) continue
            runCatching {
                if (flush) port.flush()
                if (c.key != skipDevice && (selected.isEmpty() || endpoint.key in selected)) {
                    // Один байт на сообщение, метка времени — CLOCK_MONOTONIC в наносекундах.
                    val msg = ByteArray(1)
                    for (i in 1 until batch.lastIndex step 2) {
                        msg[0] = batch[i].toByte()
                        port.send(msg, 0, 1, batch[i + 1])
                    }
                }
            }
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
        if (!started || info.id in opened || !opening.add(info.id)) return
        val ticket = generation
        m.openDevice(info, { dev ->
            if (!started || ticket != generation) dev?.close()
            else {
                opening -= info.id
                if (dev != null) attach(dev) else addLog("open failed: ${name(info)}")
            }
        }, main)
    }

    private fun attach(dev: MidiDevice) {
        val info = dev.info
        if (info.id in opened) { if (opened[info.id]?.device !== dev) dev.close(); return }
        val key = deviceKey(info)
        controls.rememberDevice(key)
        val inputs = (0 until info.outputPortCount).mapNotNull { index ->
            val endpoint = endpoint(info, key, MidiDeviceInfo.PortInfo.TYPE_OUTPUT, index)
            dev.openOutputPort(index)?.also { it.connect(Parser(endpoint)) }?.let { endpoint to it }
        }
        val outputs = (0 until info.inputPortCount).mapNotNull { index ->
            val endpoint = endpoint(info, key, MidiDeviceInfo.PortInfo.TYPE_INPUT, index)
            dev.openInputPort(index)?.let { endpoint to it }
        }
        opened[info.id] = Connection(dev, key, inputs, outputs)
        addLog("opened: ${name(info)}")
        refresh()
    }

    private fun endpoint(info: MidiDeviceInfo, key: String, type: Int, index: Int): MidiEndpoint {
        val portName = info.ports.firstOrNull { it.type == type && it.portNumber == index }?.name.orEmpty()
        return MidiEndpoint(key, index, if (portName.isEmpty()) "${name(info)} · ${index + 1}" else "${name(info)} · $portName")
    }

    private fun addLog(line: String) {
        main.post {
            log.add(0, line)
            while (log.size > 200) log.removeAt(log.size - 1)
        }
    }

    /** Разбор потока байт: running status, SysEx, реальное время. */
    private inner class Parser(val endpoint: MidiEndpoint) : MidiReceiver() {
        private val label = endpoint.label
        private var at = 0L
        private var status = 0
        private val data = IntArray(2)
        private var n = 0
        private var sysex = false
        private val sx = IntArray(12)
        private var sxLen = 0

        @Synchronized
        override fun onSend(msg: ByteArray, offset: Int, count: Int, timestamp: Long) {
            at = if (timestamp > 0L) timestamp else System.nanoTime()
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
            if (b == 0xF8 || b in 0xFA..0xFC) {
                val time = at
                main.post {
                    if (started) {
                        sync.realtime(endpoint, b, time)
                        if (b == 0xF8) clockTicks.value++
                    }
                }
            }
            when (b) {
                0xF8 -> Unit
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
            if (s and 0xF0 in listOf(0x80, 0x90, 0xB0)) main.post {
                if (started) controls.message(endpoint, s, d1, d2)
            }
            val text = when (s and 0xF0) {
                0x90 -> if (d2 > 0) "NoteOn ch$ch ${noteName(d1)} v$d2" else "NoteOff ch$ch ${noteName(d1)}"
                0x80 -> "NoteOff ch$ch ${noteName(d1)}"
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
        val ticket = generation
        m.openBluetoothDevice(d, { dev ->
            if (!started || ticket != generation) dev?.close()
            else if (dev == null) addLog("BLE connect failed")
            else attach(dev)
        }, main)
    }
}

private val NOTE_NAMES = arrayOf("C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B")
fun noteName(n: Int) = NOTE_NAMES[n % 12] + (n / 12 - 1)   // 60 = C4, 48 = C3 — как в JAMbo
