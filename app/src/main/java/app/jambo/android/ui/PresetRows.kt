package app.jambo.android.ui

import androidx.compose.foundation.gestures.detectDragGesturesAfterLongPress
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.unit.dp
import androidx.compose.ui.zIndex
import kotlin.math.roundToInt

/** Список пресетов EQ и мастера. Палец отслеживается в неподвижной строке, сдвигается только её содержимое. */
@Composable
internal fun PresetRows(
    names: List<String>,
    load: (Int) -> Unit,
    rename: (Int) -> Unit,
    delete: (Int) -> Unit,
    move: (Int, Int) -> Unit,
) {
    val onMove by rememberUpdatedState(move)
    var open by remember(names) { mutableIntStateOf(-1) }
    var dragging by remember(names) { mutableIntStateOf(-1) }
    var destination by remember(names) { mutableIntStateOf(-1) }
    var dy by remember(names) { mutableFloatStateOf(0f) }
    var rowPitch by remember(names) { mutableFloatStateOf(0f) }
    val gap = 4.dp

    fun clearDrag() {
        dragging = -1
        destination = -1
        dy = 0f
    }

    Column(Modifier.verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(gap)) {
        names.forEachIndexed { index, name ->
            key(index, name) {
                Box(
                    Modifier.zIndex(if (dragging == index) 1f else 0f).pointerInput(names, index) {
                        detectDragGesturesAfterLongPress(
                            onDragStart = {
                                rowPitch = (size.height + gap.roundToPx()).toFloat().coerceAtLeast(1f)
                                dragging = index
                                destination = index
                                dy = 0f
                                open = -1
                            },
                            onDragEnd = {
                                val to = destination
                                clearDrag()
                                if (to in names.indices && to != index) onMove(index, to)
                            },
                            onDragCancel = { clearDrag() },
                        ) { change, delta ->
                            change.consume()
                            dy += delta.y
                            destination = (index + (dy / rowPitch).roundToInt()).coerceIn(names.indices)
                        }
                    },
                ) {
                    Box(Modifier.graphicsLayer {
                        translationY = when {
                            dragging == index -> dy
                            dragging >= 0 && index > dragging && index <= destination -> -rowPitch
                            dragging >= 0 && index < dragging && index >= destination -> rowPitch
                            else -> 0f
                        }
                        shadowElevation = if (dragging == index) 8.dp.toPx() else 0f
                    }) {
                        SwipeRow(name, null, null, null, false, open == index,
                            { open = if (it) index else -1 }, { load(index) }, { rename(index) }, { delete(index) })
                    }
                }
            }
        }
    }
}
