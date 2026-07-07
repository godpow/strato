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
        return updates.firstOrNull { it.enabled }
    }
}
