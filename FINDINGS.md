# Dishonored 1 borderless: findings and method

A record of what was actually measured about the game, why every off-the-shelf
borderless tool fails against it, what specifically defeats each obstacle, and
the non-obvious failure modes that cost time along the way.

Written after the working build. Companion to [`plan.md`](plan.md) (the design)
and [`README.md`](README.md) (how to use it).

---

## 1. Summary

Dishonored resists borderless windowing for two independent reasons, and both
have to be dealt with or you get the flicker/black-screen behaviour the
community reports:

1. **It takes exclusive fullscreen.** The display mode changes, so alt-tab has
   to tear the mode down and rebuild it — the multi-second display flailing.
   External window tools cannot touch this at all; it is a Direct3D device
   property, decided before any window styling matters.
2. **It re-asserts its own window style.** Anything that strips borders from
   outside gets undone from inside the process, which is the flicker.

The working approach fixes (1) at the D3D9 device layer and (2) at the window
*message* layer — intercepting the game's style and position changes before they
take effect, rather than correcting them afterwards. Both live in one 32-bit
`d3d9.dll` proxy that Windows loads instead of the system one.

---

## 2. What the game actually is (measured, not assumed)

`plan.md` left the renderer as an open question ("Dishonored is D3D9, but confirm
on your install"). Parsing the PE headers of
`Binaries\Win32\Dishonored.exe` settles it:

| Property | Value | Why it matters |
| --- | --- | --- |
| PE machine | `0x14C` (i386) | **The proxy must be 32-bit.** A 64-bit DLL is not loadable by this process, and fails with no error message. |
| Optional header magic | `0x10B` (PE32) | Confirms the above. |
| Imports from `d3d9.dll` | `Direct3DCreate9`, `D3DPERF_BeginEvent`, `D3DPERF_EndEvent`, `D3DPERF_SetOptions` | D3D9 confirmed. Only four symbols must be forwarded for the game to link. |
| Other imports | `DINPUT8`, `XINPUT1_3` (ordinals 2, 3), `MSVCR90`/`MSVCP90`, `binkw32`, `steam_api`, `WSOCK32`, … | DirectInput8 + XInput for input — relevant to the untested raw-mouse question. |

Two consequences follow directly:

- **A proxy DLL works.** `d3d9.dll` is a static import and is *not* a KnownDLL,
  so Windows searches the application directory first. Dropping a `d3d9.dll`
  into `Binaries\Win32` gets it loaded ahead of `System32`, with no injector, no
  launcher, and no patching of the executable — which also sidesteps the
  legal/ethical concerns `plan.md` raised about Option D (binary patching).
- **`DllMain` runs before the game's `main`.** That is early enough to change
  process-wide state — critically, DPI awareness — before the game creates its
  window.

---

## 3. Why the generic tools fail

The standard external recipe is: find the window → strip `WS_CAPTION` /
`WS_THICKFRAME` → resize to the desktop. Against Dishonored, each step has a
problem:

**Step 3 is undone.** The engine re-applies its own style and geometry (on focus
change, on mode change). An external tool can only re-apply after the fact, so
the two fight — that is the reported endless flicker.

**Step 1–3 do not address exclusive fullscreen at all.** Even a perfectly
borderless window is irrelevant if the D3D device owns the display mode. This is
the part that makes alt-tab expensive, and no window-manipulation tool can reach
it.

### What actually defeats it

Three things, in order of how much they matter:

**(a) Force `Windowed = TRUE` at `CreateDevice` and `Reset`.**
`D3DPRESENT_PARAMETERS.Windowed` is the whole exclusive-fullscreen decision. Flip
it in the proxy and the game renders into a normal window while believing it
asked for fullscreen. This alone removes D3D9's own auto-minimise-on-focus-loss
and the display mode change. Hooking `Reset` as well as `CreateDevice` is not
optional — `Reset` is the path the in-game video options take, and an unhooked
`Reset` would put the game straight back into exclusive fullscreen mid-session.

Alongside the flag, three parameters must be sanitised or the call fails:

- `FullScreen_RefreshRateInHz` must be `0` in windowed mode.
- `PresentationInterval` must be `DEFAULT`, `ONE` or `IMMEDIATE`; `TWO`/`THREE`/
  `FOUR` are fullscreen-only and are rejected.
- A `BackBufferFormat` chosen for a fullscreen mode may not be presentable
  windowed; `D3DFMT_UNKNOWN` means "match the desktop" and is always safe.

**(b) Intercept style changes instead of correcting them.**
`WM_STYLECHANGING` is delivered *before* a `SetWindowLong(GWL_STYLE)` takes
effect, and the handler can rewrite `STYLESTRUCT.styleNew` in place. Same for
`WM_WINDOWPOSCHANGING` and `SetWindowPos`/`MoveWindow`. So the game's attempt to
restore its frame never becomes visible in the first place — there is nothing to
flicker, because there is no wrong state to correct. This is the difference
between subclassing from inside the process and polling from outside it.

**(c) Return `0` from `WM_NCCALCSIZE`.**
This declares the client area to be the entire window rect, which removes the
non-client frame *regardless of the style bits*. It is the robustness backstop:
even if the game managed to re-add `WS_CAPTION` faster than it could be
stripped, there would still be no visible border. Worth having precisely because
the failure mode it guards against is the one everyone reports.

Observed on the test window: style `0x14CF0000` → `0x94000000`.

```
0x14CF0000 = WS_VISIBLE | WS_CLIPSIBLINGS | WS_CAPTION | WS_SYSMENU
           | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX
0x94000000 = WS_POPUP | WS_VISIBLE | WS_CLIPSIBLINGS
```

---

## 4. The non-obvious obstacles

These are the ones that produce no error message, or an error that points
somewhere else entirely. Each cost real time or would have.

### 4.1 DPI awareness — silently produces a blurry window

On a 3840×2160 display at 175% scaling, a **DPI-unaware process is told the
desktop is 2194×1234**. It sizes its window to that, and the compositor upscales
the result. The game looks soft, the window looks right, and nothing anywhere
reports a problem.

Exclusive fullscreen bypasses scaling entirely, so this problem *only appears
once you fix the borderless part* — it would have looked like the mod made the
image worse.

The fix is `SetProcessDpiAwarenessContext(PER_MONITOR_AWARE_V2)` in `DllMain`,
before the game creates its window, falling back to `PER_MONITOR_AWARE`,
`SYSTEM_AWARE`, then legacy `SetProcessDPIAware()`.

**Verify it, do not assume it.** If the exe's manifest already declares a DPI
awareness, every one of those calls is a no-op that returns failure. The proxy
logs `IsProcessDPIAware=` explicitly for this reason. If it ever reports `no`,
the fallback is the per-exe compatibility flag: *Properties → Compatibility →
Change high DPI settings → Override high DPI scaling behaviour → Application*.

### 4.2 `__stdcall` export decoration — game refuses to start, no error

`Direct3DCreate9` and the `D3DPERF_*` functions are `__stdcall`. A 32-bit
mingw/lld link will happily emit them as `Direct3DCreate9@4` or
`_Direct3DCreate9`. The game imports the plain name, the import resolves to
nothing, and the process dies during loading — no dialog, no log, nothing.

Fixed by an explicit `.def` file with undecorated names plus `-Wl,--kill-at`,
and then **asserted** by re-reading the built DLL's export table
(`tools/Check-Pe.ps1`, run on every build).

### 4.3 Non-system runtime imports — same silent failure

llvm-mingw will link `libc++.dll`, `libunwind.dll` or `libwinpthread-1.dll`
unless told not to. Any of them missing from the game directory produces exactly
the same symptom as 4.2: the process will not start and says nothing.

Avoided by using no C++ standard library in the DLL at all (Win32 APIs and fixed
buffers instead), `-fno-exceptions -fno-rtti`, and `-static`. The build asserts
that the import table contains only system DLLs — `KERNEL32`, `USER32` and the
`api-ms-win-crt-*` UCRT forwarders that ship with Windows.

### 4.4 `DllMain` runs under the loader lock

`LoadLibrary` from `DllMain` is a deadlock risk. So:

- The real `d3d9.dll` is loaded **lazily**, on the first export call, never in
  `DllMain`.
- DPI awareness uses `GetModuleHandle("user32")` + `GetProcAddress` — user32 is
  already loaded by the time a statically imported DLL gets
  `DLL_PROCESS_ATTACH`, so nothing new is loaded.
- The watchdog thread starts at `CreateDevice`, not in `DllMain`.
- The real DLL is found via `GetSystemDirectoryA()`, which returns SysWOW64
  directly for a 32-bit process rather than depending on filesystem redirection.

### 4.5 `D3DERR_DEVICELOST` (`0x88760868`) at `CreateDevice`

Cost the most time during testing, and points nowhere near its cause: **another
process already owns the display in exclusive fullscreen.** D3D9 will not create
a second device until it lets go.

In this case it was Dishonored itself, running in another window while the test
suite was being developed. A plain unproxied D3D9 app failed identically, which
is what proved it environmental rather than a bug in the proxy — a useful
bisection when a graphics call fails for no visible reason. Both the proxy log
and the test harness now name this cause explicitly.

### 4.6 Re-entrancy in the enforcement path

Applying the style calls `SetWindowLong` and `SetWindowPos`, which
*synchronously* re-enter the same window procedure via `WM_STYLECHANGING` and
`WM_WINDOWPOSCHANGING`. Without a guard this recurses until the stack runs out.
An interlocked flag around the enforcement function means the re-entrant calls
adjust the message in flight instead.

### 4.7 A 64-bit toolchain cannot build this

mingw-w64 x86_64 packages generally ship no 32-bit runtime libraries, so `-m32`
compiles but fails at link with a wall of "skipping incompatible" messages.
`llvm-mingw` (`scoop install mingw-mstorsjo-llvm-ucrt`) hosts x86_64 and targets
i686 in one install, and does not shadow an existing `g++` shim.

---

## 5. Design decisions worth recording

**Vtable patching instead of COM wrapper objects.** `plan.md` 4.2.1 described
returning a wrapped `IDirect3D9`. The implementation instead patches the two
vtable slots it needs and hands the game genuine interface pointers.
`IDirect3DDevice9` has ~119 methods; hand-forwarding 117 of them that need no
modification is 117 chances to get a signature subtly wrong for no benefit. It
also keeps the Steam overlay working, since it hooks the same vtable and sees an
ordinary D3D9 object.

The trade-off, stated plainly: the slot indices (`CreateDevice` and `Reset` are
both 16, fixed by COM's ABI and the declaration order in `d3d9.h`) are
load-bearing constants. A wrong index would swap some unrelated function pointer
and crash rather than report anything. Their correctness is asserted only by the
test suite passing.

**Retry with the game's original parameters on failure.** If a rewritten
`CreateDevice` or `Reset` fails, the proxy restores the parameters the game
asked for and retries. Worst case the mod does nothing; it should never be the
reason the game will not start.

**No structured exception handling.** `plan.md` 4.3.3 suggested wrapping the
window procedure in `__try`/`__except`. mingw/gcc does not support SEH on i686
and clang's support there is incomplete, so the procedure is instead written to
be total: every pointer out of a message is null-checked, every window handle is
`IsWindow`-checked, and anything unrecognised goes straight to the original
procedure.

**Contingencies that turned out to be worth building** (`plan.md` 4.6): a
watchdog thread that re-applies geometry and *re-subclasses if something else
replaces the window procedure*, and detection of window recreation via
`EnumWindows` filtered by process id. Neither fired during testing, but both are
cheap and cover the failure modes that would otherwise need a code change.

---

## 6. How it was validated

Deliberately, none of this depended on launching the game — that was left as the
final confirmation rather than the development loop.

**Static, on every build** (`tools/Check-Pe.ps1`): PE machine is `x86`; exports
are undecorated; imports are system-only. These are the three failure modes from
§4.2–4.3 that produce no diagnostic at all, so they are asserted rather than
eyeballed.

**Runtime, against a stand-in** (`tests/Verify-Borderless.ps1`): a small D3D9 app
built to resemble the game in the ways that matter — 32-bit, static
`Direct3DCreate9` import, a conventional decorated window, a device requested as
exclusive fullscreen at 1280×720, and no DPI manifest. The proxy goes in its
directory, and a **separate process** then asserts style bits, window rect,
client rect, and backbuffer size. Mid-run the app calls `Reset` asking to return
to exclusive fullscreen, and the geometry is re-checked at exit, so a proxy that
wins the first exchange and loses a later one still fails.

Two scenarios, 26 assertions:

| | Result |
| --- | --- |
| defaults, primary display | window `0,0 3840x2160`, client == window, backbuffer == client |
| `TargetMonitorIndex=2`, `1600x900` | window `4000,873 1600x900`, centred on the secondary — the only run that exercises monitor selection and centring against a non-zero monitor origin |

**What the automated tests do not prove.** They assert style and geometry, not
that pixels reached the screen — the harness starts the app with a hidden-window
startup flag, so `WS_VISIBLE` state is not part of what is checked. They also do
not reproduce an engine that fights back. Confirmation of alt-tab behaviour and
the in-game video options came from the real game.

**Confirmed in the game:** borderless at native resolution, and alt-tab without
the multi-second display mode churn — the game takes focus loss normally and
opens its pause menu.

**Known remaining behaviour: the Windows key does nothing while the game has
focus.** Almost certainly not a windowing issue — Dishonored imports
`DINPUT8.dll`, and a DirectInput8 keyboard acquired with `DISCL_EXCLUSIVE` has
the Windows logo key suppressed by DirectInput itself, by design and
independently of windowed versus fullscreen. Mitigating it would mean
intercepting `IDirectInputDevice8::SetCooperativeLevel` to drop the exclusive
flag, which is a separate piece of work with its own risk to input handling. It
is untested whether this behaved any differently before the mod.

Still unverified: input latency under load, raw mouse capture behaviour, and
resolutions other than the ones tested.

---

## 7. Map

| Path | |
| --- | --- |
| `src/d3d9_proxy.cpp` | Exports, lazy load of the real `d3d9.dll` |
| `src/hooks_d3d9.cpp` | Vtable patching, `D3DPRESENT_PARAMETERS` rewriting |
| `src/window.cpp` | Subclass, message handling, enforcement, watchdog |
| `src/dllmain.cpp` | DPI awareness, startup ordering |
| `src/config.cpp` | `DishonoredBorderless.ini` |
| `tools/Check-Pe.ps1` | PE assertions |
| `tests/Verify-Borderless.ps1` | Runtime assertions |

Log: `DishonoredBorderless.log` beside the DLL, or `%LOCALAPPDATA%\DishonoredBorderless\`
if the game directory is not writable. `LogLevel=debug` adds a line per style and
geometry correction — the first thing to look at if something ever fights back.

---

## 8. Applying this to other UE3 games

Nothing here is Dishonored-specific except the assumption of D3D9 and the
`.ini` filename. The same proxy should work for any 32-bit D3D9 UE3 title that
lacks borderless support, provided it statically imports `Direct3DCreate9`.
Check that first with the PE parse in §2 — if a game imports `d3d9.dll` lazily
or uses D3D11, the proxy is never loaded and none of this applies.
