package com.droiddeck.launcher.gpu

import org.junit.Assert.*
import org.junit.Test

class GpuVendorTest {
    @Test fun maliAndImmortalisAreArmButCannotUseTurnip() {
        for (name in listOf("Mali-G720", "Immortalis-G720 MC12")) {
            val gpu = GpuInfo(name, 0, GpuInfo.Family.NOT_ADRENO, "MT6989", false)
            assertEquals(GpuInfo.Vendor.ARM, gpu.vendor)
            assertFalse(gpu.supportsTurnip)
        }
    }
    @Test fun physicalVendorOverridesStaleAdrenoClassification() {
        val gpu = GpuInfo("unknown", 0, GpuInfo.Family.ADRENO_UNKNOWN, "", false,
            vulkanVendorId = 0x13b5)
        assertEquals(GpuInfo.Vendor.ARM, gpu.vendor)
        assertFalse(gpu.supportsTurnip)
    }

    @Test fun qualcommVendorAloneDoesNotEstablishTurnipCompatibility() {
        val gpu = GpuInfo("unknown", 0, GpuInfo.Family.NOT_ADRENO, "", false,
            vulkanVendorId = 0x5143)
        assertEquals(GpuInfo.Vendor.QUALCOMM, gpu.vendor)
        assertFalse(gpu.supportsTurnip)
    }

    @Test fun unknownAdrenoRetainsTurnipCompatibility() {
        val gpu = GpuInfo("Adreno", 0, GpuInfo.Family.ADRENO_UNKNOWN, "", false)
        assertEquals(GpuInfo.Vendor.QUALCOMM, gpu.vendor)
        assertTrue(gpu.supportsTurnip)
        assertEquals(GpuInfo.Vendor.OTHER, GpuInfo.vendorOf(false, "mt6989"))
    }
}
