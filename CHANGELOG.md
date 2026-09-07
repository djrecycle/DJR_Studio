# Changelog

Semua perubahan penting ke DJR_Studio dicatat di sini. Format mengikuti
gaya [Keep a Changelog](https://keepachangelog.com/), versi mengikuti
[Semantic Versioning](https://semver.org/).

## [v1.1.0] - 2026-09-07

### Ditambahkan
- **Piano roll articulation tools** (lanjutan roadmap workflow ala FL Studio):
  - **Strum** (Ctrl+J) — nada terpilih disebar berurutan dari pitch terendah,
    seperti gitar dipetik.
  - **Flam** (Ctrl+F) — tiap nada terpilih dapat grace note pelan tepat
    sebelumnya.
  - **Arpeggiate** (Ctrl+K) — chord yang stack disusun ulang jadi berurutan
    naik, permanen di data not (beda dari arpeggiator live di ChannelSettings).
- **Sticky draw length** — klik atau resize sebuah nada di piano roll bikin
  panjang itu jadi default nada berikutnya yang digambar, alih-alih selalu
  balik ke default berdasar snap.

## [v1.0.0] - 2026-09-04

Rilis stabil pertama (sebelumnya beta).

### Ditambahkan
- Piano roll workflow tools ala FL Studio: scale/key highlighting, chord
  stamp (grouping, toggle on/off, menu terkategori).
- Tombol reset instrumen preview ke waveform + envelope default.
- CI otomatis (build + ctest) lewat GitHub Actions tiap push/PR.

### Diubah
- Status rilis dari beta ke stabil; versi tidak lagi memakai suffix `-beta`.

## [v0.2.1-beta] - 2026-08-19

### Diperbaiki
- Keymap ketik: `R` kembali jadi nada F, record dipindah ke Ctrl+R; mapping
  huruf bawaan `MidiKeyboardComponent` dimatikan supaya satu tombol tidak
  membunyikan dua nada; Ctrl/Cmd/Alt + huruf kembali murni shortcut.
- Pesan MIDI live distempel waktu sebelum masuk collector.
- Halaman Audio Device jadi default di Preferences, dengan input level
  meter supaya sumber rekaman bisa dicek sebelum tombol record ditekan.
- Meter mixer dan master dibaca dalam desibel (-60..0 dBFS), bukan
  amplitudo linier — sebelumnya sinyal normal nyaris tidak menggerakkan
  meter.

## [v0.2.0-beta] - 2026-08-12

Rilis beta pertama.

### Ditambahkan
- Playlist dan piano roll dengan tool ala FL Studio, mixer, host VST3,
  recording audio/MIDI, undo/redo, time signature 4/4 sampai 12/8,
  metronome dengan count-in, dan export WAV.

[v1.1.0]: https://github.com/djrecycle/DJR_Studio/releases/tag/v1.1.0
[v1.0.0]: https://github.com/djrecycle/DJR_Studio/releases/tag/v1.0.0
[v0.2.1-beta]: https://github.com/djrecycle/DJR_Studio/releases/tag/v0.2.1-beta
[v0.2.0-beta]: https://github.com/djrecycle/DJR_Studio/releases/tag/v0.2.0-beta
