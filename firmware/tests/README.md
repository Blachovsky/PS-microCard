# Firmware host tests

This directory contains tests that run on the host computer using Ceedling 1.1.3,
Unity 2.7.1, and CMock 2.7.0. The production firmware is still built with
CMake from the parent directory.

The current test suite includes:

- 61 unit tests,
- 14 pipeline integration tests,
- 75 tests in total.

## Installation and running the tests

Ruby dependencies are pinned in `Gemfile.lock`. Install them once by running
the following commands in this directory:

```console
bundle config set --local path vendor/bundle
bundle install
```

### Windows

Run selected groups from PowerShell or `cmd.exe`:

```powershell
# Unit tests only
.\run_tests.cmd "test:path[unit]"

# Integration tests only
.\run_tests.cmd "test:path[integration]"

# All tests
.\run_tests.cmd test:all
```

A single test file can be run with the `test:file_name` task, for example:

```powershell
.\run_tests.cmd test:test_micro_sd_worker
```

### Linux and macOS

The POSIX script accepts the same Ceedling tasks:

```bash
# Unit tests only
./run_tests.sh "test:path[unit]"

# Integration tests only
./run_tests.sh "test:path[integration]"

# All tests
./run_tests.sh test:all

# Single test file
./run_tests.sh test:test_micro_sd_worker
```

Build artifacts are stored in `tests/build/` and ignored by Git.

## Structure

```text
tests/test/
|-- unit/                 tests for individual modules
|-- integration/          tests covering flows across multiple modules
|   `-- support/          environment used only by pipeline tests
`-- support/              shared Pico SDK, FatFs, and hardware substitutes
```

Files in `test/support/` are used by both test groups. They expose only the
minimal platform interfaces required by the host tests. Production code is
compiled for the target device using the real Pico SDK and FatFs.

The integration tests compile the real `ps1_card_bus`, `ps1_card_emulator`,
`micro_sd_image`, and `micro_sd_worker` modules together. Hardware boundaries,
time, and the filesystem remain simulated. The file model distinguishes data
written by `f_write` from data that becomes durable only after a successful
`f_sync`.

The pipeline support layer does not compile `main.c`, `menu.c`, `micro_sd.c`,
the board configuration, OLED driver, or production PIO program. It supplies
its own lifecycle, mount/card-presence/path model and automatically acknowledges
the image-switch pause that real firmware negotiates with Core 0.

## Unit tests

### `test_ps1_card_emulator.c`

| Test | What it verifies |
|---|---|
| `test_get_frame_ptr_returns_frame_start_for_valid_address` | Returns the start of the first and last valid frame in RAM. |
| `test_get_frame_ptr_rejects_address_outside_card` | Rejects address 1024 and addresses outside the card image. |
| `test_frame_checksum_combines_address_bytes_and_frame_data` | Calculates the XOR of both address bytes and all 128 frame bytes. |
| `test_commit_frame_rejects_invalid_arguments` | Rejects an out-of-range address and a `NULL` data pointer. |
| `test_take_changed_frame_rejects_invalid_arguments` | Validates all output pointers passed to the `take` function. |
| `test_commit_and_take_changed_frame_returns_stable_copy` | Writes a known 128-byte frame and retrieves an identical, stable copy. |
| `test_commits_to_first_middle_and_last_frames_are_isolated_and_pending` | Verifies isolation of writes to frames 0, 10, 100, and 1023 and that they remain pending. |
| `test_two_identical_commits_still_publish_latest_version` | Verifies that two identical writes still publish the latest frame version. |
| `test_version_counter_wraps_through_uint32_max_to_zero` | Verifies version-counter wraparound from `UINT32_MAX` to zero. |
| `test_second_commit_before_take_returns_only_latest_frame_version` | Verifies that multiple writes before `take` expose only the latest data and version. |
| `test_rollback_requeues_frame_that_was_not_confirmed` | Verifies that `rollback` requeues a frame that was taken but not confirmed. |
| `test_rollback_does_not_requeue_confirmed_frame` | Verifies that a frame does not return to the queue after `take` and `confirm`. |

### `test_ps1_card_bus.c`

| Test | What it verifies |
|---|---|
| `test_read_0x52_returns_128_bytes_xor_checksum_and_good_result` | Complete READ `0x52` response, 128 data bytes, XOR checksum, and success result. |
| `test_status_0x53_returns_complete_status_response` | Complete STATUS `0x53` response. |
| `test_write_0x57_commits_exactly_128_bytes_and_returns_good_result` | WRITE `0x57`, exactly 128 bytes, commit to RAM, and success ACK. |
| `test_read_accepts_first_and_last_frame_addresses` | READ operations for boundary frames 0 and 1023. |
| `test_write_accepts_first_and_last_frame_addresses` | WRITE operations for boundary frames 0 and 1023. |
| `test_out_of_range_read_returns_ff_data_checksum_and_bad_sector` | Safe READ response for an out-of-range address. |
| `test_out_of_range_write_returns_bad_sector_without_changing_ram` | Rejects an invalid WRITE without modifying RAM. |
| `test_write_with_bad_checksum_returns_error_without_changing_ram` | Rejects a WRITE with an invalid XOR checksum without a partial commit. |
| `test_unknown_commands_0x00_and_0xff_are_ignored` | Ignores unknown commands `0x00` and `0xFF`. |
| `test_access_byte_other_than_0x81_is_rejected_without_ack` | Rejects devices other than `0x81` without issuing an ACK. |
| `test_cs_abort_at_every_write_byte_stops_at_that_byte` | Aborts WRITE when CS rises at every possible byte boundary. |
| `test_cs_abort_at_every_read_and_status_byte_stops_at_that_byte` | Aborts READ and STATUS when CS rises at every byte boundary. |
| `test_clock_timeout_during_write_data_prevents_commit` | Ensures a timeout during WRITE data cannot modify RAM. |
| `test_hardware_xfer_generates_ack_pulse_and_transfers_lsb_first` | LSB-first bit order and generation of the ACK pulse. |
| `test_hardware_xfer_aborts_when_cs_rises_at_each_bit` | Transport behavior when CS rises at each individual bit. |
| `test_hardware_xfer_aborts_if_cs_rises_while_waiting_for_clock` | Aborts while waiting for the next clock edge if CS rises. |
| `test_hardware_xfer_times_out_when_no_clock_edge_arrives` | Times out when no further clock edge arrives. |
| `test_hardware_xfer_accepts_very_slow_edges_before_timeout` | Accepts very slow transfers that still remain within the timeout limit. |
| `test_hardware_xfer_rejects_edge_at_exact_timeout_boundary` | Behavior exactly at the timeout boundary. |

The host tests verify the parser and the pipelined transport contract: the first
`0x81` byte is received without an ACK, and each subsequent response is
prepared before the ACK that opens the next byte. Tests named
`hardware_xfer_*` exercise only the reference bit-bang transport compiled for
`UNIT_TEST`. Compiling the production PIO state machines proves that they build;
it does not verify their runtime timing or electrical behavior. Those properties
require target-hardware measurements, including logic-analyzer captures.

### `test_micro_sd_worker.c`

| Test | What it verifies |
|---|---|
| `test_flush_writes_one_frame_at_its_offset_then_syncs_closes_and_confirms` | Writes one frame at the correct offset and verifies the sync, close, and confirm sequence. |
| `test_flush_writes_several_frames_and_confirms_each_after_single_sync` | Writes several frames and confirms them only after a shared `f_sync`. |
| `test_poll_syncs_only_after_250_ms_idle_delay` | Synchronizes only after exactly 250 ms of inactivity. |
| `test_flush_does_not_open_or_write_when_no_frame_changed` | Performs no FatFs operations when there are no changes. |
| `test_open_error_rolls_back_without_confirming` | Rolls back and does not confirm after an `f_open` failure. |
| `test_seek_error_closes_file_and_rolls_back_without_confirming` | Closes the file and rolls back after an `f_lseek` failure. |
| `test_write_error_rolls_back_without_confirming` | Rolls back and does not confirm after an `f_write` failure. |
| `test_short_write_of_127_bytes_is_an_error_and_is_not_confirmed` | Treats a short 127-byte write as an error and does not confirm the frame. |
| `test_sync_error_closes_file_and_does_not_confirm_written_frame` | Ensures an `f_sync` failure does not confirm data previously written by `f_write`. |
| `test_close_error_does_not_confirm_synced_frame` | Prevents confirmation after an `f_close` failure even if sync succeeded earlier. |
| `test_no_space_error_from_write_is_recovered_without_confirming` | Handles an out-of-space condition without losing the frame from the queue. |
| `test_read_only_card_error_from_open_is_recovered_without_confirming` | Handles a read-only card error while opening the image. |
| `test_card_removed_before_open_prevents_any_write` | Prevents any write when the SD card is removed before `f_open`. |
| `test_card_removed_between_open_and_write_prevents_commit` | Prevents confirmation when the SD card is removed after opening but before writing. |
| `test_card_removed_between_write_and_sync_rolls_back_pending_frame` | Rolls back the pending frame when the SD card is removed after `f_write` but before `f_sync`. |
| `test_card_removed_during_close_does_not_confirm_frame` | Leaves the frame unconfirmed when the SD card is removed during `f_close`. |
| `test_failed_write_is_retried_after_card_reconnect_and_then_confirmed` | Retries a failed frame after the SD card returns and then confirms it. |
| `test_reconnect_retries_several_frames_left_pending_by_failure` | Retries several frames left pending by a failure. |
| `test_failure_after_partial_batch_replays_all_unconfirmed_frames` | Replays all unconfirmed frames after a failure in the middle of a batch. |

### `test_micro_sd_image.c`

| Test | What it verifies |
|---|---|
| `test_create_new_mcr_has_exact_size_header_directory_and_checksums` | Creates a 131072-byte MCR with the correct header, directory, and checksums. |
| `test_image_size_boundaries_accept_only_131072_bytes` | Rejects sizes 0, 1, 131071, 131073, and oversized files. |
| `test_load_existing_valid_image_copies_all_bytes_to_card_ram` | Loads an entire valid image into card RAM. |
| `test_all_zero_and_all_ff_images_are_formatted_as_blank_cards` | Automatically formats images filled with `0x00` or `0xFF` as blank cards. |
| `test_invalid_header_directory_state_and_directory_checksum_are_rejected` | Rejects an invalid header, directory entry state, and directory checksum. |
| `test_deleted_directory_entry_states_are_accepted` | Accepts valid deleted-entry directory states. |
| `test_list_images_accepts_mcr_case_variants_and_filters_other_files` | Accepts `.MCR`, `.mcr`, and `.McR` extensions while filtering other files. |
| `test_delete_image_flushes_storage_and_removes_file` | Flushes storage before deleting the image and calls `f_unlink` correctly. |
| `test_create_auto_uses_first_free_card_number` | Selects the first available `CARDxxx.MCR` filename. |
| `test_list_images_stops_at_capacity_when_more_images_exist` | Limits the image list to the capacity of the result array. |
| `test_create_auto_reports_error_when_card000_through_card999_are_taken` | Reports an error when all names from `CARD000` to `CARD999` are taken. |

## Pipeline integration tests

Each scenario is located in a separate `test_pipeline_*.c` file and is built as
an independent test executable.

| File and test | What it verifies |
|---|---|
| `test_pipeline_normal_write.c` — `test_pipeline_normal_write_is_synced_confirmed_and_persisted` | Complete WRITE flow through the bus and RAM, exactly 128 bytes at the correct MCR offset, `f_sync`, confirmation, persistence, and final READ. |
| `test_pipeline_immediate_read_before_sd.c` — `test_pipeline_immediate_read_uses_ram_before_sd_worker_runs` | An immediate READ after WRITE sees the new data in RAM even though the worker has not yet updated the MCR file. |
| `test_pipeline_persistence_after_restart.c` — `test_pipeline_synced_write_survives_complete_firmware_restart` | Data made durable by `f_sync` survives a restart and reloading the image. |
| `test_pipeline_restart_before_sync.c` — `test_pipeline_restart_before_sync_uses_only_last_confirmed_data` | A restart after `f_write` but before `f_sync` does not treat buffered data as durable; rollback exposes the lack of confirmation. |
| `test_pipeline_same_frame_twice.c` — `test_pipeline_second_write_to_same_frame_wins_before_sync` | The second WRITE to the same frame before sync wins in both RAM and the MCR image. |
| `test_pipeline_slow_sd_multiple_writes.c` — `test_pipeline_slow_sd_converges_to_latest_ram_state_per_frame` | With slow `f_write` and `f_sync`, multiple interleaved WRITEs converge to the latest state of frames 5, 100, and 500. |
| `test_pipeline_partial_batch_write_failure.c` — `test_pipeline_failure_on_third_frame_recovers_entire_batch` | A failure on the third `f_write` does not lose frames 12 and 13, and after recovery the whole image matches RAM. |
| `test_pipeline_sync_failure.c` — `test_pipeline_sync_failure_keeps_written_frame_unconfirmed` | A successful `f_write` followed by a failed `f_sync` leaves the frame unconfirmed and eligible for retry. |
| `test_pipeline_sd_removal_during_write.c` — `test_pipeline_sd_removal_during_write_disconnects_ps1_safely` | Removing the SD card halfway through a frame does not create a durable partial save and safely disconnects the PS1 card interface. |
| `test_pipeline_sd_reinsert_after_failure.c` — `test_pipeline_sd_reinsert_remounts_and_syncs_pending_frame` | Directly reconnecting the simulated worker while RAM is preserved writes the pending frame and restores a correct READ response; this is not the production menu recovery path. |
| `test_pipeline_write_during_outage.c` — `test_pipeline_console_writes_during_sd_outage_sync_after_reconnect` | Models the pre-detection window by keeping logical card presence enabled after physical storage becomes unavailable; those WRITEs remain in RAM and are synchronized after direct worker reconnect. |
| `test_pipeline_sd_write_failure.c` — `test_pipeline_write_failure_retries_dirty_frame_after_sd_recovers` | A write failure leaves the frame dirty; after the SD card recovers, the frame is retried and confirmed. |
| `test_pipeline_image_swap.c` — `test_pipeline_image_swap_flushes_a_and_exposes_only_b_after_delay` | With the UNIT_TEST pause auto-acknowledged, switching from image A to B flushes A, hides the card for the required delay/probe period, and exposes only B afterward. |
| `test_pipeline_sd_removal_during_swap.c` — `test_pipeline_sd_removal_during_swap_never_exposes_partial_image` | In the simulated lifecycle, removal during an A → B swap never exposes a partial image; retrying completes with a consistent B image. |

## What the host tests do not cover

- execution and timing of `ps1_card_bus.pio` on RP2350 hardware,
- real dual-core scheduling, IRQ masking, XIP contention, or GPIO electrical behavior,
- the production `menu_task_run()` storage-removal and reload path,
- the real FatFs, SPI microSD, OLED, buttons, or custom PCB,
- power-loss atomicity, storage endurance, or compatibility across PlayStation models.

The evidence status and the remaining physical test plan are summarized in
[`../../docs/verification.md`](../../docs/verification.md).
