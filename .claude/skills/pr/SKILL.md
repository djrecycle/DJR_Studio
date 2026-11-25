---
name: pr
description: Push branch DJR_Studio yang aktif dan buat Pull Request ke master di GitHub (djrecycle/DJR_Studio) dengan body berbahasa Indonesia (Masalah / Perbaikan / Verifikasi).
disable-model-invocation: true
---

# /pr - push + Pull Request (khusus DJR_Studio)

User mengetik `/pr` = izin eksplisit untuk push dan membuat PR. JANGAN merge PR.
Balas user dalam Bahasa Indonesia.

## 1. Repo dan branch

Pakai checkout yang sama dengan pekerjaan sesi ini (repo utama
`/home/rraangga/projects/DJR_Studio` atau worktree
`/mnt/291fb983-71a6-4e03-885f-ee6e24bce244/DJR_Studio-wt`; cek `git status` keduanya
kalau tidak jelas).

- Kalau branch aktif `master`: berhenti, jangan push ke master. Sarankan `/cm` dulu
  (yang akan membuat branch).
- Kalau masih ada perubahan yang belum di-commit (selain `.claude/`): beri tahu user
  dan tanyakan apakah mau di-commit dulu (`/cm`) atau PR tanpa perubahan itu.
- `git fetch` lalu `git log --oneline origin/master..HEAD`. Kalau kosong, tidak
  ada yang perlu di-PR - laporkan dan berhenti.

## 2. Push

```bash
git push -u origin <branch>
```

Kalau push ditolak (non-fast-forward), jangan force-push; laporkan ke user.

## 3. Sudah ada PR?

`gh pr view <branch> --json number,url,state`. Kalau sudah ada PR yang OPEN untuk
branch ini, push di atas sudah cukup memperbaruinya - laporkan URL-nya, jangan
buat PR kedua.

## 4. Buat PR

- Base: `master`.
- Judul: Bahasa Inggris, dari judul commit (kalau beberapa commit, satu kalimat
  yang merangkum semuanya).
- Body Bahasa Indonesia, mengikuti PR sebelumnya (mis. #31):

```markdown
## Masalah
<apa yang dialami user / kenapa perlu, termasuk root cause kalau ini fix>

## Perbaikan
- <perubahan utama, dengan nama file/kelas penting dalam `backtick`>

## Verifikasi
- <test yang dijalankan + hasilnya, mis. "engine tests: semua lolos (N test baru)">
- <cek visual/manual yang benar-benar dilakukan>
- <yang BELUM dites, ditulis jujur, mis. "belum dites manual oleh user">

🤖 Generated with [Claude Code](https://claude.com/claude-code)
```

Isi bagian Verifikasi hanya dengan yang benar-benar dilakukan di sesi ini. Jangan
tulis "dites manual oleh user: berhasil" kecuali user memang mengatakannya.

```bash
gh pr create --base master --title "<judul>" --body-file - <<'EOF'
<body>
EOF
```

## 5. Laporan

URL PR, nomornya, dan jumlah commit di dalamnya. Kalau ada catatan memory proyek
yang terkait fitur ini (`~/.claude/projects/-mnt-291fb983-71a6-4e03-885f-ee6e24bce244-DJR-Studio/memory/`),
perbarui statusnya dengan nomor PR.
