# Dishonored 1 Borderless Windowed Mod – Feasibility & Plan

## Goal
Create a custom, technical mod or shim that forces Dishonored 1 (UE3) to run in a truly borderless windowed mode on modern Windows, without relying on generic borderless tools, and understand precisely what prevents Dishonored 1 from working with borderless windowed mode. [web:22]

## High‑Level Findings (From Research)
- Dishonored 1 uses Unreal Engine 3 with a constrained window management layer: the game disables runtime resizing and tightly controls fullscreen/windowed state. [web:22][web:35]
- PCGamesN’s Dishonored PC tweak guide explicitly notes that “all resizing of the window in Dishonored is completely disabled” and that standard window‑stretching tools do not work. [web:22]
- Community reports confirm that Borderless Gaming, NoMoreBorder, generic “RemoveBorders” utilities, and PCGW‑style methods fail or cause flickering/crashes in Dishonored 1, even though they work fine with other titles. [web:14][web:11][web:29][web:30]
- The game can be forced into windowed mode via launch options (`-windowed`, `-sw`) or Alt+Enter, but once in windowed mode the client window cannot be resized and appears to fight external tools that attempt border or size manipulation. [web:28][web:32][web:23][web:33]
- Resolution is stored in non‑text locations (registry and options files) rather than simply in INI keys, indicating custom handling around window creation and mode switching. [web:22][web:15][web:33]

Conclusion: Dishonored 1’s executable/UE3 client code likely:
- Creates a fixed‑size window with disabled resize styles.
- Hooks or overrides standard Windows messages related to resize and style changes.
- Re‑asserts its desired window style on focus or mode changes, which causes flickering/minimization when external tools attempt to change it. [web:22][web:14][web:27][web:33]

A conventional borderless tool that only sets `WS_POPUP` / clears borders once is therefore insufficient. Any feasible solution likely must:
- Interpose at the Direct3D / swap chain layer, or
- Hook the game’s window procedure and override its behavior, or
- Replace or wrap the window while still presenting the game’s render surface. [web:21][web:22][web:35]

---

## What Is Preventing Borderless Window From Working

### Engine Behavior
- UE3 itself does not ship borderless support out of the box; UE3 games that have it modified engine source. [web:35]
- Dishonored’s build hard‑locks the window size and disables user resizing, likely via window style flags (no `WS_THICKFRAME`, disabled `WS_MAXIMIZEBOX`, etc.). [web:22][web:33]
- The game appears to re‑apply its window styles when gaining focus or when alt‑tabbing, causing external changes to be undone and sometimes producing a black/flickering screen when tools like Borderless Gaming try to stretch it. [web:14][web:20][web:27][web:30]

### Window Resizing Disabled
- PCGamesN explicitly states that all window resizing is disabled and that tools which “stretch” windowed games into fullscreen fail for Dishonored. [web:22]
- Generic borderless tools normally:
  - Force the game into windowed mode.
  - Remove window borders/title bar (changing style to `WS_POPUP`, etc.).
  - Resize the window to match desktop resolution.
- Because Dishonored blocks resize and restyles itself, step (3) fails or is immediately undone, leading to reports of endless flicker and forced minimization. [web:14][web:27][web:30][web:33]

### Resolution & Mode Storage
- Resolution is controlled through engine state, config, and registry; users report that launch options can be ignored and windowed/fullscreen behavior is inconsistent. [web:28][web:32][web:23][web:33]
- PCGamesN notes that borderless fullscreen windowed “has not been implemented” and that even standard stretching tools do not work, reinforcing that mode handling is tightly controlled by the executable. [web:22]

### Interaction With External Hooks
- PCGamingWiki and Steam threads mention GeDoSaTo and other advanced tools as options but users report Dishonored hanging or failing to launch when hooked. [web:14][web:31]
- Reddit and Steam discussions repeatedly describe Dishonored as one of the hardest games to force into borderless windowed, with nearly every standard tool failing due to its UE3 window handling. [web:11][web:29][web:30]

---

## Feasibility Analysis – Options

### Option A: Pure Win32 Window Style/Resize Shim (External Helper)
**Concept:**
- A helper process:
  - Enumerates windows.
  - Identifies the Dishonored game window by class name, title, or process ID.
  - Repeatedly enforces specific styles/position (borderless + desktop‑sized) via a timer loop.

**Problems:**
- Dishonored disables resize and restyles itself:
  - You’ll likely see constant flicker due to competing style changes.
  - Focus/input bugs and possible crashes. [web:22][web:14][web:27]
- Without going inside the process (WndProc hook), you may not be able to stop it from undoing your changes. [web:22]

**Feasibility:**
- Low for a clean, stable solution; at best you might brute‑force something ugly with visible artifacts.

### Option B: WndProc Hook + Style Override (Injected DLL)
**Concept:**
- Inject a custom DLL into Dishonored’s process.
- Locate the game’s main window (`FindWindow`, `EnumWindows` + `GetWindowThreadProcessId`).
- Subclass the window by replacing its WndProc (`SetWindowLongPtr(GWLP_WNDPROC)`).
- Intercept/override resize/style‑related messages:
  - `WM_SIZE`, `WM_WINDOWPOSCHANGING`, `WM_STYLECHANGING`, etc.
- Force:
  - Window style: borderless (`WS_POPUP`, no caption/thick frame).
  - Window rect: desktop resolution on the target monitor.

**Why this could work:**
- You are handling the same messages the game uses and can block/modify its attempts to reset size/styles.
- Subclassing allows selective forwarding to the original WndProc, minimizing breakage of unrelated behavior.

**Risks/unknowns:**
- UE3’s internal abstractions could crash if message flow diverges too far from expectations.
- Dishonored may assume exclusive fullscreen semantics for input/vsync/alt‑tab; running borderless could expose edge cases. [web:22][web:33]

**Feasibility:**
- Technically feasible but non‑trivial; suitable for a developer comfortable with C++/Win32 and injection.

### Option C: D3D Wrapper / Proxy (Swap Chain‑Level Borderless)
**Concept:**
- Build a Direct3D 9 wrapper DLL (`d3d9.dll`) that Dishonored loads instead of system D3D.
- In the wrapper:
  - Intercept `IDirect3D9::CreateDevice` / `IDirect3DDevice9::Reset`.
  - Inspect and modify `D3DPRESENT_PARAMETERS` (fullscreen vs windowed, backbuffer size, etc.).
- Enforce:
  - `Windowed = TRUE`.
  - Backbuffer width/height = desktop or configured resolution.
- Optionally create/manage a borderless top‑level window that hosts the game’s rendering surface while Dishonored thinks it’s fullscreen.

**Analogues:**
- Borderless Gaming’s BGProxy adds “Force Windowed” and flip‑model control, making games think they’re fullscreen while actually running windowed. [web:21]

**Challenges:**
- Verify renderer: Dishonored is D3D9, but confirm on your install. [web:33]
- Engine may still attempt exclusive fullscreen semantics; you must keep internal logic happy while faking borderless.

**Feasibility:**
- Medium–high for someone comfortable with C++/COM and D3D9 proxy patterns.

### Option D: Code Patch / EXE Binary Mod
**Concept:**
- Use IDA/Ghidra on Dishonored’s executable.
- Identify UE3 client code that:
  - Calls `CreateWindowEx`.
  - Sets window styles and calls `ShowWindow`.
  - Disables resizing or restyles on focus.
- Patch instructions to:
  - Include resize‑friendly styles.
  - Stop re‑enforcing fullscreen or fixed size.
  - Potentially relax checks on resolution.

**Pros:**
- No external helper; behavior changed at source.
- Very stable once correct patches are found.

**Cons:**
- Significant RE effort.
- Legal/ethical considerations of patching a commercial EXE.
- Any update (unlikely now) could invalidate patches.

**Feasibility:**
- High from a pure technical standpoint, but time‑intensive; best if you’re comfortable with disassembly and patching.

---

## Recommended Strategy

Given constraints and your skill set, the most promising approach is a **D3D9 proxy + WndProc hook hybrid**:

- D3D9 wrapper:
  - Keeps Dishonored logically “fullscreen” while physically running in a window sized to your desktop. [web:21][web:22][web:33]
  - Controls backbuffer size, windowed flag, and reset behavior at the renderer level.
- WndProc hook:
  - Ensures the window stays borderless.
  - Overrides any resize/focus logic that tries to revert fullscreen or enforce fixed size. [web:22][web:14][web:27]

This mimics BGProxy‑style behavior but with game‑specific control tailored to Dishonored’s quirks. [web:21]

---

## Implementation Plan (Repository `plan.md`)

### 1. Scope & Objectives
- Target: Dishonored 1 (Steam, Windows, UE3).
- Goal: Stable, fully borderless windowed at arbitrary resolutions.
- Constraint: No generic borderless wrapper; Dishonored‑specific solution.

### 2. Technical Background

#### 2.1 Engine & Windowing
- Engine: UE3, no built‑in borderless support. [web:35]
- Known behavior:
  - Resizing disabled; borderless fullscreen windowed not implemented and stretching tools fail. [web:22]
  - Windowed mode via `-windowed` or Alt+Enter; launch options sometimes overridden. [web:28][web:32][web:23]
  - Resolution behavior is non‑trivial and linked to internal state and configs. [web:33]

#### 2.2 Prior Attempts (Community)
- Borderless Gaming / NoMoreBorder / RemoveBorders:
  - Flicker, crashes, or no effect on Dishonored. [web:14][web:27][web:30]
- GeDoSaTo:
  - Listed as option but users often can’t get Dishonored running under it. [web:14][web:31]
- General consensus:
  - Dishonored is unusually resistant to borderless hacks; PCGamesN confirms tools can’t stretch it. [web:22][web:29]

### 3. Design Overview

#### 3.1 Architecture
- Components:
  - `d3d9.dll` proxy (custom).
  - WndProc hook module (inside proxy DLL).
  - `DishonoredBorderless.ini` config file.

- Data flow:
  1. Dishonored loads custom `d3d9.dll` from game directory.
  2. Proxy loads real system `d3d9.dll` and wraps `IDirect3D9`.
  3. Proxy intercepts device creation/reset and forces windowed + desktop‑sized backbuffer.
  4. Proxy discovers main window handle after device creation.
  5. Proxy installs WndProc subclass.
  6. WndProc hook enforces borderless style and position on relevant messages.

#### 3.2 Configuration Options
- `DishonoredBorderless.ini`:
  - `ResolutionWidth`, `ResolutionHeight`.
  - `TargetMonitorIndex`.
  - `EnableWndProcHook = true/false`.
  - `LogLevel = info/debug/trace`.

### 4. Detailed Tasks

#### 4.1 Environment Setup
- Tools:
  - Visual Studio (MSVC).
  - Windows SDK (Win32 + D3D9 headers).
  - x64dbg/Process Explorer for runtime inspection.

- Steps:
  - Build a trivial D3D9 sample app and test proxy DLL there first.
  - Confirm your proxy works before attaching to Dishonored.

#### 4.2 D3D9 Proxy Implementation

##### 4.2.1 Proxy Skeleton
- Export `Direct3DCreate9`.
- Internally load `C:\Windows\System32\d3d9.dll` using `LoadLibrary`.
- Return your wrapped `IDirect3D9` object that holds a pointer to the real one.

##### 4.2.2 Device Creation/Reset
- Wrap `IDirect3D9::CreateDevice`:
  - Inspect `D3DPRESENT_PARAMETERS`.
  - Force `Windowed = TRUE`.
  - Set `BackBufferWidth/Height` to desktop or `ResolutionWidth/Height` from config.

- Wrap `IDirect3DDevice9::Reset` similarly:
  - Maintain borderless windowed semantics after in‑game resolution changes.

##### 4.2.3 Presentation
- Forward `Present` and swap chain calls to the real device.
- Optionally add logging for device resets and mode changes to debug behavior.

#### 4.3 WndProc Hook Implementation

##### 4.3.1 Window Discovery
- After device creation, find the game window:
  - Use `EnumWindows` + `GetWindowThreadProcessId` filtered by Dishonored’s PID.
  - Optionally confirm by title/class.

##### 4.3.2 Subclassing
- Store original WndProc via `GetWindowLongPtr(GWLP_WNDPROC)`.
- Install `BorderlessWndProc` using `SetWindowLongPtr`.

##### 4.3.3 Message Handling
- In `BorderlessWndProc`:
  - On `WM_SIZE`, `WM_WINDOWPOSCHANGING`, `WM_STYLECHANGING`, `WM_ACTIVATE`:
    - Enforce borderless style (clear caption/borders, set `WS_POPUP`).
    - Set window rect to desktop resolution for target monitor.
  - For other messages:
    - Call original WndProc.

- Add safety:
  - Try/catch style guards or structured exception handling to avoid crashing on unexpected messages.

#### 4.4 Resolution & Registry Integration
- Optionally:
  - Read resolution details from user configs if you want to sync internal game res and backbuffer. [web:33]
  - Provide CLI or small GUI to adjust settings without editing `.ini` manually.

#### 4.5 Testing

##### 4.5.1 Core Tests
- Launch with proxy:
  - Verify game runs normally.
  - Verify the window is borderless and fills desktop.
- Alt‑tab behavior:
  - Confirm it returns cleanly and doesn’t minimize/flicker more than vanilla. [web:22][web:27]

##### 4.5.2 Edge Cases
- Multi‑monitor setups.
- Different desktop resolutions (1080p, 1440p, 4K).
- In‑game fullscreen/windowed toggle (ensure you handle mode changes gracefully).

##### 4.5.3 Performance/Input
- Check for:
  - Increased input latency or stutter.
  - Issues with raw mouse input or keyboard capture.

#### 4.6 Contingencies
- If subclassing WndProc crashes:
  - Fallback to periodic `SetWindowPos`/style enforcement from a timer inside the DLL.
- If UE3 recreates the window:
  - Detect new window handle and re‑apply subclassing.
- If proxy fails on some systems:
  - Add configuration flag to disable D3D hooking and use only WndProc hook.

### 5. Future Extensions
- Generic UE3 profile:
  - Reuse proxy/hook for other UE3 games that lack borderless mode. [web:35]
- Overlay UI:
  - Runtime toggling of borderless settings and resolution.
- Integration with RTSS or custom frame limiter for improved VRR behavior. [web:11]

### 6. Risk & Effort Summary
- Development time: weeks of part‑time work for robust implementation.
- Stability risk: medium; mitigated with careful testing and fail‑safe modes.
- Overall feasibility: moderately high for a technically sophisticated developer comfortable with C++/Win32/D3D9.

If successful, this project becomes a bespoke Dishonored 1 borderless windowed solution that works around the engine’s unusual window management rather than relying on generic tools that the game actively thwarts. [web:22][web:14][web:29]
