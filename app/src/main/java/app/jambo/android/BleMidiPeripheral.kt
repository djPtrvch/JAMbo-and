package app.jambo.android

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothGattServer
import android.bluetooth.BluetoothGattServerCallback
import android.bluetooth.BluetoothGattService
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.le.AdvertiseCallback
import android.bluetooth.le.AdvertiseData
import android.bluetooth.le.AdvertiseSettings
import android.content.Context
import android.os.Handler
import android.os.Looper
import android.os.ParcelUuid
import androidx.compose.runtime.mutableStateOf
import java.io.ByteArrayOutputStream
import java.util.UUID

/**
 * ADVERTISE: телефон/планшет сам виден как Bluetooth MIDI-устройство (GATT-сервер с сервисом BLE-MIDI).
 * iPad подключается к нему из настроек Bluetooth MIDI своих приложений; входящие ноты играют тон.
 */
@SuppressLint("MissingPermission")
class BleMidiPeripheral(
    private val ctx: Context,
    private val onMidi: (label: String, bytes: ByteArray) -> Unit,
    private val log: (String) -> Unit,
) {
    private val main = Handler(Looper.getMainLooper())
    private val bm = ctx.getSystemService(BluetoothManager::class.java)
    private var gatt: BluetoothGattServer? = null
    private var on = false

    val advertising = mutableStateOf(false)
    val connected = mutableStateOf<String?>(null)

    fun start() {
        val adapter = bm?.adapter
        if (adapter == null || !adapter.isEnabled) { log("Bluetooth is off or unavailable"); return }
        if (!adapter.isMultipleAdvertisementSupported) { log("BLE advertising not supported"); return }
        on = true
        val svc = BluetoothGattService(SERVICE, BluetoothGattService.SERVICE_TYPE_PRIMARY)
        val ch = BluetoothGattCharacteristic(
            CHAR,
            BluetoothGattCharacteristic.PROPERTY_READ or
                BluetoothGattCharacteristic.PROPERTY_WRITE_NO_RESPONSE or
                BluetoothGattCharacteristic.PROPERTY_NOTIFY,
            BluetoothGattCharacteristic.PERMISSION_READ or BluetoothGattCharacteristic.PERMISSION_WRITE,
        )
        ch.addDescriptor(BluetoothGattDescriptor(CCCD,
            BluetoothGattDescriptor.PERMISSION_READ or BluetoothGattDescriptor.PERMISSION_WRITE))
        svc.addCharacteristic(ch)
        gatt = bm.openGattServer(ctx, serverCb)
        gatt?.addService(svc)   // реклама — после onServiceAdded
    }

    fun stop() {
        on = false
        stopAdvertising()
        gatt?.close(); gatt = null
        connected.value = null
    }

    private fun startAdvertising() {
        val adv = bm?.adapter?.bluetoothLeAdvertiser ?: return
        val settings = AdvertiseSettings.Builder()
            .setAdvertiseMode(AdvertiseSettings.ADVERTISE_MODE_LOW_LATENCY)
            .setTxPowerLevel(AdvertiseSettings.ADVERTISE_TX_POWER_HIGH)
            .setConnectable(true)
            .build()
        val data = AdvertiseData.Builder().addServiceUuid(ParcelUuid(SERVICE)).build()
        val scan = AdvertiseData.Builder().setIncludeDeviceName(true).build()
        adv.startAdvertising(settings, data, scan, advCb)
    }

    private fun stopAdvertising() {
        runCatching { bm?.adapter?.bluetoothLeAdvertiser?.stopAdvertising(advCb) }
        advertising.value = false
    }

    private val advCb = object : AdvertiseCallback() {
        override fun onStartSuccess(s: AdvertiseSettings) {
            main.post { advertising.value = true }
            log("advertising as \"${runCatching { bm?.adapter?.name }.getOrNull()}\"")
        }
        override fun onStartFailure(code: Int) {
            main.post { advertising.value = false }
            log("advertising failed: $code")
        }
    }

    private val serverCb = object : BluetoothGattServerCallback() {
        override fun onServiceAdded(status: Int, service: BluetoothGattService) {
            if (status == BluetoothGatt.GATT_SUCCESS && on) startAdvertising()
            else log("GATT service failed: $status")
        }

        override fun onConnectionStateChange(device: BluetoothDevice, status: Int, state: Int) {
            val name = runCatching { device.name }.getOrNull() ?: device.address
            if (state == BluetoothProfile.STATE_CONNECTED) {
                main.post { connected.value = name; stopAdvertising() }
                log("BLE connected: $name")
            } else if (state == BluetoothProfile.STATE_DISCONNECTED) {
                main.post { connected.value = null; if (on) startAdvertising() }
                log("BLE disconnected: $name")
            }
        }

        override fun onCharacteristicReadRequest(d: BluetoothDevice, id: Int, offset: Int, c: BluetoothGattCharacteristic) {
            gatt?.sendResponse(d, id, BluetoothGatt.GATT_SUCCESS, 0, ByteArray(0))
        }

        override fun onCharacteristicWriteRequest(
            d: BluetoothDevice, id: Int, c: BluetoothGattCharacteristic,
            prepared: Boolean, needResponse: Boolean, offset: Int, value: ByteArray?,
        ) {
            if (needResponse) gatt?.sendResponse(d, id, BluetoothGatt.GATT_SUCCESS, 0, null)
            val midi = value?.let { bleToMidi(it) } ?: return
            if (midi.isNotEmpty()) onMidi("BLE·${connected.value ?: "host"}", midi)
        }

        override fun onDescriptorReadRequest(d: BluetoothDevice, id: Int, offset: Int, desc: BluetoothGattDescriptor) {
            gatt?.sendResponse(d, id, BluetoothGatt.GATT_SUCCESS, 0, BluetoothGattDescriptor.DISABLE_NOTIFICATION_VALUE)
        }

        override fun onDescriptorWriteRequest(
            d: BluetoothDevice, id: Int, desc: BluetoothGattDescriptor,
            prepared: Boolean, needResponse: Boolean, offset: Int, value: ByteArray?,
        ) {
            if (needResponse) gatt?.sendResponse(d, id, BluetoothGatt.GATT_SUCCESS, 0, null)
        }
    }

    companion object {
        val SERVICE: UUID = UUID.fromString("03B80E5A-EDE8-4B33-A751-6CE34EC4C700")
        val CHAR: UUID = UUID.fromString("7772E5DB-3868-4112-A1A9-F2669D106BF3")
        val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

        /**
         * Пакет BLE-MIDI → обычный поток MIDI: байт заголовка и байты меток времени убираются.
         * После заголовка и после байта данных/статуса байт со старшим битом — метка времени;
         * сразу за меткой — статус (или данные при running status).
         */
        fun bleToMidi(p: ByteArray): ByteArray {
            if (p.size < 2 || (p[0].toInt() and 0x80) == 0) return ByteArray(0)
            val out = ByteArrayOutputStream(p.size)
            var afterTs = false
            for (i in 1 until p.size) {
                val b = p[i].toInt() and 0xFF
                if (b >= 0x80 && !afterTs) { afterTs = true; continue }   // метка времени
                out.write(b)
                afterTs = false
            }
            return out.toByteArray()
        }
    }
}
