# Galaxy Tab S10+ / MT6989 Vulkan investigation

This change fixes incorrect driver selection and adds diagnostics. It does **not**
implement a Mali Linux Vulkan driver or demonstrate a Steam login screen.

## Confirmed source-level causes

* `TurnipDriver.chooseByDevice` previously reduced every GPU to an Adreno model
  number. Zero (including non-Adreno) selected `turnip-sdk36` on API 36.
* `LinuxRuntime.vulkanIcd` unconditionally preferred a freedreno manifest and
  otherwise picked an arbitrary manifest. Runtime r9's Turnip driver needs
  Qualcomm KGSL; it cannot enumerate the Immortalis-G720 through Samsung's Mali
  device interface.
* Display driver and Linux guest ICD are separate ABIs and WSI paths. Selecting
  Android system Vulkan fixes the display choice, not the Linux renderer.

Non-Adreno sessions now report `LINUX_VULKAN_UNSUPPORTED` before launching a
known incompatible guest backend. Imported Linux Turnip and pinned Android
AdrenoTools selections cannot bypass that compatibility check. Unknown Adreno
models keep their existing SDK fallback, including Quest's patched defaults.

The reported missing physical device prevents Gamescope backend creation. The
Wayland extension error is a separate, unverified WSI requirement: its chronology
alone cannot prove it is just a downstream symptom. Extension enumeration,
instance enabled-extension lists and a real surface creation must be checked in
**the Linux process** after a usable ICD exists. Android extension availability
does not establish Linux Wayland WSI support.

## Candidate paths and missing prerequisites

| Path | What it requires | Status for this device |
| --- | --- | --- |
| Android system Vulkan | Android loader, vendor Bionic HAL, Android surface | Used for app display; added instance/device probes, device execution pending |
| Directly load Android HAL in Linux | A glibc/Bionic bridge plus WSI and buffer/fence interoperability | No such implementation is present; not a valid ICD substitution |
| Venus | A running virtio Vulkan renderer/transport backed by a host GPU, external memory and synchronization, client WSI | No host renderer or transport provided by r9; selecting Venus alone cannot work |
| PanVK | A supported Mali kernel UAPI/device and compatible Mesa driver, WSI and buffer sharing | No verified access to a compatible DRM/panthor device on this Samsung stock kernel; GPU architecture name alone is insufficient |
| Software Vulkan | An actual CPU ICD such as Lavapipe, matching dependencies and Wayland WSI; Gamescope feature support | Not selected or bundled by this patch; performance and Gamescope compatibility unverified |

Do not rename `freedreno_icd.json`, pass the Android `libvulkan.so` as a Linux
ICD, or claim acceleration from the presence of a JSON file. A Mali bridge is a
separate substantial implementation requiring real-device kernel and Vulkan
capability evidence.

## Install and collect evidence

Use the APK from the fork's **Build APK** workflow artifact after CI succeeds.
The standard workflow signs with the repository test key. If an existing release
uses a different signing certificate, Android will reject an update; preserve
user data and use a separate device/profile or an explicitly backed-up install.
Do not uninstall the user's app automatically.

1. Install using `adb install -r path/to/app-release.apk`, then fully stop and
   reopen DroidDeck so the compositor reloads the selected display driver.
2. Open device information to populate the Android Vulkan report. Start one
   Steam session, then use **Share logs**. Keep `device.txt`, `app.log`,
   `session.log`, `events.jsonl` and the APK commit/build label together.
3. Expected on this patch: display Auto resolves to `system`, GPU name comes
   from the system Vulkan physical device (e.g. Immortalis-G720), and the Linux
   session reports `LINUX_VULKAN_UNSUPPORTED`, not a misleading `GUEST_EXIT`.
   This is a deliberate stop, **not** a successful Gamescope or Steam test.
4. Capture kernel/device information without root:

   ```sh
   adb shell getprop ro.board.platform
   adb shell getprop ro.hardware
   adb shell ls -l /dev/mali0 /dev/dri /dev/kgsl-3d0
   adb logcat -d -s VulkanInfo TurnipDriver LinuxVulkanDriver SessionService
   ```

   Missing paths and permission denials are useful evidence. Do not change
   SELinux enforcement or device permissions to force a test through.

## Independent validation gates

| Gate | Evidence to require | Current outcome |
| --- | --- | --- |
| Mali recognition | System physical-device name/vendor; vendor policy unit tests | Policy tests added; SM-X820 pending |
| ICD loading | Actual loader/driver load result in the relevant process | Android `icd_loader`; Linux has no compatible backend |
| Instance | `instance_create_result=0` | Android probe added; device pending |
| Physical device | `physical_device_result=0` (or `VK_INCOMPLETE`) and a returned device | Android probe added; Linux original failure confirmed by supplied log |
| Logical device | `logical_device_result=0` using a graphics queue | Android probe added; device pending; no Gamescope feature claim |
| Wayland surface | Linux `VK_KHR_wayland_surface` availability **and enabled at instance creation**, actual `vkCreateWaylandSurfaceKHR` success | Not tested; Android report explicitly says surface not tested |
| Gamescope backend | Successful Vulkan backend creation and presentation | Not tested / blocked by missing compatible Linux backend |
| Steam UI | Visible and interactive login screen, frame evidence and session-ready event | Not tested / blocked |

The Android probe uses a minimal logical device; it does not enable or validate
all features Gamescope requires. `android_surface_available` and
`wayland_surface_available` are availability checks, not surface creation.

After implementing a real Linux backend, first run `vulkaninfo --summary` and
`vulkaninfo` under the **same ICD, library path and proot environment as
Gamescope**, preserving exit codes and loader diagnostics (`VK_LOADER_DEBUG=all`).
Then test Wayland instance/surface creation against DroidDeck's active compositor,
backend startup, and finally Steam login, in that order. Do not remove the guard
until that backend is explicitly implemented and selected.
