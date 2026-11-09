// Ovládanie HearMeOutHost.exe (VST pluginy): skenovanie, reťazec, štart/stop, príkazy, autoštart.
use serde::{Deserialize, Serialize};
use std::path::{Path, PathBuf};

fn app_dir() -> PathBuf {
    let d = PathBuf::from(std::env::var("APPDATA").unwrap_or_default()).join("HearMeOut");
    let _ = std::fs::create_dir_all(&d);
    d
}
fn host_exe() -> PathBuf {
    std::env::current_exe().ok().and_then(|p| p.parent().map(|d| d.join("HearMeOutHost.exe"))).unwrap_or_default()
}

#[derive(Serialize, Clone)]
pub struct FoundPlugin { pub name: String, pub path: String, pub folder: String }

fn scan_dir(dir: &Path, depth: u32, out: &mut Vec<FoundPlugin>) {
    let Ok(rd) = std::fs::read_dir(dir) else { return };
    for e in rd.flatten() {
        let p = e.path();
        let is_vst3 = p.extension().and_then(|x| x.to_str()).map(|x| x.eq_ignore_ascii_case("vst3")).unwrap_or(false);
        if is_vst3 {
            let name = p.file_stem().and_then(|s| s.to_str()).unwrap_or("?").to_string();
            let folder = p.parent().and_then(|x| x.file_name()).and_then(|x| x.to_str()).unwrap_or("").to_string();
            out.push(FoundPlugin { name, path: p.display().to_string(), folder });
        } else if depth > 0 && p.is_dir() {
            scan_dir(&p, depth - 1, out);
        }
    }
}

#[tauri::command]
pub fn vst_scan() -> Vec<FoundPlugin> {
    let mut roots = vec![PathBuf::from(std::env::var("CommonProgramFiles").unwrap_or_else(|_| "C:\\Program Files\\Common Files".into())).join("VST3")];
    if let Ok(l) = std::env::var("LOCALAPPDATA") { roots.push(PathBuf::from(l).join("Programs").join("Common").join("VST3")); }
    let mut out = Vec::new();
    for r in roots { scan_dir(&r, 4, &mut out); }
    out.sort_by(|a, b| a.name.to_lowercase().cmp(&b.name.to_lowercase()));
    out.dedup_by(|a, b| a.path.eq_ignore_ascii_case(&b.path));
    out
}

#[derive(Serialize, Deserialize, Clone)]
pub struct ChainSlot { pub uid: String, pub enabled: bool, pub path: String }

#[tauri::command]
pub fn vst_chain_get() -> Vec<ChainSlot> {
    let t = std::fs::read_to_string(app_dir().join("chain.ini")).unwrap_or_default();
    t.lines()
        .filter_map(|l| l.strip_prefix("slot="))
        .filter_map(|r| {
            let mut it = r.splitn(3, '|');
            Some(ChainSlot { uid: it.next()?.to_string(), enabled: it.next()? == "1", path: it.next()?.to_string() })
        })
        .collect()
}

#[tauri::command]
pub fn vst_chain_set(slots: Vec<ChainSlot>) -> Result<(), String> {
    let mut s = String::from("; HearMeOut – reťazec VST pluginov (spravuje appka)\n");
    for c in &slots {
        if c.uid.contains('|') || c.uid.contains('\n') || c.path.contains('\n') { return Err("Neplatný slot.".into()); }
        s.push_str(&format!("slot={}|{}|{}\n", c.uid, if c.enabled { 1 } else { 0 }, c.path));
    }
    let p = app_dir().join("chain.ini");
    let tmp = p.with_extension("ini.tmp");
    std::fs::write(&tmp, s).map_err(|e| e.to_string())?;
    std::fs::rename(&tmp, &p).map_err(|e| e.to_string())?;
    let _ = send("reload");
    Ok(())
}

#[tauri::command]
pub fn host_status() -> serde_json::Value {
    let running = find_window().is_some();
    let mut v: serde_json::Value = std::fs::read_to_string(app_dir().join("host-status.json"))
        .ok().and_then(|t| serde_json::from_str(&t).ok()).unwrap_or(serde_json::json!({}));
    v["running"] = serde_json::Value::Bool(running);
    v["installed"] = serde_json::Value::Bool(host_exe().exists());
    v
}

#[cfg(windows)]
fn wide(s: &str) -> Vec<u16> { s.encode_utf16().chain(std::iter::once(0)).collect() }

#[cfg(windows)]
fn find_window() -> Option<windows_sys::Win32::Foundation::HWND> {
    use windows_sys::Win32::UI::WindowsAndMessaging::FindWindowW;
    let cls = wide("HearMeOutHostWnd");
    let h = unsafe { FindWindowW(cls.as_ptr(), std::ptr::null()) };
    if h.is_null() { None } else { Some(h) }
}
#[cfg(not(windows))]
fn find_window() -> Option<()> { None }

#[cfg(windows)]
fn send(cmd: &str) -> Result<(), String> {
    use windows_sys::Win32::System::DataExchange::COPYDATASTRUCT;
    use windows_sys::Win32::UI::WindowsAndMessaging::{SendMessageTimeoutW, SMTO_ABORTIFHUNG, WM_COPYDATA};
    let h = find_window().ok_or("HearMeOut Host nebeží.")?;
    let bytes = cmd.as_bytes();
    let cd = COPYDATASTRUCT { dwData: 1, cbData: bytes.len() as u32, lpData: bytes.as_ptr() as *mut _ };
    let mut res = 0usize;
    unsafe { SendMessageTimeoutW(h, WM_COPYDATA, 0, &cd as *const _ as isize, SMTO_ABORTIFHUNG, 3000, &mut res) };
    Ok(())
}
#[cfg(not(windows))]
fn send(_: &str) -> Result<(), String> { Err("Len pre Windows".into()) }

#[tauri::command]
pub fn host_start() -> Result<(), String> {
    if find_window().is_some() { return Ok(()); }
    let exe = host_exe();
    if !exe.exists() { return Err("Chýba HearMeOutHost.exe vedľa appky.".into()); }
    std::process::Command::new(&exe).spawn().map_err(|e| format!("Host sa nespustil: {e}"))?;
    for _ in 0..30 { if find_window().is_some() { return Ok(()); } std::thread::sleep(std::time::Duration::from_millis(100)); }
    Err("Host sa spustil, ale neodpovedá.".into())
}

#[tauri::command]
pub fn host_stop() { let _ = send("quit"); }

pub fn host_running() -> bool { find_window().is_some() }

#[tauri::command]
pub fn host_open_editor(uid: String) -> Result<(), String> {
    host_start()?;
    send(&format!("open {uid}"))
}

#[cfg(windows)]
#[tauri::command]
pub fn host_autostart(on: bool) -> Result<(), String> {
    use winreg::enums::{HKEY_CURRENT_USER, KEY_SET_VALUE};
    use winreg::RegKey;
    let run = RegKey::predef(HKEY_CURRENT_USER)
        .open_subkey_with_flags("Software\\Microsoft\\Windows\\CurrentVersion\\Run", KEY_SET_VALUE)
        .map_err(|e| e.to_string())?;
    if on { run.set_value("HearMeOutHost", &format!("\"{}\"", host_exe().display())).map_err(|e| e.to_string()) }
    else { let _ = run.delete_value("HearMeOutHost"); Ok(()) }
}
#[cfg(not(windows))]
#[tauri::command]
pub fn host_autostart(on: bool) -> Result<(), String> { let _ = on; Ok(()) }
