# Storage management and Recovery

Open **Settings → Library Cleanup / Storage…** to review content by age and reclaim unused file storage.

## Find and review content

1. Enter a period from 1 to 36500 days and choose **Find Content**. The scan runs in the background.
2. Review presentations, media assets, and unreferenced managed files. **Show protected / unknown-age items** explains exclusions.
3. Check individual eligible items or choose **Select All Eligible**. The summary estimates recoverable file storage.
4. Choose **Review Move to Recovery…** and inspect records and file dispositions.
5. Confirm **Move to Recovery** to hide those records while retaining their IDs, content, and files. This stage retains the files on disk.

Creation, import, modification, and actual last-use dates are tracked separately. Unknown history remains protected. Live takes, navigation, and actual media previews record use. Browsing a library row leaves usage history alone.

Saved services protect referenced presentations and direct media. Presentations and Looks protect shared files. The entire live service snapshot, including upcoming entries, is protected across running WorshipCue windows. Paths outside managed folders and symlinks are protected.

## Restore or permanently remove

In **Recovery**, select a batch and choose **Restore Selected Batch** to return it to the library.

To reclaim storage, choose **Review Permanent Removal…**. Review the selected records, removable files, retained-file reasons, and estimate. **Permanently Delete** confirms irreversible removal. Shared files are counted once and retained while another active or Recovery record or saved Look references them.

If content changed or was used again after archiving, restore it and run a fresh age scan. **Refresh / Retry Pending Removal** retries file removal that was already confirmed but could not finish.

## Back up the library

Close WorshipCue windows and the MCP companion, then copy the entire library folder, including the database, `media`, `backgrounds`, and `.cleanup-recovery` when present. Include any remaining SQLite `-wal` and `-shm` files. Restore the folder's contents together to preserve relative file references.

[Content Library](content-library.md) · [Media Library](media-library.md) · [Troubleshooting](troubleshooting.md)
