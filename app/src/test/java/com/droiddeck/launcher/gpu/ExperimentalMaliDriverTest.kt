package com.droiddeck.launcher.gpu

import org.junit.Assert.*
import org.junit.Test

class ExperimentalMaliDriverTest {
    @Test fun restrictsExperimentalDriverToTargetGpu() {
        val mali = GpuInfo("Mali", 0, GpuInfo.Family.NOT_ADRENO, "", false, vulkanVendorId = 0x13b5)
        assertTrue(ExperimentalMaliDriver.eligible(mali, "0xc8700000"))
        assertFalse(ExperimentalMaliDriver.eligible(mali, "0xc8700010"))
        assertFalse(ExperimentalMaliDriver.eligible(mali, null))
        assertFalse(ExperimentalMaliDriver.eligible(mali, "invalid"))
        val adreno = GpuInfo("Adreno", 740, GpuInfo.Family.A7XX, "", false)
        assertFalse(ExperimentalMaliDriver.eligible(adreno, "0xc8700000"))
    }
}
