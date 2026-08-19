#include "Localisation.h"

#include "Settings.h"

namespace djr
{

namespace
{
    Localisation::Language currentLanguage = Localisation::Language::english;
}

void Localisation::setLanguage(Language language)
{
    currentLanguage = language;

    if (language == Language::english)
    {
        // No table at all: TRANS hands back the source string, which is the
        // English one. Nothing to keep in step, nothing to forget to add.
        juce::LocalisedStrings::setCurrentMappings(nullptr);
        return;
    }

    auto* mappings = new juce::LocalisedStrings(getIndonesianTranslations(), false);
    juce::LocalisedStrings::setCurrentMappings(mappings);
}

Localisation::Language Localisation::getLanguage() noexcept
{
    return currentLanguage;
}

juce::String Localisation::getLanguageName(Language language)
{
    return language == Language::indonesian ? "Bahasa Indonesia" : "English";
}

juce::Array<Localisation::Language> Localisation::getAvailableLanguages()
{
    return { Language::english, Language::indonesian };
}

Localisation::Language Localisation::languageFromString(const juce::String& value)
{
    return value.equalsIgnoreCase("id") ? Language::indonesian : Language::english;
}

juce::String Localisation::languageToString(Language language)
{
    return language == Language::indonesian ? "id" : "en";
}

void Localisation::saveChoice(Language language)
{
    // Through the shared store rather than writing the file directly: the
    // plugin search paths live in the same file, and a write that carried only
    // the language used to take them with it.
    Settings::set("language", languageToString(language));
}

Localisation::Language Localisation::loadSavedChoice()
{
    return languageFromString(Settings::get("language", "en"));
}

juce::String Localisation::getIndonesianTranslations()
{
    // Generated from one map so the table and the sources cannot drift apart.
    return juce::String("language: Indonesian\n")
        + "countries: id\n"
        + "\"Warp to tempo\" = \"Warp ikut tempo\"\n"
        + "\"Warp mode\" = \"Mode warp\"\n"
        + "\"Resample (pitch follows tempo)\" = \"Resample (pitch ikut tempo)\"\n"
        + "\"Stretch (keep pitch)\" = \"Stretch (pitch tetap)\"\n"
        + "\"Edit clip\" = \"Ubah clip\"\n"
        + "\"Change warp mode\" = \"Ubah mode warp\"\n"
        + "\"Change fade\" = \"Ubah fade\"\n"
        + "\"Mute clip\" = \"Mute clip\"\n"
        + "\"Slice clip\" = \"Potong clip\"\n"
        + "\"Slip clip\" = \"Slip clip\"\n"
        + "\"Fade in\" = \"Fade in\"\n"
        + "\"Fade out\" = \"Fade out\"\n"
        + "\"Off\" = \"Mati\"\n"
        + "\"Count-in cancelled.\" = \"Count-in dibatalkan.\"\n"
        + "\"Linux DAW - still in beta\" = \"Linux DAW - masih beta\"\n"
        + "\" s) landed on \" = \" s) masuk ke \"\n"
        + "\" at beat \" = \" di beat \"\n"
        + "\" is processed live again.\" = \" kembali diproses langsung.\"\n"
        + "\"Loading \" = \"Memuat \"\n"
        + "\" into \" = \" ke \"\n"
        + "\" minutes ago\" = \" menit lalu\"\n"
        + "\" hours ago\" = \" jam lalu\"\n"
        + "\" days ago\" = \" hari lalu\"\n"
        + "\"Simple (A S D F = C D E F)\" = \"Sederhana (A S D F = C D E F)\"\n"
        + "\"Input\" = \"Input\"\n"
        + "\"None\" = \"Tidak ada\"\n"
        + "\"No audio inputs on this device\" = \"Device ini tidak punya input audio\"\n"
        + "\"Remove this folder\" = \"Hapus folder ini\"\n"
        + "\"Add folder\" = \"Tambah folder\"\n"
        + "\"Envelope\" = \"Envelope\"\n"
        + "\"LFO\" = \"LFO\"\n"
        + "\"Filter\" = \"Filter\"\n"
        + "\"Levels adjustment\" = \"Penyetelan level\"\n"
        + "\"Polyphony\" = \"Polifoni\"\n"
        + "\"Time\" = \"Waktu\"\n"
        + "\"Group\" = \"Grup\"\n"
        + "\"Arpeggiator\" = \"Arpeggiator\"\n"
        + "\"Echo delay / fat mode\" = \"Echo delay / fat mode\"\n"
        + "\"Generator\" = \"Generator\"\n"
        + "\"Envelope and instrument settings\" = \"Setelan envelope dan instrument\"\n"
        + "\"Miscellaneous functions\" = \"Fungsi lain-lain\"\n"
        + "\"(not wired up yet)\" = \"(belum tersambung)\"\n"
        + "\"Projects\" = \"Proyek\"\n"
        + "\"Samples\" = \"Sampel\"\n"
        + "\"Recordings\" = \"Rekaman\"\n"
        + "\"Presets\" = \"Preset\"\n"
        + "\"Plugins\" = \"Plugin\"\n"
        + "\"Language\" = \"Bahasa\"\n"
        + "\"some labels change after a restart\" = \"sebagian label berubah setelah restart\"\n"
        + "\"Octave\" = \"Oktaf\"\n"
        + "\"Z X C V B N M  +  Q W E R T Y U   (two octaves, like FL)\" = \"Z X C V B N M  +  Q W E R T Y U   (dua oktaf, seperti FL)\"\n"
        + "\"A S D F G H J = C D E F G A B,  sharps on W E T Y U\" = \"A S D F G H J = C D E F G A B,  sharp di W E T Y U\"\n"
        + "\"No tracks yet\" = \"Belum ada track\"\n"
        + "\"Add track\" = \"Tambah track\"\n"
        + "\"New MIDI track\" = \"Track MIDI baru\"\n"
        + "\"New audio track\" = \"Track Audio baru\"\n"
        + "\"New bus\" = \"Bus baru\"\n"
        + "\"Delete track\" = \"Hapus track\"\n"
        + "\"Rename track...\" = \"Ganti nama track...\"\n"
        + "\"Rename track\" = \"Ganti nama track\"\n"
        + "\"Monitor input\" = \"Monitor input\"\n"
        + "\"Remove all plugins\" = \"Hapus semua plugin\"\n"
        + "\"Freeze track\" = \"Freeze track\"\n"
        + "\"Unfreeze track\" = \"Unfreeze track\"\n"
        + "\"Bounce to audio...\" = \"Bounce ke audio...\"\n"
        + "\"Bounce to audio\" = \"Bounce ke audio\"\n"
        + "\"Add automation\" = \"Tambah automation\"\n"
        + "\"Place clip\" = \"Taruh clip\"\n"
        + "\"Move clip\" = \"Geser clip\"\n"
        + "\"Delete clip\" = \"Hapus clip\"\n"
        + "\"Move selected clips\" = \"Geser clip terpilih\"\n"
        + "\"Delete selected clips\" = \"Hapus clip terpilih\"\n"
        + "\"Remove placement\" = \"Hapus penempatan\"\n"
        + "\"Restore full length\" = \"Kembalikan panjang penuh\"\n"
        + "\"Extend by 1 bar\" = \"Panjangkan 1 bar\"\n"
        + "\"Rename pattern...\" = \"Ganti nama pattern...\"\n"
        + "\"Rename pattern\" = \"Ganti nama pattern\"\n"
        + "\"Snap to grid\" = \"Snap ke grid\"\n"
        + "\"Snap on - click to turn off\" = \"Snap aktif - klik untuk mematikan\"\n"
        + "\"Snap off - click to turn on\" = \"Snap mati - klik untuk menyalakan\"\n"
        + "\"Follow playhead during playback\" = \"Ikuti playhead saat playback\"\n"
        + "\"Collapse panel\" = \"Turunkan panel\"\n"
        + "\"Select - move and trim clips\" = \"Select - geser & trim clip\"\n"
        + "\"Select - pick and move notes\" = \"Select - pilih & geser note\"\n"
        + "\"Paint - place clips\" = \"Paint - taruh clip\"\n"
        + "\"Draw - place notes\" = \"Draw - taruh note\"\n"
        + "\"Delete - remove clips\" = \"Delete - hapus clip\"\n"
        + "\"Delete - remove notes\" = \"Delete - hapus note\"\n"
        + "\"Slip - slide the audio inside a clip\" = \"Slip - geser isi di dalam clip\"\n"
        + "\"Slice - cut a clip in two\" = \"Slice - potong clip jadi dua\"\n"
        + "\"Zoom - drag out an area\" = \"Zoom - tarik area untuk memperbesar\"\n"
        + "\"Playback - click to play from there\" = \"Playback - klik untuk memutar dari situ\"\n"
        + "\"Playback - click to play\" = \"Playback - klik untuk memutar\"\n"
        + "\"Add automation lane\" = \"Tambah lane automation\"\n"
        + "\"Remove automation lane\" = \"Hapus lane automation\"\n"
        + "\"Add automation point\" = \"Tambah titik automation\"\n"
        + "\"Move automation point\" = \"Geser titik automation\"\n"
        + "\"Delete automation point\" = \"Hapus titik automation\"\n"
        + "\"Bend automation\" = \"Bengkokkan automation\"\n"
        + "\"Straighten automation curve\" = \"Luruskan kurva automation\"\n"
        + "\"Clear automation lane\" = \"Kosongkan lane automation\"\n"
        + "\"Bypass automation\" = \"Bypass automation\"\n"
        + "\"Enable automation\" = \"Aktifkan automation\"\n"
        + "\"Delete all points\" = \"Hapus semua titik\"\n"
        + "\"Delete point\" = \"Hapus titik\"\n"
        + "\"Click to place a point\" = \"Klik untuk menaruh titik\"\n"
        + "\"Create automation clip\" = \"Buat automation clip\"\n"
        + "\"Remove automation\" = \"Hapus automation\"\n"
        + "\"All types\" = \"Semua jenis\"\n"
        + "\"All\" = \"Semua\"\n"
        + "\"Generators\" = \"Generator\"\n"
        + "\"Effects\" = \"Efek\"\n"
        + "\"Off\" = \"Mati\"\n"
        + "\"On\" = \"Aktif\"\n"
        + "\"No notes yet\" = \"Belum ada note\"\n"
        + "\"Delete note\" = \"Hapus note\"\n"
        + "\"Pattern length\" = \"Panjang pattern\"\n"
        + "\"Pattern length follows its contents.\" = \"Panjang pattern mengikuti isinya.\"\n"
        + "\"Pattern length locked to \" = \"Panjang pattern dikunci di \"\n"
        + "\"Select a MIDI track to use the step sequencer\" = \"Pilih track MIDI untuk memakai step sequencer\"\n"
        + "\"Name for PAT \" = \"Nama untuk PAT \"\n"
        + "\"Name for track \" = \"Nama untuk track \"\n"
        + "\"No plugins yet - press Scan\" = \"Belum ada plugin - tekan Scan\"\n"
        + "\"No plugins match this filter\" = \"Tidak ada plugin untuk filter ini\"\n"
        + "\"Scanning plugins...\" = \"Scanning plugin...\"\n"
        + "\"Scanning plugin folders...\" = \"Scanning folder plugin...\"\n"
        + "\"Scan plugins now\" = \"Scan plugin sekarang\"\n"
        + "\"Plugin library\" = \"Plugin library\"\n"
        + "\"Plugin search paths\" = \"Plugin search paths\"\n"
        + "\"No instrument yet\" = \"Belum ada instrument\"\n"
        + "\"Click to load the selected plugin\" = \"Klik untuk load plugin terpilih\"\n"
        + "\"Open instrument editor\" = \"Buka editor instrument\"\n"
        + "\"Open plugin editor\" = \"Buka editor plugin\"\n"
        + "\"Remove instrument\" = \"Lepas instrument\"\n"
        + "\"Remove all inserts\" = \"Hapus semua insert\"\n"
        + "\"Pick a plugin in the PLUGINS panel first.\" = \"Pilih dulu plugin di panel PLUGINS.\"\n"
        + "\"The plugin could not be created.\" = \"Plugin gagal dibuat.\"\n"
        + "\"This track has no instrument yet.\" = \"Track ini belum punya instrument.\"\n"
        + "\"This track has no plugin to open.\" = \"Track ini belum punya plugin yang bisa dibuka.\"\n"
        + "\"The mixer is full - no more tracks can be added.\" = \"Mixer sudah penuh - track baru tidak bisa ditambahkan.\"\n"
        + "\"Search samples, presets, plugins...\" = \"Cari sample, preset, plugin...\"\n"
        + "\"Nothing here\" = \"Tidak ada item\"\n"
        + "\"Empty folder\" = \"Folder kosong\"\n"
        + "\"No automatic backups yet.\" = \"Belum ada backup otomatis.\"\n"
        + "\"No templates yet.\" = \"Template belum tersedia.\"\n"
        + "\"No projects in \" = \"Belum ada project di \"\n"
        + "\"Count-in off.\" = \"Count-in mati.\"\n"
        + "\"Count-in before recording\" = \"Count-in sebelum record\"\n"
        + "\"Metronome off.\" = \"Metronome mati.\"\n"
        + "\"Recording finished.\" = \"Recording selesai.\"\n"
        + "\"Recording saved: \" = \"Rekaman disimpan: \"\n"
        + "\"Recording to \" = \"Recording ke \"\n"
        + "\"No audio input - recording MIDI only.\" = \"Tidak ada input audio - merekam MIDI saja.\"\n"
        + "\"Audio input could not be recorded; MIDI still was.\" = \"Audio input tidak bisa direkam; MIDI tetap direkam.\"\n"
        + "\"No audio device\" = \"Tidak ada audio device\"\n"
        + "\"Pattern mode: the active pattern loops.\" = \"Pattern mode: pattern aktif diulang.\"\n"
        + "\"Song mode: plays the pattern placements on the playlist.\" = \"Song mode: memainkan penempatan pattern di playlist.\"\n"
        + "\"New project\" = \"Project baru\"\n"
        + "\"New project created.\" = \"Project baru dibuat.\"\n"
        + "\"Open project\" = \"Buka project\"\n"
        + "\"Save project\" = \"Simpan project\"\n"
        + "\"Save\" = \"Simpan\"\n"
        + "\"Cancel\" = \"Batal\"\n"
        + "\"Saved: \" = \"Tersimpan: \"\n"
        + "\"Project opened: \" = \"Project dibuka: \"\n"
        + "\"Project opened: patterns, audio clips, plugins and the panel layout were restored.\" = \"Project dibuka: pattern, clip audio, plugin dan layout panel dipulihkan.\"\n"
        + "\"Panel layout reset to the default.\" = \"Layout panel dikembalikan ke default.\"\n"
        + "\"Nothing to undo.\" = \"Tidak ada yang bisa di-undo.\"\n"
        + "\"Nothing to redo.\" = \"Tidak ada yang bisa di-redo.\"\n"
        + "\"Export audio to WAV\" = \"Export audio ke WAV\"\n"
        + "\"Export failed\" = \"Export gagal\"\n"
        + "\"Export finished: \" = \"Export selesai: \"\n"
        + "\"The export settings are not valid.\" = \"Pengaturan export tidak valid.\"\n"
        + "\"Export length is zero - there is nothing to render.\" = \"Panjang export nol - tidak ada yang bisa dirender.\"\n"
        + "\"The render settings are not valid.\" = \"Pengaturan render tidak valid.\"\n"
        + "\"Render length is zero - there is nothing to render.\" = \"Panjang render nol - tidak ada yang bisa dirender.\"\n"
        + "\"Writing the file failed part way through the render.\" = \"Penulisan file gagal di tengah render.\"\n"
        + "\"The wav writer could not be created.\" = \"Writer wav gagal dibuat.\"\n"
        + "\"The destination folder could not be created: \" = \"Folder tujuan tidak bisa dibuat: \"\n"
        + "\"The file could not be written: \" = \"File tidak bisa ditulis: \"\n"
        + "\"Track render failed\" = \"Render track gagal\"\n"
        + "\"Freeze failed\" = \"Freeze gagal\"\n"
        + "\"Freeze finished: \" = \"Freeze selesai: \"\n"
        + "\"Bounce failed\" = \"Bounce gagal\"\n"
        + "\"Bounce finished: \" = \"Bounce selesai: \"\n"
        + "\"The render could not be read back: \" = \"Hasil render tidak bisa dibaca kembali: \"\n"
        + "\"The render could not be placed on the new track.\" = \"Hasil render tidak bisa ditaruh di track baru.\"\n"
        + "\"Empty file: \" = \"File kosong: \"\n"
        + "\"File not found: \" = \"File tidak ditemukan: \"\n"
        + "\"Unsupported format: \" = \"Format tidak didukung: \"\n"
        + "\"File too long to load into memory: \" = \"File terlalu panjang untuk dimuat ke memori: \"\n"
        + "\"Computer keyboard as MIDI\" = \"Keyboard komputer sebagai MIDI\"\n"
        + "\"Play notes from the keyboard when there is no controller\" = \"Mainkan nada dari keyboard kalau tidak ada controller\"\n"
        + "\"Computer keyboard is now a MIDI controller.\" = \"Keyboard komputer aktif sebagai MIDI.\"\n"
        + "\"The computer keyboard no longer plays notes.\" = \"Keyboard komputer tidak lagi memainkan nada.\"\n"
        + "\"Keyboard octave: \" = \"Oktaf keyboard: \"\n"
        + "\"Octave up\" = \"Oktaf naik\"\n"
        + "\"Octave down\" = \"Oktaf turun\"\n"
        + "\"Raise / lower the keyboard octave\" = \"Naik / turun oktaf keyboard\"\n"
        + "\"Show tooltips on every control\" = \"Tampilkan tooltip di semua kontrol\"\n"
        + "\"Auto-scroll the playlist during playback\" = \"Auto-scroll playlist saat playback\"\n"
        + "\"Open the plugin editor automatically after loading\" = \"Buka plugin editor otomatis setelah load\"\n"
        + "\"Browser on the left\" = \"Browser di kiri\"\n"
        + "\"Browser on the right\" = \"Browser di kanan\"\n"
        + "\"Browser at the bottom\" = \"Browser di bawah\"\n"
        + "\"Right click (note)\" = \"Klik kanan (note)\"\n"
        + "\"Scroll horizontally\" = \"Geser horizontal\"\n"
        + "\"Active track: \" = \"Track aktif: \"\n"
        + "\"Track: \" = \"Track: \"\n"
        + "\"No track\" = \"Tidak ada track\"\n"
        + "\"Pick an audio track first - \" = \"Pilih track audio dulu - \"\n"
        + "\" loaded as an insert on \" = \" jadi insert di \"\n"
        + "\" loaded as the instrument on \" = \" jadi instrument di \"\n"
        + "\" is not supported yet.\" = \" belum didukung.\"\n"
        + "\" cannot be placed on a MIDI track.\" = \" tidak bisa ditaruh di track MIDI.\"\n"
        + "\" could not be restored: its description is damaged.\" = \" tidak bisa dipulihkan: deskripsi rusak.\"\n"
        + "\" could not be found\" = \" tidak ditemukan lagi\"\n"
        + "\" could not be read: \" = \" tidak terbaca: \"\n"
        + "\" - the track is processed live again.\" = \" - track diproses langsung.\"\n"
        + "\" (audio track - the MIDI editor is empty)\" = \" (track audio - editor MIDI kosong)\"\n"
        + "\" (bus - receives sends from other tracks)\" = \" (bus - menerima kiriman track lain)\"\n"
        + "\" MIDI notes recorded.\" = \" note MIDI direkam.\"\n"
        + "\" - MIDI was recorded too.\" = \" - MIDI juga direkam.\"\n"
        + "\" bars before recording.\" = \" bar sebelum record.\"\n"
        + "\" plugins found.\" = \" plugin terdeteksi.\"\n"
        + "\"just now\" = \"baru saja\"\n"
        + "\" does not exist\" = \" belum ada\"\n";
}

} // namespace djr
