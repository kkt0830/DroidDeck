package com.droiddeck.launcher.gpu

import android.content.Context
import java.io.File
import java.security.MessageDigest

object ExperimentalMaliDriver {
    internal fun eligible(gpu: GpuInfo, deviceId: String?): Boolean =
        gpu.vendor == GpuInfo.Vendor.ARM && !gpu.supportsTurnip &&
            deviceId?.removePrefix("0x")?.toLongOrNull(16) == 0xc8700000L

    fun bundledFor(context: Context, gpu: GpuInfo): Boolean =
        eligible(gpu, VulkanInfo.cachedOrNull()?.get("device_id")) &&
            context.assets.list("experimental-panvk").orEmpty().contains("SHA256SUMS")

    fun prepare(context: Context, gpu: GpuInfo): File? {
        if (!eligible(gpu, VulkanInfo.query()["device_id"])) return null
        val assets = context.assets
        if (!assets.list("experimental-panvk").orEmpty().contains("SHA256SUMS")) return null
        val sums = assets.open("experimental-panvk/SHA256SUMS").bufferedReader().use { it.readLines() }
            .associate { line -> line.trim().split(Regex("\\s+"), limit = 2).let { it[1] to it[0] } }
        val expected = requireNotNull(sums["libvulkan_panfrost.so"])
        require(expected.matches(Regex("[a-f0-9]{64}"))) { "Invalid experimental PanVK checksum" }
        val dir = File(context.filesDir, "drivers/panvk-experimental/$expected").apply { mkdirs() }
        for (name in listOf("libvulkan_panfrost.so", "panvk-probe")) {
            val temp = File(dir, "$name.tmp")
            try {
                val hash = MessageDigest.getInstance("SHA-256")
                assets.open("experimental-panvk/$name").use { input ->
                    temp.outputStream().use { output ->
                        val buffer = ByteArray(65536)
                        while (true) {
                            val n = input.read(buffer)
                            if (n < 0) break
                            hash.update(buffer, 0, n)
                            output.write(buffer, 0, n)
                        }
                    }
                }
                val actual = hash.digest().joinToString("") { "%02x".format(it) }
                check(actual == sums[name]) { "Experimental PanVK checksum mismatch: $name" }
                check(temp.renameTo(File(dir, name))) { "Cannot install experimental PanVK: $name" }
                if (name == "panvk-probe") check(File(dir, name).setExecutable(true, true))
            } finally {
                temp.delete()
            }
        }
        return File(dir, "panfrost_icd.aarch64.json").apply {
            writeText("""{"file_format_version":"1.0.0","ICD":{"library_path":"./libvulkan_panfrost.so","api_version":"1.3.0"}}""")
        }
    }
}
