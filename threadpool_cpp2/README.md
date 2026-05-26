# Thread Pool Server — C++ / Win32 GDI Edition
### Zero external dependencies — works out-of-the-box with CLion's bundled MinGW

```
┌──────────────────────────────────────────────────────┐
│  THREAD POOL SERVER  |  Sci-Fi Edition               │
│  Win32 GDI  ·  C++17  ·  No SFML  ·  No Qt          │
└──────────────────────────────────────────────────────┘
```

## Why Win32 GDI?
The previous version needed SFML which had to be separately downloaded.
This version uses **only Windows system libraries** (gdi32, user32, comctl32)
which are already available in CLion's bundled MinGW — no installation needed.

---

## Open in CLion (2–3 clicks)

1. **File → Open** → select the `threadpool_cpp2` folder
2. CLion detects `CMakeLists.txt` automatically → click **Open as Project**
3. Hit **▶ Run** (or Shift+F10)

That's it. No SFML_DIR, no vcpkg, no package manager.

---

## Build from terminal (MinGW)

```bash
mkdir build && cd build
cmake .. -G "MinGW Makefiles"
mingw32-make -j4
./ThreadPoolServer.exe
```

---

## Features (identical to Python version)

| Feature | Description |
|---------|-------------|
| Thread status | 4 animated IDLE/BUSY panels, updated 60×/s |
| Scan-line | Moving cyan glow across the grid background |
| Log terminal | Real-time scrolling listbox, green-on-black |
| Task: Print | Sleeps briefly, logs start/done |
| Task: Sleep | Blocks the thread for N seconds |
| Task: CPU | Computes fib(N), reports time |
| Task: IO Sim | Simulates file writes with delays |
| Run Demo | Queues 5 tasks across all thread types |
| CPU Benchmark | Single-thread vs pool, prints speedup |
| Wait All | Blocks until queue empty |
| Timeline | Gantt chart of all finished tasks |

---

## Project structure

```
threadpool_cpp2/
├── CMakeLists.txt        ← zero-dependency build
├── README.md
├── include/
│   ├── ThreadPool.h      ← C++17 thread pool (header-only)
│   └── Tasks.h           ← task_print/sleep/cpu/io (header-only)
└── src/
    └── main.cpp          ← Win32 GUI + WinMain
```

---

## Keyboard shortcuts

| Key | Action |
|-----|--------|
| **T** | Toggle timeline |
| **Esc** | Toggle timeline |
| **Click** anywhere | Close timeline overlay |

---

## Notes
- The log listbox is capped at 500 lines (auto-scrolls)
- The benchmark spins up a second pool of 4 threads and measures wall-clock time
- Timeline only shows **finished** tasks
- Press Enter in any input field or click ADD to queue that task type
