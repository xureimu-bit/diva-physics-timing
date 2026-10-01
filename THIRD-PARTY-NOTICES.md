# Third-party notices

The MIT license at the repository root applies to this project's own code and documentation. Third-party components retain their own licenses.

- [MinHook](https://github.com/TsudaKageyu/minhook): BSD-style license, `source/third_party/minhook/LICENSE.txt`; original disassembler notices are retained.
- [ReShade](https://github.com/crosire/reshade), SDK headers for 6.8.0: BSD-style license, `source/controls/third_party/reshade/LICENSE.md`.
- [Dear ImGui](https://github.com/ocornut/imgui), 1.92.5 docking headers: MIT license, `source/controls/third_party/imgui/LICENSE.txt`. ReShade supplies the ImGui implementation at runtime. Upstream URLs and header hashes are in `source/controls/third_party/sources.json`.

The game adapter's offsets and layout names were checked against public reference projects:

- [MMPlusMods](https://github.com/korenkonder/MMPlusMods), `src/MMPlusModsShared/rob/rob.hpp` (MIT, `licenses/LICENSE-MMPlusMods.txt`).
- [ReDIVA](https://github.com/korenkonder/ReDIVA), `src/CRE/rob/ex_block.cpp` and `src/KKdLib/mat.cpp` (MIT, `licenses/LICENSE-ReDIVA.txt`).

Game function addresses are validated against instruction bytes before hooks are enabled. These references do not imply compatibility with every game version. No game files are distributed here.
