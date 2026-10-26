External material, not included (see the main README, "Requirements"). Expected layout:

| Folder | Contents | Source |
|---|---|---|
| `sdk25_interfaces/includes/`, `sdk25_interfaces/libs/` | 3DO SDK 2.5 headers and libraries | `ext.3dodev.com/3DO/Portfolio_2.5/Interfaces/2p5/` |
| `sdk25_examples/ExamplesLib/` | SDK 2.5 ExamplesLib sources | `ext.3dodev.com/3DO/Portfolio_2.5/Examples/ExamplesLib/` |
| `sdk25_examples/NVRAM/Storage_Tuner/` | SDK 2.5 Storage Tuner sources (`st.c`, `stlists.c`, `stmem.c`, headers, `storagetuner.make`) | `ext.3dodev.com/3DO/Portfolio_2.5/Examples/NVRAM/Storage_Tuner/` |
| `sdk_mpw_armtools/` | MPW `armcc` 4.62, `armlink` 4.49 | `ext.3dodev.com/3DO/Toolkit_1.5/Tools/ArmTools/` |
| `3do-devkit/lib/` (optional) | SDK libraries for `tools/match_all.sh` | <https://github.com/trapexit/3do-devkit> |
| `portfolio_os/kernellib/` | `finditem.c`, `CreateItem.c`, `supervectors.s` | <https://github.com/trapexit/portfolio_os> |

Check the files with `sha256sum -c references.sha256` from the repository root.
