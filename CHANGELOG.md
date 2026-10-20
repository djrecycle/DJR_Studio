# Changelog

Semua perubahan penting ke DJR_Studio dicatat di sini. Format mengikuti
gaya [Keep a Changelog](https://keepachangelog.com/), versi mengikuti
[Semantic Versioning](https://semver.org/).

## [v1.2.0] - 2026-10-20

### Ditambahkan
- **Quantize di piano roll** — tombol **Quantize ▾** dengan pilihan grid
  (ikuti snap, 1/16, 1/8, 1 ketukan, 1/2 bar, 1 bar); **Ctrl+Q** mengulang
  grid terakhir. Hanya not terpilih, atau semua not kalau tidak ada yang dipilih.
- **Export lebih lengkap** — WAV, FLAC, atau AIFF; 16-bit, 24-bit, atau
  32-bit float (WAV); opsi normalisasi ke puncak -0.3 dBFS. Pilihan terakhir diingat.
- **DJR MIDI Sampler** — instrumen sampler bawaan multi-pad, otomatis dimuat
  ke setiap track MIDI kosong, dengan starter kit sintetis dan mode keyboard
  (satu sample dimainkan di seluruh piano roll).
- **Track Bass dan Pad bawaan** punya suara preview-synth sendiri.
- **Panel External Apps** — meluncurkan aplikasi JACK mandiri (misalnya Carla)
  sebagai proses terpisah dan menyambungkan outputnya ke input DJR_Studio.
- **Dukungan plugin LADSPA** di Linux (Calf, CAPS, CMT, swh-plugins, dan
  salinan bawaan LMMS).
- **Jendela plugin terpisah** — editor plugin bisa dibuka sebagai jendela
  sendiri, diingat per plugin.
- **Bypass per slot** di insert chain.
- **DJR Audio Editor** otomatis ditambahkan ke setiap track audio baru.
- **Splash screen** dengan logo saat aplikasi dibuka, ikon aplikasi baru, dan
  watermark logo di grid playlist. Tema bawaan bergeser ke biru navy.

### Diperbaiki
- Di KDE Plasma (Wayland) dengan layar kecil, jendela utama kehilangan title
  bar dan baris menu atas tertutup panel. Sekarang jendela di-maximise lewat
  window manager.
- Aplikasi bisa hang saat memuat ulang plugin LV2 berbasis GTK.
- Editor plugin yang tidak pernah mendapat ukuran tidak lagi menghasilkan jendela kosong.
- Halaman generator yang kosong sekarang menjelaskan sebabnya.
- Scan plugin pulih dari plugin yang crash, dan menandai plugin LV2 yang
  memakai kontrol file-path yang tidak bisa dibaca.
- Plugin dengan kanal lebih sempit dari track stereo di-downmix dengan benar.
- Plugin dengan terlalu banyak kanal diberi peringatan, tidak dilewati diam-diam.
- Use-after-free di wrapper plugin, serta hapus/urutkan ulang insert per slot.
- DJR Audio Editor tidak lagi mencatat edit yang tidak mengubah apa pun.

### Lainnya
- CI memasang `ladspa-sdk`, yang dibutuhkan sejak dukungan LADSPA.

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
