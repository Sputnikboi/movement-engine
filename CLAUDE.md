# Claude Code Instructions

## Workflow
- After every change that compiles, commit and push so it can be tested on the Windows machine.
- Cannot run sudo directly (password required). Write out the sudo command and the user will run it with `!` if it looks right.

## Project
- C++20, Vulkan 1.3, SDL3, Dear ImGui, HandmadeMath, cgltf
- Build (Linux, for compile checks only): `cmake --build /home/sputnikboi/movement-engine/build`
- Windows builds via GitHub Actions — trigger with `gh workflow run windows.yml --repo Sputnikboi/movement-engine`
- Single binary target: `movement_engine`
