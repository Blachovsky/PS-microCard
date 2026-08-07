# Testy hostowe firmware

Ten katalog zawiera testy uruchamiane na komputerze za pomocą Ceedling 1.1.3,
Unity 2.7.1 i CMock 2.7.0. Firmware produkcyjny nadal jest budowany przez
CMake z katalogu nadrzędnego.

Obecny zestaw obejmuje:

- 61 testów jednostkowych,
- 14 testów integracyjnych pipeline,
- łącznie 75 testów.

## Instalacja i uruchamianie

Zależności Ruby są przypięte w `Gemfile.lock`. Należy zainstalować je raz,
wykonując w tym katalogu:

```powershell
bundle install
```

Uruchamianie wybranych grup:

```powershell
# Tylko testy jednostkowe
.\run_tests.cmd "test:path[unit]"

# Tylko testy integracyjne
.\run_tests.cmd "test:path[integration]"

# Wszystkie testy
.\run_tests.cmd test:all
```

Pojedynczy plik można uruchomić zadaniem `test:nazwa_pliku`, na przykład:

```powershell
.\run_tests.cmd test:test_micro_sd_worker
```

Artefakty są zapisywane w `tests/build/` i ignorowane przez Git.

## Struktura

```text
tests/test/
|-- unit/                 testy pojedynczych modułów
|-- integration/          testy przepływu przez kilka modułów
|   `-- support/          środowisko używane tylko przez pipeline
`-- support/              wspólne zamienniki Pico SDK, FatFs i sprzętu
```

Pliki w `test/support/` są używane przez obie grupy. Udostępniają wyłącznie
minimalne interfejsy platformy potrzebne na hoście. Kod produkcyjny jest
kompilowany dla urządzenia z prawdziwym Pico SDK i FatFs.

Testy integracyjne kompilują razem rzeczywiste moduły `ps1_card_bus`,
`ps1_card_emulator`, `micro_sd_image` i `micro_sd_worker`. Symulowane pozostają
granice sprzętowe, czas oraz system plików. Model pliku rozróżnia dane zapisane
przez `f_write` od danych utrwalonych dopiero przez poprawny `f_sync`.

## Testy jednostkowe

### `test_ps1_card_emulator.c`

| Test | Co sprawdza |
|---|---|
| `test_get_frame_ptr_returns_frame_start_for_valid_address` | Zwrócenie początku pierwszej i ostatniej poprawnej ramki w RAM. |
| `test_get_frame_ptr_rejects_address_outside_card` | Odrzucenie adresu 1024 i adresów spoza obrazu karty. |
| `test_frame_checksum_combines_address_bytes_and_frame_data` | Obliczenie XOR z obu bajtów adresu i wszystkich 128 bajtów ramki. |
| `test_commit_frame_rejects_invalid_arguments` | Odrzucenie adresu poza zakresem i wskaźnika danych `NULL`. |
| `test_take_changed_frame_rejects_invalid_arguments` | Walidację wszystkich wskaźników wyjściowych funkcji `take`. |
| `test_commit_and_take_changed_frame_returns_stable_copy` | Zapis znanych 128 bajtów i pobranie identycznej, stabilnej kopii. |
| `test_commits_to_first_middle_and_last_frames_are_isolated_and_pending` | Izolację zapisów ramek 0, 10, 100 i 1023 oraz ich obecność w kolejce zmian. |
| `test_two_identical_commits_still_publish_latest_version` | Dwa identyczne zapisy nadal publikują najnowszą wersję ramki. |
| `test_version_counter_wraps_through_uint32_max_to_zero` | Przejście licznika wersji przez `UINT32_MAX` do zera. |
| `test_second_commit_before_take_returns_only_latest_frame_version` | Kilka zapisów przed `take` zwraca wyłącznie najnowsze dane i wersję. |
| `test_rollback_requeues_frame_that_was_not_confirmed` | `rollback` ponownie udostępnia pobraną, ale niepotwierdzoną ramkę. |
| `test_rollback_does_not_requeue_confirmed_frame` | Ramka po `take` i `confirm` nie wraca do kolejki. |

### `test_ps1_card_bus.c`

| Test | Co sprawdza |
|---|---|
| `test_read_0x52_returns_128_bytes_xor_checksum_and_good_result` | Pełną odpowiedź READ `0x52`, 128 bajtów, XOR i kod sukcesu. |
| `test_status_0x53_returns_complete_status_response` | Kompletną odpowiedź komendy STATUS `0x53`. |
| `test_write_0x57_commits_exactly_128_bytes_and_returns_good_result` | WRITE `0x57`, dokładnie 128 bajtów, commit do RAM i ACK sukcesu. |
| `test_read_accepts_first_and_last_frame_addresses` | READ ramek granicznych 0 i 1023. |
| `test_write_accepts_first_and_last_frame_addresses` | WRITE ramek granicznych 0 i 1023. |
| `test_out_of_range_read_returns_ff_data_checksum_and_bad_sector` | Bezpieczną odpowiedź READ dla adresu poza zakresem. |
| `test_out_of_range_write_returns_bad_sector_without_changing_ram` | Odrzucenie niepoprawnego WRITE bez zmiany RAM. |
| `test_write_with_bad_checksum_returns_error_without_changing_ram` | Odrzucenie WRITE z błędnym XOR bez częściowego commitu. |
| `test_unknown_commands_0x00_and_0xff_are_ignored` | Ignorowanie nieznanych komend `0x00` i `0xFF`. |
| `test_access_byte_other_than_0x81_is_rejected_without_ack` | Odrzucenie urządzenia innego niż `0x81` bez ACK. |
| `test_cs_abort_at_every_write_byte_stops_at_that_byte` | Przerwanie WRITE po podniesieniu CS na każdym możliwym etapie. |
| `test_cs_abort_at_every_read_and_status_byte_stops_at_that_byte` | Przerwanie READ i STATUS po podniesieniu CS na każdym bajcie. |
| `test_clock_timeout_during_write_data_prevents_commit` | Timeout w połowie danych WRITE nie może zmienić RAM. |
| `test_hardware_xfer_generates_ack_pulse_and_transfers_lsb_first` | Kolejność bitów LSB-first i wygenerowanie impulsu ACK. |
| `test_hardware_xfer_aborts_when_cs_rises_at_each_bit` | Reakcję transportu na podniesienie CS przy każdym bicie. |
| `test_hardware_xfer_aborts_if_cs_rises_while_waiting_for_clock` | Przerwanie podczas oczekiwania na następne zbocze zegara. |
| `test_hardware_xfer_times_out_when_no_clock_edge_arrives` | Timeout przy całkowitym braku kolejnego zbocza zegara. |
| `test_hardware_xfer_accepts_very_slow_edges_before_timeout` | Akceptację bardzo wolnej transmisji mieszczącej się w limicie. |
| `test_hardware_xfer_rejects_edge_at_exact_timeout_boundary` | Zachowanie dokładnie na granicy timeoutu. |

### `test_micro_sd_worker.c`

| Test | Co sprawdza |
|---|---|
| `test_flush_writes_one_frame_at_its_offset_then_syncs_closes_and_confirms` | Zapis jednej ramki pod właściwy offset oraz kolejność sync, close i confirm. |
| `test_flush_writes_several_frames_and_confirms_each_after_single_sync` | Zapis kilku ramek i potwierdzenie ich dopiero po wspólnym `f_sync`. |
| `test_poll_syncs_only_after_250_ms_idle_delay` | Synchronizację dokładnie po 250 ms bezczynności. |
| `test_flush_does_not_open_or_write_when_no_frame_changed` | Brak operacji FatFs, gdy nie ma zmian. |
| `test_open_error_rolls_back_without_confirming` | Rollback i brak confirm po błędzie `f_open`. |
| `test_seek_error_closes_file_and_rolls_back_without_confirming` | Zamknięcie pliku i rollback po błędzie `f_lseek`. |
| `test_write_error_rolls_back_without_confirming` | Rollback i brak confirm po błędzie `f_write`. |
| `test_short_write_of_127_bytes_is_an_error_and_is_not_confirmed` | Krótki zapis 127 B jest błędem i nie potwierdza ramki. |
| `test_sync_error_closes_file_and_does_not_confirm_written_frame` | Błąd `f_sync` nie potwierdza danych zapisanych przez `f_write`. |
| `test_close_error_does_not_confirm_synced_frame` | Błąd `f_close` blokuje confirm mimo wcześniejszego sync. |
| `test_no_space_error_from_write_is_recovered_without_confirming` | Obsługę braku miejsca bez utraty ramki z kolejki. |
| `test_read_only_card_error_from_open_is_recovered_without_confirming` | Obsługę karty tylko do odczytu podczas otwierania obrazu. |
| `test_card_removed_before_open_prevents_any_write` | Wyjęcie SD przed `f_open` nie rozpoczyna zapisu. |
| `test_card_removed_between_open_and_write_prevents_commit` | Wyjęcie SD po otwarciu, ale przed zapisem, nie powoduje confirm. |
| `test_card_removed_between_write_and_sync_rolls_back_pending_frame` | Wyjęcie SD po `f_write`, ale przed `f_sync`, wykonuje rollback. |
| `test_card_removed_during_close_does_not_confirm_frame` | Wyjęcie SD podczas `f_close` pozostawia ramkę niepotwierdzoną. |
| `test_failed_write_is_retried_after_card_reconnect_and_then_confirmed` | Ponowny zapis jednej ramki po powrocie SD. |
| `test_reconnect_retries_several_frames_left_pending_by_failure` | Ponowienie kilku ramek pozostawionych przez awarię. |
| `test_failure_after_partial_batch_replays_all_unconfirmed_frames` | Awarię w środku serii i ponowny zapis wszystkich niepotwierdzonych ramek. |

### `test_micro_sd_image.c`

| Test | Co sprawdza |
|---|---|
| `test_create_new_mcr_has_exact_size_header_directory_and_checksums` | Utworzenie MCR 131072 B z nagłówkiem, katalogiem i checksumami. |
| `test_image_size_boundaries_accept_only_131072_bytes` | Odrzucenie rozmiarów 0, 1, 131071, 131073 i dużych plików. |
| `test_load_existing_valid_image_copies_all_bytes_to_card_ram` | Załadowanie całego poprawnego obrazu do RAM. |
| `test_all_zero_and_all_ff_images_are_formatted_as_blank_cards` | Automatyczne formatowanie obrazów wypełnionych `0x00` lub `0xFF`. |
| `test_invalid_header_directory_state_and_directory_checksum_are_rejected` | Odrzucenie błędnego nagłówka, wpisu katalogu i checksumy. |
| `test_deleted_directory_entry_states_are_accepted` | Akceptację prawidłowych stanów usuniętych wpisów katalogowych. |
| `test_list_images_accepts_mcr_case_variants_and_filters_other_files` | Rozszerzenia `.MCR`, `.mcr`, `.McR` oraz filtrowanie innych plików. |
| `test_delete_image_flushes_storage_and_removes_file` | Flush przed usunięciem obrazu i poprawne `f_unlink`. |
| `test_create_auto_uses_first_free_card_number` | Wybór pierwszej wolnej nazwy `CARDxxx.MCR`. |
| `test_list_images_stops_at_capacity_when_more_images_exist` | Ograniczenie listy do pojemności tablicy wynikowej. |
| `test_create_auto_reports_error_when_card000_through_card999_are_taken` | Błąd po zajęciu wszystkich nazw `CARD000`–`CARD999`. |

## Testy integracyjne pipeline

Każdy scenariusz znajduje się w osobnym pliku `test_pipeline_*.c` i jest
budowany jako osobny program testowy.

| Plik i test | Co sprawdza |
|---|---|
| `test_pipeline_normal_write.c` — `test_pipeline_normal_write_is_synced_confirmed_and_persisted` | Pełny WRITE przez magistralę, RAM, dokładnie 128 B pod właściwym offsetem MCR, `f_sync`, confirm i końcowy READ. |
| `test_pipeline_immediate_read_before_sd.c` — `test_pipeline_immediate_read_uses_ram_before_sd_worker_runs` | READ natychmiast po WRITE widzi nowe dane w RAM, mimo że worker nie zmienił jeszcze MCR. |
| `test_pipeline_persistence_after_restart.c` — `test_pipeline_synced_write_survives_complete_firmware_restart` | Dane utrwalone przez `f_sync` przeżywają restart i ponowne załadowanie obrazu. |
| `test_pipeline_restart_before_sync.c` — `test_pipeline_restart_before_sync_uses_only_last_confirmed_data` | Restart po `f_write`, ale przed `f_sync`, nie uznaje buforowanych danych za trwałe; rollback ujawnia brak confirm. |
| `test_pipeline_same_frame_twice.c` — `test_pipeline_second_write_to_same_frame_wins_before_sync` | Drugi WRITE tej samej ramki przed sync wygrywa w RAM i MCR. |
| `test_pipeline_slow_sd_multiple_writes.c` — `test_pipeline_slow_sd_converges_to_latest_ram_state_per_frame` | Przy wolnym `f_write` i `f_sync` wiele przeplatanych WRITE kończy się najnowszym stanem ramek 5, 100 i 500. |
| `test_pipeline_partial_batch_write_failure.c` — `test_pipeline_failure_on_third_frame_recovers_entire_batch` | Błąd trzeciego `f_write` nie gubi ramek 12 i 13, a po odzyskaniu cały obraz odpowiada RAM. |
| `test_pipeline_sync_failure.c` — `test_pipeline_sync_failure_keeps_written_frame_unconfirmed` | Poprawne `f_write` z błędnym `f_sync` pozostawia ramkę niepotwierdzoną i umożliwia ponowienie. |
| `test_pipeline_sd_removal_during_write.c` — `test_pipeline_sd_removal_during_write_disconnects_ps1_safely` | Wyjęcie SD w połowie ramki nie tworzy trwałego częściowego save'a i kontrolowanie odłącza kartę PS1. |
| `test_pipeline_sd_reinsert_after_failure.c` — `test_pipeline_sd_reinsert_remounts_and_syncs_pending_frame` | Ponowne włożenie SD montuje storage, zapisuje zaległą ramkę i przywraca poprawny READ. |
| `test_pipeline_write_during_outage.c` — `test_pipeline_console_writes_during_sd_outage_sync_after_reconnect` | WRITE wykonane podczas niedostępności SD pozostają w RAM i są synchronizowane po powrocie storage. |
| `test_pipeline_sd_write_failure.c` — `test_pipeline_write_failure_retries_dirty_frame_after_sd_recovers` | Błąd zapisu pozostawia dirty frame, która po powrocie SD jest ponawiana i potwierdzana. |
| `test_pipeline_image_swap.c` — `test_pipeline_image_swap_flushes_a_and_exposes_only_b_after_delay` | Zmiana A na B zapisuje A, ukrywa kartę na wymagany czas/probe i udostępnia wyłącznie dane B. |
| `test_pipeline_sd_removal_during_swap.c` — `test_pipeline_sd_removal_during_swap_never_exposes_partial_image` | Wyjęcie SD w połowie A → B nie udostępnia częściowego obrazu, a ponowienie kończy się spójnym B. |
