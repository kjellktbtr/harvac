# Plan: Enable Dead Code Elimination in Harvac Build

## Context

The Harvac OS build uses OpenWatcom wcc/wlink. Currently, the build system uses:
- `library harvac.lib` for demand-driven loading at object file level (good)
- But no function-level dead code elimination within object files

OpenWatcom wlink supports `option eliminate` for dead code elimination, but it requires:
1. Compiler flag `-zm` ("emit functions in separate segments") - puts each function in its own code segment
2. Linker option `option eliminate` - removes unreferenced segments
3. Linker option `option start=symbol` - defines the entry point so linker knows what's "referenced"

## Goal / Success Criteria

- Enable function-level dead code elimination for kernel and all .COM apps
- Reduce binary sizes by eliminating unused functions
- Build must succeed and all apps must work correctly
- Kernel must boot and shell must run

## Assumptions & Open Questions

- [x] The `-zm` flag is compatible with small memory model (-ms) and 8086 target (-0)
- [x] The kernel entry point symbol `__entry` works with `option start=__entry`
- [x] The app entry point symbol `_main_` (mangled from `_main`) works with `option start=_main_`
- [x] No issues with assembly files (rt.asm, entry.asm, syscall.asm, exec_stub.asm) - they don't use -zm

## Phase 1: Add -zm compiler flag to all C compilations

### 1.1 Kernel and library C sources
- [x] Modify `compile_c()` function in build.py to add `-zm` flag
- [x] Verify this applies to all C_SOURCES and library sources

### 1.2 App C sources
- [x] Already covered by compile_c() since apps use the same function

### 1.3 MEDIT and NCD sources
- [x] Modify `build_medit()` and `build_ncd()` to add `-zm` flag

## Phase 2: Add linker eliminate options

### 2.1 Kernel linking (link_kernel)
- [x] Add `option eliminate` to temp linker file
- [x] Add `option start=__entry` to temp linker file
- [x] Verify kernel still boots

### 2.2 App linking (link_app)
- [x] Add `option eliminate` to temp linker file
- [x] Add `option start=_main_` to temp linker file
- [x] Only for C apps (is_c_app == True)

### 2.3 MEDIT linking (build_medit)
- [x] Add `option eliminate` and `option start=_main_`

### 2.4 NCD linking (build_ncd)
- [x] Add `option eliminate` and `option start=_main_`

## Phase 3: Build and Test

### 3.1 Full build
- [x] Run `make check` (compile check)
- [x] Run `make` (full build)
- [x] Run `make run` (boot test)

### 3.2 Size verification
- [x] Compare binary sizes before/after
- [x] Verify KERNEL.COM and .COM apps are smaller or same size

### 3.3 Size Results

| App | Before | After | Savings |
|-----|--------|-------|---------|
| KERNEL.COM | 18392 | 17926 | 466 (2.5%) |
| HELLO.COM | 8262 | 304 | 7958 (96.3%) |
| CAT.COM | 8400 | 1434 | 6966 (82.9%) |
| LS.COM | 8592 | 2194 | 6398 (74.5%) |
| EDIT.COM | 15552 | 11468 | 4084 (26.3%) |
| NCD.COM | 13514 | 11668 | 1846 (13.7%) |

## Done when

- [x] All phases complete and verified
- [x] Build passes without errors
- [x] Kernel boots and shell runs
- [x] Binary sizes reduced (or at least not increased)