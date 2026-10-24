package com.droiddeck.launcher.gpu

import android.content.Context
import java.io.File
import java.security.MessageDigest

object ExperimentalMaliDriver {
    internal fun eligible(gpu: GpuInfo, deviceId: String?): Boolean =
        gpu.vendor == GpuInfo.Vendor.ARM && !gpu.supportsTurnip &&
            deviceId?.removePrefix("0x")?.toLongOrNull(16) == 0xc8700000L

    fun prepare(context: Context, gpu: GpuInfo): File? {
        if (!eligible(gpu, VulkanInfo.query()["device_id"])) return null
        val assets = context.assets
        if (!assets.list("experimental-panvk").orEmpty().contains("SHA256SUMS")) return null
        val expected = assets.open("experimental-panvk/SHA256SUMS").bufferedReader().use {
            it.readLine().substringBefore(' ')
        }
        require(expected.matches(Regex("[a-f0-9]{64}"))) { "Invalid experimental PanVK checksum" }
        val dir = File(context.filesDir, "drivers/panvk-experimental/$expected").apply { mkdirs() }
        val library = File(dir, "libvulkan_panfrost.so")
        val temp = File(dir, "driver.tmp")
        try {
            assets.open("experimental-panvk/libvulkan_panfrost.so").use { input ->
                temp.outputStream().use { input.copyTo(it) }
            }
            val hash = MessageDigest.getInstance("SHA-256")
            temp.inputStream().use { input ->
                val buffer = ByteArray(65536)
                while (true) {
                    val n = input.read(buffer)
                    if (n < 0) break
                    hash.update(buffer, 0, n)
                }
            }
            val actual = hash.digest().joinToString("") { "%02x".format(it) }
            check(actual == expected) { "Experimental PanVK checksum mismatch" }
            check(temp.renameTo(library)) { "Cannot install experimental PanVK" }
        } finally {
            temp.delete()
        }
        return File(dir, "panfrost_icd.aarch64.json").apply {
            writeText("""{"file_format_version":"1.0.0","ICD":{"library_path":"./libvulkan_panfrost.so","api_version":"1.3.0"}}""")
        }
    }
}
