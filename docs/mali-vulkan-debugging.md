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

## SM-X820 evidence and experimental Kbase path (2026-10-24)

The supplied `DroidDeck-2026-10-24-03-steam-3.zip` confirms ARM vendor `0x13b5`,
GPU `0xc8700000`, system Vulkan 1.3.247, successful Android instance and minimal
logical-device creation, and one returned physical device. `/dev/mali0` is
visible with read/write access checks passing; KGSL and renderD128 are absent.
This does not prove that Kbase ioctls or DMA-BUF export are permitted.
The observed status -1 is the application's `LINUX_VULKAN_UNSUPPORTED` guard;
Gamescope did not run in that session.

A newly identified candidate is zenithblue-oss/panvk-kbase-android, pinned in
`tools/panvk/build.sh`. Unlike upstream DRM-only PanVK, it implements a Kbase
kernel backend and includes a glibc build path. Its universal g615-v11-csf patch
series includes G720/v12; the separate g720 profile is incomplete. Upstream
reports tests on the same GPU ID in a Tab S10 Ultra, but those are third-party
claims, not verification on this SM-X820 or of DroidDeck/Gamescope.

`Build experimental PanVK Kbase` builds this driver with X11 and Wayland WSI
on an ARM64 GitHub runner. A successful run can be explicitly supplied to
`Build APK` using `experimental_panvk_run`. Ordinary APK builds contain no
experimental driver. The artifact carries source revisions, licenses, ELF
metadata, and checksums; the APK verifies checksums again when extracting it.
Only ARM device ID 0xc8700000 selects this experimental path. Adreno keeps its
existing driver path. No Android vendor HAL is substituted for a glibc ICD.

The experimental APK bypasses the earlier guard only when the actual bundled
Kbase library is present and verified. Both Vulkan loader environment variables
and the runtime's driver override point to that library's ICD. The existing
`/dev` bind exposes the real `/dev/mali0`; it is not renamed to a DRM node.

Before starting Gamescope, `panvk-probe` runs inside the Linux session with the
same ICD and Wayland environment. It records instance extensions, instance
creation, physical-device properties, minimal graphics/compute logical-device
creation, and actual Wayland surface creation/presentation-support query when
available. A 30-second timeout bounds hangs. A successful minimal probe does
not validate all Gamescope features, DMA-BUF presentation, or Steam UI. The
native Gamescope Wayland backend can use DMA-BUF without a Vulkan surface;
surface failure is therefore recorded separately rather than treated as proof
that all presentation paths are broken.

Device procedure: install the specifically labeled experimental APK, start one
Steam session, then Share logs. Inspect `panvk_probe.*`, `graphics.experimental_icd`,
loader errors, Gamescope backend messages, and whether the Steam login screen
actually appears. Preserve existing app data if Android reports a signing
certificate mismatch. Do not install a normal diagnostic APK expecting this
experimental driver to be included.

Experimental presentation uses Gamescope's SDL backend with Wayland and
`MESA_VK_WSI_DEBUG=sw` (shared-memory presentation, not a CPU Vulkan ICD).
This avoids the cached-instance native-Wayland-to-SDL fallback and does not
require the Android vendor Vulkan driver to import Linux DMA-BUFs. The Android
compositor now enables DMA-BUF extensions only as a supported group, preserving
its shared-memory upload path. Adreno with all extensions retains the existing
DMA-BUF path. GPU composition, Gamescope extension requirements and actual
shared-memory swapchain presentation remain device validation gates.

## SM-X820 follow-up: DRM render-node check

`DroidDeck-2026-10-25-04-steam.zip` confirms the experimental Linux driver
loads and accesses Kbase CSF uAPI 1.21. The Linux probe reports success (0)
for instance creation, physical-device enumeration (one Mali device), minimal
logical-device creation, Wayland surface creation and surface-support query;
queue presentation support is true. Gamescope selects that same Mali device,
then fails with `physical device has no render node` before backend creation.
Steam UI is still unverified.

The bundled driver advertises `VK_EXT_physical_device_drm` even on its Kbase
path, where neither DRM node exists. Gamescope 3.16.29 treats that advertisement
as requiring a render node even for SDL swapchain presentation. Local patch
`0002-kbase-no-drm-extension.patch` disables only this optional extension on
Kbase; DRM devices retain it. This uses Gamescope's existing path for drivers
without DRM device properties, without inventing a node or changing the Mali
kernel interface. DRM format-modifier and external-memory extensions are not
removed. Successful rebuild does not establish successful Gamescope startup;
the next device log must show progress beyond the render-node check.

## SM-X820 follow-up: Steam WebHelper black screen

`DroidDeck-2026-10-25-08-steam.zip` uses the corrected bd02913f driver.
Gamescope creates its nested swapchain, starts Xwayland, and the app records
`frame.first` and `session.ready`. These events do not prove a visible login
screen. WebHelper records a fatal X IO error on display :0, restarts, and Steam
subsequently exits with status 0. The causal source of the X connection failure
is not established; the empty Android crash buffer does not rule out guest faults.

The next experimental build selects `-cef-disable-gpu` only for the Steam browser
UI in a bundled Mali session. This tests a software-rendered login UI while
retaining PanVK for Gamescope and preserving the Vulkan driver environment.
Adreno and ordinary builds retain their previous CEF selection. This is a
compatibility workaround to validate on the device, not proof of a complete fix.

## SM-X820 follow-up: software CEF restart loop

The `2026-10-25-09-steam` session lasts about 10.6 minutes without `frame.first`.
The software CEF option is present in both session and WebHelper command logs.
WebHelper repeatedly reaches BrowserReady, then Steam restarts it roughly every
10 seconds (`startcount=62`). UI transport connects transiently, so the transport
errors alone do not establish a network failure. This disproves the usefulness
of the software-UI workaround for the supplied run; the restart cause remains
unconfirmed.

The next test replaces that workaround with CEF ANGLE/OpenGL over Zink/PanVK,
explicit X11, and no client Gamescope WSI layer in experimental Mali sessions.
The separate CEF GPU process is retained by omitting the preload's default
`--in-process-gpu`; `--no-zygote` and the existing audio feature workaround remain.
This avoids both direct CEF ANGLE/Vulkan and the all-software UI path. Gamescope
itself starts before these client-only settings, using the existing SDL/Wayland
PanVK path. Client applications in that experimental session inherit the disabled
Gamescope WSI layer and use the ordinary ICD WSI instead. Ordinary/Adreno launch
options are unchanged. This remains a compatibility experiment, requiring an
actual visible login screen and stable WebHelper on SM-X820.

## SM-X820 follow-up: standard login UI experiment

The `2026-10-25-01-steam` session confirms ANGLE/OpenGL selection and first-frame
notification, but the UI window disappears and Steam shuts down about 17 seconds
after launch. WebHelper transport reaches connected before the restart, so
transient transport errors are not sufficient evidence of a broken network.
The visible login screen remains unverified. The GL route alone did not fix it.

The next test uses standard Steam desktop UI (`-nobigpicture`) for experimental
Mali instead of `-gamepadui -steamdeck -steamos3`, still nested inside Gamescope.
This avoids the failing Deck login flow without claiming its root cause is known.
ANGLE/OpenGL and the corrected PanVK remain. The external mangoapp overlay is
also disabled only for experimental Mali: the device logs confirm repeated
`ghc::filesystem::filesystem_error` from `/sys/class/thermal/` permission denial.
That overlay failure is separate from the unproven cause of WebHelper restarts.
Ordinary sessions retain their original UI choice and overlay behavior.

## SM-X820 follow-up: force graphics options at WebHelper exec

`2026-10-25-03-steam` confirms the standard login UI and successful Vulkan
preflight, but no first-frame notification. Steam requests DesktopLoginWindow,
then WebHelper restarts approximately every ten seconds. The session ends via
an explicit clean-exit request after about 138 seconds. This is not evidence
of a new Vulkan initialization failure or of a spontaneous client crash.

Although the Steam launch command includes CEF ANGLE/OpenGL switches, the
recorded WebHelper GPU child command contains no explicit use-gl/use-angle
switches. This does not prove which implementation Chromium selected internally,
but shows that the intended graphics route was not validated at the exec boundary.
The experimental Mali preload argument specification now supplies ANGLE/OpenGL,
X11 and ignore-gpu-blocklist directly when launching the WebHelper browser,
in addition to Steam's CEF request. GPU child arguments remain unmodified so
Chromium can choose its own subprocess arguments.

A scoped graphics trace records only a fixed allowlist of backend switches for
browser/GPU-process exec, without dumping other arguments. This makes the next
log distinguish the new launch route from an ignored Steam CEF request. The
change does not alter the Kbase kernel backend or claim hardware rendering is
fixed. Actual visible login and session stability remain device tests.
