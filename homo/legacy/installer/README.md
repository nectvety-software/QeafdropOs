# Legacy installer source (preserved, not part of the shipping build)

This source was migrated from `VQEAF_OS/apps/legacy/installer`. It emits the historical `VQEAF-QEAPP/1` ZIP layout, which is not the current QEAPP `manifest.json` package contract. Do not add it to the shipping image until it is ported and tested by the current QEAPP manager.

The active OS build is `python homo/scripts/build_violet.py` followed by `python homo/scripts/package_violet.py`.