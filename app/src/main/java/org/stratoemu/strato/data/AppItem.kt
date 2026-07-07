/*
 * SPDX-License-Identifier: MPL-2.0
 * Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)
 */

package org.stratoemu.strato.data

import org.stratoemu.strato.loader.AppEntry
import java.io.Serializable

@Suppress("SERIAL")
class AppItem(meta : AppEntry, private val updates : List<BaseAppItem>, private val dlcs : List<BaseAppItem>) : BaseAppItem(meta), Serializable {

    /**
     * The version of the enabled update when one is attached, falling back to the base game version
     */
    override val version : String? get() = getEnabledUpdate()?.version ?: super.version

    fun getEnabledDlcs() : List<BaseAppItem> {
        return dlcs.filter { it.enabled }
    }

    fun getEnabledUpdate() : BaseAppItem? {
        return updates.filter { it.enabled }.maxWithOrNull { x, y -> compareVersions(x.version ?: "", y.version ?: "") }
    }

    /**
     * Compares two display versions numerically per dot-separated segment so that 1.10.0 orders above 1.9.0
     */
    private fun compareVersions(first : String, second : String) : Int {
        val firstParts = first.split('.')
        val secondParts = second.split('.')
        for (i in 0 until maxOf(firstParts.size, secondParts.size)) {
            val firstNumber = firstParts.getOrNull(i)?.filter { it.isDigit() }?.toIntOrNull() ?: 0
            val secondNumber = secondParts.getOrNull(i)?.filter { it.isDigit() }?.toIntOrNull() ?: 0
            if (firstNumber != secondNumber)
                return firstNumber.compareTo(secondNumber)
        }
        return 0
    }
}
