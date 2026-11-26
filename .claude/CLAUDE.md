# Catatan mesin lokal (laptop HP EliteBook 2570p)

## Build: SELALU `-j2`, jangan `-j$(nproc)` atau `-j8`

Laptop ini cuma punya 4 thread (i5-3230M) dan RAM 7,6 GB. Tiap proses compile
C++ JUCE bisa makan 0,5–1 GB. Dengan `-j8` satu tab Konsole pernah tembus 5,4 GB,
lalu **systemd-oomd membunuh seluruh tab** (zsh + sesi Claude Code + build).
Konsole lalu menampilkan "Warning: Program '/usr/bin/zsh' crashed." Ini terjadi
berkali-kali pada 23–24 Nov 2026.

```bash
cmake --build build-debug --target djr_studio djr_studio_engine_tests -j2
```

- Paling banyak `-j3`, dan itu hanya kalau tidak ada aplikasi berat lain yang terbuka.
- Jangan menjalankan dua build bersamaan (mis. di worktree lain) di saat yang sama.
- Build yang lama tetap dijalankan di background dan ditunggu notifikasinya.
- Kalau sesi tiba-tiba mati, cek dulu: `journalctl -u systemd-oomd | grep Killed`.

## ccache (dipasang 2026-11-24)

`build-debug` dan `build-release` sudah memakai ccache. Cache disimpan di
`/mnt/291fb983-71a6-4e03-885f-ee6e24bce244/ccache` (maks 8 GB, setelan di
`~/.config/ccache/ccache.conf`), BUKAN di `~/.cache`, karena disk `/` hampir penuh.
Kalau membuat folder build baru (mis. di worktree), konfigurasi dengan:

```bash
cmake -S . -B build-debug -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
```

Cek apakah cache bekerja: `ccache -s` (lihat Hits/Misses).
