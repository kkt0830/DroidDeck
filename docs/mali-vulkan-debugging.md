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

The supplied error sequence matches a concrete fallback bug in Gamescope
3.16.29 (upstream commit `8f212644c46460549035971ab914421180f61a7c`):

1. `CWaylandBackend::GetInstanceExtensions()` returns an empty list. Its native
   Wayland path uses DMA-BUF presentation, not a Vulkan Wayland swapchain.
2. `vulkan_get_instance()` caches the first instance in a function-local static.
3. Wayland `Init()` calls `vulkan_init(instance, VK_NULL_HANDLE)`. Physical-device
   selection fails with the incompatible Turnip ICD.
4. `main.cpp` falls back to `CSDLBackend` when Wayland initialization fails. SDL
   reuses that cached instance, which lacks its required surface extensions.

This explains how the Wayland-surface error can be downstream of device
selection failure; it is not evidence that enabling one extension fixes Mali.
A full device log is still needed to establish the exact backend sequence in the
installed binary. Also, “failed to find physical device” means no *suitable*
device was selected: upstream ignores enumeration return codes and filters by
Vulkan version and queue support, so this line alone does not prove a zero count.

For native Wayland, validate DMA-BUF presentation and synchronization. For SDL
Wayland, independently validate enabled instance extensions and actual surface
creation. Android extension availability proves neither Linux path works.

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
user data and retain the installed app until a compatible signing key or a
separate test application ID is available. A second Android user profile does
not generally bypass package signing-certificate requirements.
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
4. No PC is needed for the first report: **Share logs** includes the raw Android
   Vulkan probe results, board platform, kernel, and app-scoped stat/read/write
   observations for Mali/KGSL/DRM nodes in `device.txt`. Visibility does not prove
   successful device opening, ioctl support, or SELinux permission.

   If a PC is available, capture additional shell-scoped information without root:

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

## Android-to-Linux bridge source audit

The following public implementations were inspected; none was integrated or
represented as a working SM-X820 backend:

* [Vortek client](https://github.com/brunodev85/vortek/tree/b1730c5def9b575672e671aee11d79ae7adc63d1)
  provides a real Unix-socket Vulkan forwarding protocol. In
  `src/vulkan_calls.c`, surface creation is Xlib-only; no
  `vkCreateWaylandSurfaceKHR` entry exists. `vkGetMemoryFdPropertiesKHR` and
  `vkImportSemaphoreFdKHR` unconditionally return
  `VK_ERROR_INVALID_EXTERNAL_HANDLE`. Gamescope's semaphore import path calls
  the latter, so matching function names alone is not interoperability.
* [Termux Vortek server](https://github.com/nostalgia296/vortek-termux/tree/616b508b7712405ec394cf435b6dfe59fc40da94)
  includes an Android system-loader path and Android Hardware Buffer handling,
  but its standalone presentation backend is X11. Integrating it requires a
  host service, a matching Linux client, lifecycle/transport integration, and
  compatible presentation and external-memory/fence implementations. Disabling
  the non-Adreno guard or installing its manifest supplies none of these.

Required next device evidence is the existing APK's **Share logs** archive:
Android physical-device properties/extensions, minimal logical-device result,
and app-visible kernel device nodes. This can be collected on the tablet
without a PC. It narrows host-driver versus kernel-driver implementations; it
is not itself proof that either complete bridge works. No new rendering APK
was built from this documentation-only audit.
