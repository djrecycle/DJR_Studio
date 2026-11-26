---
name: cmpr
description: Commit lalu buat Pull Request DJR_Studio sekaligus - jalankan langkah /cm lalu /pr.
disable-model-invocation: true
---

# /cmpr - commit + PR (khusus DJR_Studio)

User mengetik `/cmpr` = izin eksplisit untuk commit, push, dan membuat PR sekaligus.
JANGAN merge. Balas user dalam Bahasa Indonesia.

1. Baca dan jalankan semua langkah di
   `/home/rraangga/projects/DJR_Studio/.claude/skills/cm/SKILL.md`.
2. Kalau commit tidak terjadi (build/test gagal, ada patch sementara, atau tidak
   ada perubahan), BERHENTI di situ - jangan lanjut ke PR. Laporkan alasannya.
3. Kalau commit berhasil, baca dan jalankan semua langkah di
   `/home/rraangga/projects/DJR_Studio/.claude/skills/pr/SKILL.md`.
   Langkah "masih ada perubahan yang belum di-commit" boleh dilewati kalau sisa
   perubahannya hanya file yang memang sengaja tidak di-commit di langkah 1.
4. Laporan gabungan, singkat: hash + judul commit, hasil test, URL PR.
