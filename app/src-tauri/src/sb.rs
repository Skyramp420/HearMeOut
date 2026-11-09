// Soundboard: zoznam zvukov (soundboard.ini v %APPDATA%\HearMeOut), import súborov, prehrávanie cez HearMeOutHost.
use serde::{Deserialize, Serialize};
use std::path::PathBuf;

const EXTS: &[&str] = &["mp3", "wav", "flac"];
const MAX_BYTES: u64 = 60 * 1024 * 1024;

fn app_dir() -> PathBuf {
    let d = PathBuf::from(std::env::var("APPDATA").unwrap_or_default()).join("HearMeOut");
    let _ = std::fs::create_dir_all(d.join("sounds"));
    d
}
fn sounds_dir() -> PathBuf { app_dir().join("sounds") }

#[derive(Serialize, Deserialize, Clone, Default)]
pub struct Pad {
    pub id: String,
    pub name: String,
    pub file: String,
    pub vol: f32,          // %
    pub hk: String,        // "mods,vk" alebo ""
    pub hk_label: String,
}

#[derive(Serialize, Deserialize, Clone)]
pub struct Board {
    pub mic_volume: f32,
    pub local_volume: f32,
    pub normalize: bool,
    pub stop_hk: String,
    pub stop_label: String,
    pub pads: Vec<Pad>,
}
impl Default for Board {
    fn default() -> Self {
        Board { mic_volume: 80.0, local_volume: 60.0, normalize: true, stop_hk: String::new(), stop_label: String::new(), pads: vec![] }
    }
}

fn clean(s: &str) -> String { s.chars().filter(|c| *c != '\n' && *c != '\r').collect() }
fn valid_hk(s: &str) -> bool {
    s.is_empty() || { let mut it = s.split(','); matches!((it.next().map(|a| a.parse::<u32>()), it.next().map(|b| b.parse::<u32>()), it.next()), (Some(Ok(_)), Some(Ok(_)), None)) }
}

pub fn read_board() -> Board {
    let t = std::fs::read_to_string(app_dir().join("soundboard.ini")).unwrap_or_default();
    let mut b = Board::default();
    let mut labels = std::collections::HashMap::new();
    for l in t.lines() {
        let Some((k, v)) = l.split_once('=') else { continue };
        match k {
            "mic_volume" => b.mic_volume = v.parse().unwrap_or(80.0),
            "local_volume" => b.local_volume = v.parse().unwrap_or(60.0),
            "normalize" => b.normalize = v == "1",
            "stop_hotkey" => b.stop_hk = v.to_string(),
            "stop_label" => b.stop_label = v.to_string(),
            "label" => { if let Some((id, lab)) = v.split_once('|') { labels.insert(id.to_string(), lab.to_string()); } }
            "pad" => {
                let p: Vec<&str> = v.splitn(5, '|').collect();
                if p.len() == 5 {
                    b.pads.push(Pad { id: p[0].into(), vol: p[1].parse().unwrap_or(100.0), hk: p[2].into(), file: p[3].into(), name: p[4].into(), hk_label: String::new() });
                }
            }
            _ => {}
        }
    }
    for p in &mut b.pads { if let Some(l) = labels.get(&p.id) { p.hk_label = l.clone(); } }
    b
}

#[tauri::command]
pub fn sb_get() -> Board { read_board() }

#[tauri::command]
pub fn sb_set(board: Board) -> Result<(), String> {
    let mut s = String::from("; HearMeOut – soundboard (spravuje appka)\n");
    s += &format!("mic_volume={}\nlocal_volume={}\nnormalize={}\n", board.mic_volume.clamp(0.0, 150.0), board.local_volume.clamp(0.0, 150.0), if board.normalize { 1 } else { 0 });
    if !valid_hk(&board.stop_hk) { return Err("Neplatná skratka.".into()); }
    s += &format!("stop_hotkey={}\nstop_label={}\n", board.stop_hk, clean(&board.stop_label));
    for p in &board.pads {
        if p.id.is_empty() || p.id.contains('|') || p.id.contains(' ') || p.file.contains('|') || p.file.contains('\n') || !valid_hk(&p.hk) {
            return Err("Neplatný zvuk v zozname.".into());
        }
        s += &format!("pad={}|{}|{}|{}|{}\n", p.id, p.vol.clamp(0.0, 200.0), p.hk, p.file, clean(&p.name));
        if !p.hk_label.is_empty() { s += &format!("label={}|{}\n", p.id, clean(&p.hk_label)); }
    }
    let path = app_dir().join("soundboard.ini");
    let tmp = path.with_extension("ini.tmp");
    std::fs::write(&tmp, s).map_err(|e| e.to_string())?;
    std::fs::rename(&tmp, &path).map_err(|e| e.to_string())?;
    // zmaž skopírované zvuky, ktoré už v zozname nie sú
    if let Ok(rd) = std::fs::read_dir(sounds_dir()) {
        for e in rd.flatten() {
            let name = e.file_name().to_string_lossy().to_string();
            if !board.pads.iter().any(|p| p.file.eq_ignore_ascii_case(&name)) { let _ = std::fs::remove_file(e.path()); }
        }
    }
    let _ = crate::host::send("sbreload");
    Ok(())
}

fn new_id() -> String {
    use std::time::{SystemTime, UNIX_EPOCH};
    let n = SystemTime::now().duration_since(UNIX_EPOCH).map(|d| d.as_nanos()).unwrap_or(0);
    format!("{:x}", (n ^ (n >> 17)) as u64 & 0xffff_ffff_ffff)
}

/// Skopíruje zvukové súbory do priečinka appky a vráti nové položky (ešte neuložené).
#[tauri::command]
pub fn sb_import(paths: Vec<String>) -> Result<Vec<Pad>, String> {
    let mut out = vec![];
    let mut skipped = vec![];
    for (i, p) in paths.iter().enumerate() {
        let src = PathBuf::from(p);
        let ext = src.extension().and_then(|e| e.to_str()).unwrap_or("").to_lowercase();
        let name = src.file_stem().and_then(|s| s.to_str()).unwrap_or("Zvuk").to_string();
        let size = std::fs::metadata(&src).map(|m| m.len()).unwrap_or(0);
        if !EXTS.contains(&ext.as_str()) || size == 0 || size > MAX_BYTES { skipped.push(name); continue; }
        let id = format!("{}{}", new_id(), i);
        let file = format!("{id}.{ext}");
        std::fs::copy(&src, sounds_dir().join(&file)).map_err(|e| format!("Neviem skopírovať {name}: {e}"))?;
        let short: String = name.replace(['_', '-'], " ").chars().take(40).collect();
        out.push(Pad { id, name: short.trim().to_string(), file, vol: 100.0, hk: String::new(), hk_label: String::new() });
    }
    if out.is_empty() && !skipped.is_empty() {
        return Err("Podporované sú súbory MP3, WAV a FLAC (do 60 MB).".into());
    }
    Ok(out)
}

#[tauri::command]
pub fn sb_play(id: String) -> Result<(), String> {
    if id.contains('\n') { return Err("Neplatný zvuk.".into()); }
    crate::host::host_start()?;
    crate::host::send(&format!("play {id}"))
}

#[tauri::command]
pub fn sb_stop() { let _ = crate::host::send("stopall"); }

/// Windows dialóg na výber zvukov (viac naraz).
#[cfg(windows)]
#[tauri::command]
pub fn sb_pick(window: tauri::WebviewWindow) -> Vec<String> {
    use windows_sys::Win32::UI::Controls::Dialogs::*;
    let owner = window.hwnd().map(|h| h.0 as _).unwrap_or(std::ptr::null_mut());
    let filter: Vec<u16> = "Zvuky (MP3, WAV, FLAC)\0*.mp3;*.wav;*.flac\0\0".encode_utf16().collect();
    let title: Vec<u16> = "Pridať zvuky\0".encode_utf16().collect();
    let mut buf = vec![0u16; 65536];
    let mut ofn: OPENFILENAMEW = unsafe { std::mem::zeroed() };
    ofn.lStructSize = std::mem::size_of::<OPENFILENAMEW>() as u32;
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter.as_ptr();
    ofn.lpstrFile = buf.as_mut_ptr();
    ofn.nMaxFile = buf.len() as u32;
    ofn.lpstrTitle = title.as_ptr();
    ofn.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if unsafe { GetOpenFileNameW(&mut ofn) } == 0 { return vec![]; }
    // formát: priečinok\0súbor1\0súbor2\0\0  alebo  celá_cesta\0\0
    let parts: Vec<String> = buf.split(|&c| c == 0).take_while(|s| !s.is_empty()).map(String::from_utf16_lossy).collect();
    match parts.len() {
        0 => vec![],
        1 => parts,
        _ => parts[1..].iter().map(|f| PathBuf::from(&parts[0]).join(f).display().to_string()).collect(),
    }
}
#[cfg(not(windows))]
#[tauri::command]
pub fn sb_pick() -> Vec<String> { vec![] }
