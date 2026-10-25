package com.droiddeck.launcher.runtime

import java.nio.file.Files
import org.junit.Assert.*
import org.junit.Test

class LinuxVulkanSelectionTest {
    @Test fun maliNeverSelectsFreedrenoOrAnArbitraryManifest() {
        val root = Files.createTempDirectory("icd-selection").toFile()
        try {
            val dir = root.resolve("usr/share/vulkan/icd.d").apply { mkdirs() }
            dir.resolve("freedreno_icd.json").writeText("{}")
            dir.resolve("unrelated.json").writeText("{}")
            assertNull(LinuxRuntime.vulkanIcd(root, false))
            assertEquals("freedreno_icd.json", LinuxRuntime.vulkanIcd(root, true)?.name)
            dir.resolve("freedreno_icd.json").delete()
            assertNull(LinuxRuntime.vulkanIcd(root, true))
        } finally { root.deleteRecursively() }
    }
}
