# UPT

A small C path tracer. Build dependencies are CMake, Clang/Clang++, and an
LTO-capable linker (LLVMgold or LLD). Python 3 and Pillow are needed to download
and convert the benchmark assets.

Build without the optional Qt display:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DUPT_WITH_GUI=OFF
cmake --build build -j
```

CMake enables `-O3`, `-march=native`, and Clang full LTO by default. To use LLD,
add `-DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld` to the configure command.
The optional GUI build requires Qt 5 Widgets and `-DUPT_WITH_GUI=ON`.

Rendering uses one thread by default. Pass `--threads N` before the frame count
to select a worker count; `--threads 0` uses all online CPUs. For the GUI:

```sh
build/upt/upt --threads 0 --gui scenes/glass.pbrt
```

Download the assets and run all six benchmarks from the repository root:

```sh
python3 scenes/fetch.py
python3 scenes/render_previews.py build/upt/upt
```

Each scene runs at 512×512 for at least four measured frames and ten measured
seconds, after two warm-up frames. Runs are sequential by default. Images and
timings go to `.cache/renders/`; open `scenes/gallery.html` to view them.
Use `--resume` to continue an interrupted run, or rebuild only the HTML with:

```sh
python3 scenes/render_previews.py --html-only
```
