---
name: cm
description: Commit perubahan DJR_Studio yang sedang dikerjakan - cek lokasi kerja (repo utama atau worktree DJR_Studio-wt), buat branch kalau masih di master, build + jalankan test, lalu commit dengan gaya pesan proyek ini.
disable-model-invocation: true
---

# /cm - commit (khusus DJR_Studio)

User mengetik `/cm` = izin eksplisit untuk commit. JANGAN push dan JANGAN buat PR
(itu `/pr`). Balas user dalam Bahasa Indonesia.

## 1. Tentukan repo yang berisi perubahan

Ada dua checkout:
- repo utama: `/home/rraangga/projects/DJR_Studio`
- worktree kerja: `/mnt/291fb983-71a6-4e03-885f-ee6e24bce244/DJR_Studio-wt`
  (tempat sebagian besar fitur dikerjakan)

Jalankan `git status --short` dan `git branch --show-current` di keduanya. Pakai
yang punya perubahan dari pekerjaan di sesi ini. Kalau keduanya punya perubahan
dan tidak jelas yang mana, tanya user. Abaikan `.claude/` (selalu untracked).

## 2. Branch

Kalau branch aktif `master`, buat branch dulu dari `origin/master` (sesudah
`git fetch`), dengan pola nama yang sudah dipakai:
`feat/<topik>`, `fix/<topik>`, `ci/<topik>`, `docs/<topik>` (kebab-case, Inggris).

## 3. Periksa isi perubahan

- Baca `git diff` dan `git diff --stat` (plus file baru di `git status`).
- Kalau ada patch debug/sementara yang tertinggal (env var `DJR_TMP_*`,
  `std::cout` diagnostik baru, `DBG` sisa percobaan), berhenti dan tanyakan ke user.
- Stage file BY NAME (`git add <file>...`), jangan `git add -A` / `git add .`.
- Jangan pernah stage: `.claude/`, `build*/`, `*.png` di root repo (`lg1.png` dll),
  `*.djrs`, `*.wav`, `settings.local.json`, atau file yang kelihatan berisi rahasia.

## 4. Build + test sebelum commit

Kalau ada file C++/CMake yang berubah, di checkout yang sama:

```bash
cmake --build build-debug --target djr_studio djr_studio_engine_tests -j2   # jangan lebih: RAM 7,6 GB, lihat .claude/CLAUDE.md
build-debug/tests/djr_studio_engine_tests_artefacts/Debug/djr_studio_engine_tests
```

Build bisa lebih dari 10 menit kalau banyak yang berubah - jalankan di background
dan tunggu notifikasinya. Kalau build gagal atau ada `FAIL`, JANGAN commit:
laporkan output yang gagal ke user dan berhenti. Kalau hanya dokumen/markdown
yang berubah, lewati langkah ini dan sebutkan itu di laporan.

## 5. Pesan commit

Ikuti gaya `git log` proyek ini:
- Baris pertama: Bahasa Inggris, kalimat perintah, tanpa prefix `feat:`/`fix:`,
  maks ~72 karakter. Contoh asli: `Pin an LV2 plugin's shared libraries open, to stop a GTK reload hang`.
- Baris kosong, lalu body Bahasa Inggris dibungkus ~75 kolom: apa masalahnya,
  kenapa terjadi, apa yang diubah. Bukan daftar file.
- Akhiri dengan baris attribution dari system reminder sesi ini (saat skill ini
  dibuat: `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`).

Commit lewat HEREDOC:

```bash
git commit -F - <<'EOF'
<summary>

<body>

Co-Authored-By: ...
EOF
```

Jangan `--amend`, `--no-verify`, atau rebase kecuali user memintanya.

## 6. Laporan

Singkat: repo/branch, hash pendek + judul commit, hasil build/test (jumlah lolos),
dan file yang sengaja TIDAK di-commit kalau ada.
