---
name: mrgr
description: Merge Pull Request DJR_Studio ke master (merge commit, seperti PR sebelumnya) setelah CI build-and-test lolos, lalu sinkronkan master lokal dan catatan memory. Argumen opsional - nomor PR, mis. "/mrgr 33".
disable-model-invocation: true
---

# /mrgr - merge PR (khusus DJR_Studio)

User mengetik `/mrgr` = izin eksplisit untuk merge PR ke `master` di
`djrecycle/DJR_Studio`. Balas user dalam Bahasa Indonesia.

## 1. Tentukan PR

- Ada argumen nomor (`/mrgr 33`) → pakai itu.
- Tanpa argumen → PR milik branch aktif di checkout kerja sesi ini (repo utama
  `/home/rraangga/projects/DJR_Studio` atau worktree
  `/mnt/291fb983-71a6-4e03-885f-ee6e24bce244/DJR_Studio-wt`):
  `gh pr view <branch> --json number,title,state,url`.
- Tidak ketemu, atau ada beberapa PR terbuka (`gh pr list --state open`) dan tidak
  jelas yang mana → tampilkan daftarnya dan tanya user. Jangan menebak.

## 2. Cek kelayakan

```bash
gh pr view <N> --json number,title,state,isDraft,mergeable,mergeStateStatus,baseRefName,headRefName,body,url
gh pr checks <N>
```

Berhenti dan laporkan (jangan merge) kalau:
- `state` bukan `OPEN`, atau `baseRefName` bukan `master`;
- `isDraft` true, atau judul/body/commit mengandung "WIP" / "not ready for review";
- `mergeable` = `CONFLICTING`. Jangan selesaikan konflik sendiri kecuali user minta;
- ada check yang gagal (`fail`). Tampilkan nama check + link log-nya.

Kalau check `build-and-test` masih `pending`, tunggu dengan
`gh pr checks <N> --watch` di background (biasanya ~6 menit) lalu cek lagi.

Kalau ada perubahan lokal di branch PR yang belum di-push / belum di-commit,
beri tahu user dulu - yang di-merge hanya yang sudah ada di GitHub.

## 3. Status tes manual

Baca bagian `## Verifikasi` di body PR. Kalau tertulis belum dites manual oleh
user dan pesan user tidak menyebut sudah dites, tanyakan sekali dengan
AskUserQuestion: "Sudah dites manual dan berhasil?" dengan pilihan
- "Sudah, berhasil - merge" → tambahkan baris `- Dites manual oleh user: berhasil.`
  di Verifikasi, lalu merge. JANGAN pakai `gh pr edit` - di repo ini gagal
  ("Projects (classic) is being deprecated"). Ambil body dengan
  `gh pr view <N> --json body -q .body > file`, ubah, lalu
  `gh api -X PATCH repos/djrecycle/DJR_Studio/pulls/<N> -F body=@file`;
- "Merge saja tanpa tes" → merge, body tidak diubah;
- "Batal" → berhenti.

## 4. Merge

Pakai merge commit, sama seperti PR #1-#32 ("Merge pull request #N from ..."):

```bash
gh pr merge <N> --merge
```

Jangan `--squash`/`--rebase`, jangan `--admin`, jangan `--delete-branch` (branch
remote di repo ini memang dibiarkan). Kalau merge ditolak, laporkan pesannya.

## 5. Sinkronkan lokal

```bash
git fetch origin
git worktree list   # cek apakah master sedang di-checkout di salah satu checkout
```

- Kalau `master` tidak sedang di-checkout di mana pun:
  `git fetch origin master:master` (fast-forward ref master lokal).
- Kalau sedang di-checkout dan bersih: `git -C <checkout itu> pull --ff-only`.
- Jangan pindah branch di checkout mana pun dan jangan hapus branch lokal tanpa
  diminta. Cukup sebutkan bahwa branch fitur sudah selesai dan kerja berikutnya
  sebaiknya dimulai dari `master` terbaru.

## 6. Memory dan laporan

- Perbarui catatan memory proyek yang menyebut PR ini
  (`~/.claude/projects/-mnt-291fb983-71a6-4e03-885f-ee6e24bce244-DJR-Studio/memory/`,
  cari `PR #<N>` atau nama branch): ubah status jadi MERGED + hash merge commit +
  tanggal, termasuk baris pointer-nya di `MEMORY.md`.
- Laporan singkat: PR #N + judul, hash merge commit di master, status CI, apakah
  master lokal sudah ikut maju. Ingatkan: aplikasi terpasang di `/usr/bin/djr_studio`
  tidak ikut berubah sampai di-build dan di-install ulang.
