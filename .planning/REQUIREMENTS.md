# Requirements: AsyncIOManager Modernization

**Defined:** 2026-09-03
**Core Value:** Reliable async load/save of data across local and remote protocols behind one URL-dispatched `FileManager` API.

## v1 Requirements

Requirements for this milestone. Each maps to roadmap phases.

### MNN Removal

- [x] **MNN-01**: `find_package(MNN)` and all MNN include/link references are removed from root `CMakeLists.txt` and `src/CMakeLists.txt`
- [ ] **MNN-02**: `include/MNNCommon.hpp`, `include/MNNLoader.hpp`, `include/MNNSaver.hpp`, `src/MNNLoader.cpp`, `src/MNNSaver.cpp`, `test/base_mnn_test.hpp` are deleted
- [x] **MNN-03**: `test/1.mnn` and `test/2.mnn` assets are replaced with generic binary test fixtures (e.g. `test/*.bin`)
- [ ] **MNN-04**: `mnn://` save prefix registration is removed; `file://` remains the only local save prefix
- [ ] **MNN-05**: All MNN references in doc comments (`@param parse - ... (for MNN)`, "Load Data on the MNN file", etc.) are scrubbed from headers and sources
- [x] **MNN-06**: Example app contains no MNN inference code or MNN library references (see FILE-11)

### LocalFile Rename

- [ ] **FILE-01**: `MNNLoader` class/files become `LocalFileLoader` (`include/LocalFileLoader.hpp`, `src/LocalFileLoader.cpp`), still registering for `file://` load prefix
- [ ] **FILE-02**: `MNNSaver` class/files become `LocalFileSaver`, registering only for `file://` save prefix
- [ ] **FILE-03**: `FileManager::InitializeSingletons()` initializes `LocalFileLoader`/`LocalFileSaver` (no MNN names anywhere)
- [ ] **FILE-04**: `FILECommon` device layer becomes `LocalFileCommon` (`include/LocalFileCommon.hpp`, `src/LocalFileCommon.cpp` split — see below)

### Platform Split

- [x] **PLAT-01**: Local-file device implementation exists as separate Windows source+header (e.g. `LocalFileCommon.win.cpp` / `LocalFileCommon.win.hpp`) with zero `#ifdef`
- [x] **PLAT-02**: Local-file device implementation exists as separate POSIX source+header (e.g. `LocalFileCommon.posix.cpp` / `LocalFileCommon.posix.hpp`) with zero `#ifdef`
- [x] **PLAT-03**: `src/CMakeLists.txt` selects the platform pair via `if(WIN32)/elseif(UNIX)` — no platform branching inside source files
- [x] **PLAT-04**: Shared/common declarations (if any remain) live in a platform-neutral header without `#ifdef _WIN32`

### Parser Layer Removal

- [x] **PARSE-01**: `include/FileParser.hpp` is deleted; `RegisterParser`, `ParseData`, and the `parsers` map are removed from `FileManager`
- [x] **PARSE-02**: The `parse` bool is removed from `FileLoader::LoadASync`, `FileManager::LoadASync`, `CompletionCallback`, all per-protocol loader/device APIs (`HTTPLoader`, `IPFSLoader`, `SFTPLoader`, `WSLoader`, and `*Common` devices), and all call sites
- [x] **PARSE-03**: The `save` bool remains in all signatures and the auto-save-after-load flow keeps working
- [x] **PARSE-04**: No behavioral change to load/save paths beyond signature cleanup (callbacks still receive `ioc`, `ResultType`, `save`)

### Build & Tests

- [ ] **TEST-01**: `mnn_loader_test` / `mnn_saver_test` are renamed to `localfile_loader_test` / `localfile_saver_test` covering the same behaviors (sync load, async load, nonexistent-file error, sync save, null-data error, async save)
- [ ] **TEST-02**: `filemanager_test` dispatch expectations updated to `LocalFileLoader`/`LocalFileSaver` names
- [x] **TEST-03**: All tests reference generic fixtures, not `.mnn` files
- [x] **TEST-04**: Full test suite builds and passes on Windows (`BUILD_TESTING=ON`)
- [x] **BUILD-01**: `MNNExample` is replaced by a generic example (e.g. `FileExample`) demonstrating `file://` load and save via `FileManager`
- [x] **BUILD-02**: Library builds clean on Windows with zero MNN dependency; `grep -ri mnn` over `include/ src/ test/ example/ CMakeLists.txt` returns no matches

## v2 Requirements

Deferred to future milestone. Tracked but not in current roadmap.

### Dormant Protocol Activation

- **PROTO-01**: Activate `SFTPLoader` in `FileManager::InitializeSingletons()` with tests
- **PROTO-02**: Activate `WSLoader` in `FileManager::InitializeSingletons()` with tests
- **PROTO-03**: Enable plain `http://` prefix in `HTTPLoader` registration

### Ifdean Cleanup (Extended)

- **CLEAN-01**: Apply per-platform file split pattern to remaining `#ifdef` sites (`FILECommon` is the only required one this milestone; assess `IPFSCommon`/others later)

## Out of Scope

Explicitly excluded. Documented to prevent scope creep.

| Feature | Reason |
|---------|--------|
| Activating dormant SFTP/WS loaders | Refactor-only milestone; activation changes runtime behavior, deferred to v2 |
| Removing `save` bool from `LoadASync` | User explicitly wants it kept — drives live auto-save-after-load behavior |
| MNN inference support | Library responsibility is async I/O; MNN usage moves to consumers (SuperGenius) |
| Backward-compat shims for renamed API | Breaking change accepted; downstream consumers update separately |
| New protocols / features | Not a feature milestone |
| Linux CI verification | No Linux CI in this environment; POSIX split verified structurally |
| Fixing `hang.dmp` latent hang issue | Pre-existing, unrelated to this refactor |

## Traceability

Which phases cover which requirements. Updated during roadmap creation.

| Requirement | Phase | Status |
|-------------|-------|--------|
| MNN-01 | Phase 4 | Complete |
| MNN-02 | Phase 1 | Pending |
| MNN-03 | Phase 4 | Complete |
| MNN-04 | Phase 1 | Pending |
| MNN-05 | Phase 1 | Pending |
| MNN-06 | Phase 4 | Complete |
| FILE-01 | Phase 1 | Pending |
| FILE-02 | Phase 1 | Pending |
| FILE-03 | Phase 1 | Pending |
| FILE-04 | Phase 1 | Pending |
| PLAT-01 | Phase 2 | Complete |
| PLAT-02 | Phase 2 | Complete |
| PLAT-03 | Phase 2 | Complete |
| PLAT-04 | Phase 2 | Complete |
| PARSE-01 | Phase 3 | Complete |
| PARSE-02 | Phase 3 | Complete |
| PARSE-03 | Phase 3 | Complete |
| PARSE-04 | Phase 3 | Complete |
| TEST-01 | Phase 1 | Pending |
| TEST-02 | Phase 1 | Pending |
| TEST-03 | Phase 5 | Complete |
| TEST-04 | Phase 5 | Complete |
| BUILD-01 | Phase 4 | Complete |
| BUILD-02 | Phase 5 | Complete |

**Coverage:**

- v1 requirements: 24 total
- Mapped to phases: 24/24 ✓
- Unmapped: 0

---
*Requirements defined: 2026-09-03*
*Last updated: 2026-09-03 after roadmap creation*
